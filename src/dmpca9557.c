#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmpca9557.h"
#include "dmdrvi.h"
#include "dmini.h"
#include "dmosi.h"
#include "dmgpio_types.h"
#include "dmi2c_types.h"
#include <errno.h>
#include <string.h>

/*
 * dmpca9557: the 8-bit I/O expander PCA9557 (NXP; the PCA9554, TCA9534 and the like work the same) on I2C, shown as GPIO pins.
 *
 * The node of a section is a group of the pins of the chip and follows the interface of dmgpio, so whatever drives a pin of dmgpio
 * (the reset line of a touch controller, say) drives it without knowing it is behind an expander: the commands
 * dmgpio_ioctl_cmd_toggle_pins, _set_pins_state, _get_high_pins_state, _get_low_pins_state and read() / write() with the
 * mask as a number ("0x01").
 *
 * The wiring is done by dmdevfs: the section of the pins and the section of the bus (dmi2c) are in one friends_group, and the friend
 * roles say which node is which:
 *
 *   i2c        the dmi2c node the chip is on (required)
 *
 * The chip is configured (the level of the outputs first, then the direction) when its bus is there and not before a client needs it.
 * The levels are read back from the chip before they are changed, so several sections of one chip do not undo each other (they are
 * serialized by one lock, shared by the sections of the module).
 */

/* Magic set to "P957" */
#define DMPCA9557_CONTEXT_MAGIC     0x50393537

#define STATE_BUF_SIZE              8       /* "0x%02X" */
#define WRITE_BUF_SIZE              16

typedef struct
{
    char   *path;                   /**< Node of the friend (NULL: not there) */
    void   *file;                   /**< Kept open once used */
} node_t;

struct dmdrvi_context
{
    uint32_t            magic;
    dmpca9557_config_t  config;
    uint8_t             instance;
    node_t              i2c;
    bool                started;    /**< The chip answered and the group is configured */
};

/* One lock for the module: the read-modify-write of a register is not interrupted by another section of the chip */
static dmosi_mutex_t g_lock = NULL;
static unsigned      g_users = 0;

static bool is_valid_context(dmdrvi_context_t context)
{
    return context != NULL && context->magic == DMPCA9557_CONTEXT_MAGIC;
}

/* ---- API ---- */

dmod_dmpca9557_api_declaration(1.0, bool, _validate_config, ( const dmpca9557_config_t *config ))
{
    if (config == NULL)
        return false;
    if (config->address < 0x18 || config->address > 0x1F)
        return false;
    return config->pins != 0U;
}

dmod_dmpca9557_api_declaration(1.0, uint8_t, _output_value, ( uint8_t current, uint8_t pins, bool high ))
{
    return high ? (uint8_t)(current | pins) : (uint8_t)(current & (uint8_t)~pins);
}

dmod_dmpca9557_api_declaration(1.0, uint8_t, _config_value, ( uint8_t current, uint8_t pins, bool output ))
{
    /* 1 = input */
    return output ? (uint8_t)(current & (uint8_t)~pins) : (uint8_t)(current | pins);
}

/* ---- Configuration ---- */

static bool string_is_high(const char *text, bool *high)
{
    if (strcmp(text, "high") == 0 || strcmp(text, "1") == 0 || strcmp(text, "on") == 0)
    {
        *high = true;
        return true;
    }
    if (strcmp(text, "low") == 0 || strcmp(text, "0") == 0 || strcmp(text, "off") == 0)
    {
        *high = false;
        return true;
    }
    return false;
}

static int read_config(dmdrvi_context_t context, dmini_context_t ini)
{
    dmpca9557_config_t *c = &context->config;

    context->instance = (uint8_t)dmini_get_int(ini, NULL, "instance", 0);
    c->address = (uint8_t)dmini_get_int(ini, NULL, "address", 0x18);
    if (dmini_has_key(ini, NULL, "pin"))
    {
        int pin = dmini_get_int(ini, NULL, "pin", -1);
        c->pins = (pin >= 0 && pin < (int)DMPCA9557_PINS) ? (uint8_t)(1U << pin) : 0U;
    }
    else
    {
        c->pins = (uint8_t)dmini_get_int(ini, NULL, "pins", 0);
    }

    const char *mode = dmini_get_string(ini, NULL, "mode", "input");
    if (strcmp(mode, "output") == 0)
    {
        c->output = true;
    }
    else if (strcmp(mode, "input") == 0)
    {
        c->output = false;
    }
    else
    {
        DMOD_LOG_ERROR("PCA9557: mode '%s' is not input or output\n", mode);
        return -EINVAL;
    }

    const char *state = dmini_get_string(ini, NULL, "state", NULL);
    if (state != NULL)
    {
        c->initial_set = string_is_high(state, &c->initial_high);
        if (!c->initial_set)
        {
            DMOD_LOG_ERROR("PCA9557: state '%s' is not high or low\n", state);
            return -EINVAL;
        }
    }

    if (!dmpca9557_validate_config(c))
    {
        DMOD_LOG_ERROR("PCA9557: invalid configuration (address 0x%02X, pins 0x%02X)\n", (unsigned)c->address, (unsigned)c->pins);
        return -EINVAL;
    }
    return 0;
}

/* ---- The node of the bus ---- */

static void node_close(node_t *node)
{
    if (node->file != NULL)
        Dmod_FileClose(node->file);
    node->file = NULL;
}

static void node_forget(node_t *node)
{
    node_close(node);
    Dmod_Free(node->path);
    node->path = NULL;
}

static void *node_file(node_t *node)
{
    if (node->file == NULL && node->path != NULL)
        node->file = Dmod_FileOpen(node->path, "r+");
    return node->file;
}

/* ---- The chip: a register is the command byte and one data byte ---- */

static int i2c_transfer(dmdrvi_context_t context, dmi2c_message_t *messages, size_t count)
{
    void *file = node_file(&context->i2c);
    dmi2c_transfer_t transfer = { messages, count };
    if (file == NULL)
        return -ENODEV;
    int ret = Dmod_Ioctl(file, dmi2c_ioctl_cmd_transfer, &transfer);
    if (ret != 0)
        node_close(&context->i2c);          /* the bus is opened again next time */
    return ret;
}

static int reg_read(dmdrvi_context_t context, uint8_t reg, uint8_t *value)
{
    dmi2c_message_t messages[2] = {
        { .address = context->config.address, .read = false, .data = &reg,  .size = 1 },
        { .address = context->config.address, .read = true,  .data = value, .size = 1 },
    };
    return i2c_transfer(context, messages, 2);
}

static int reg_write(dmdrvi_context_t context, uint8_t reg, uint8_t value)
{
    uint8_t buffer[2] = { reg, value };
    dmi2c_message_t message = { .address = context->config.address, .read = false, .data = buffer, .size = sizeof(buffer) };
    return i2c_transfer(context, &message, 1);
}

/* Drive the pins of the group to a level, the other pins are not changed (the lock is held) */
static int reg_update_output(dmdrvi_context_t context, bool high)
{
    uint8_t value;
    int ret = reg_read(context, DMPCA9557_REG_OUTPUT, &value);
    if (ret != 0)
        return ret;
    return reg_write(context, DMPCA9557_REG_OUTPUT, dmpca9557_output_value(value, context->config.pins, high));
}

/* Set the group up: the level first, so that an output does not glitch to the power-on level when it becomes one */
static int chip_start(dmdrvi_context_t context)
{
    const dmpca9557_config_t *c = &context->config;
    uint8_t value;
    int ret;

    if (c->output && c->initial_set)
    {
        ret = reg_update_output(context, c->initial_high);
        if (ret != 0)
        {
            DMOD_LOG_ERROR("PCA9557: no answer at 0x%02X (%d)\n", (unsigned)c->address, ret);
            return ret;
        }
    }
    ret = reg_read(context, DMPCA9557_REG_CONFIG, &value);
    if (ret != 0)
    {
        DMOD_LOG_ERROR("PCA9557: no answer at 0x%02X (%d)\n", (unsigned)c->address, ret);
        return ret;
    }
    ret = reg_write(context, DMPCA9557_REG_CONFIG, dmpca9557_config_value(value, c->pins, c->output));
    if (ret == 0)
        DMOD_LOG_INFO("PCA9557 0x%02X: pins 0x%02X are %s\n", (unsigned)c->address, (unsigned)c->pins, c->output ? "outputs" : "inputs");
    return ret;
}

/* Start the chip when its bus is there (the lock is held); a client that needs it before dmdevfs has told about the bus gets -ENODEV */
static int ensure_started(dmdrvi_context_t context)
{
    if (context->started)
        return 0;
    if (context->i2c.path == NULL)
        return -ENODEV;
    int ret = chip_start(context);
    if (ret == 0)
        context->started = true;
    return ret;
}

/* ---- Operations of the node ---- */

static int pins_set(dmdrvi_context_t context, bool high)
{
    if (!context->config.output)
        return -EPERM;
    dmosi_mutex_lock(g_lock);
    int ret = ensure_started(context);
    if (ret == 0)
        ret = reg_update_output(context, high);
    dmosi_mutex_unlock(g_lock);
    return ret;
}

static int pins_toggle(dmdrvi_context_t context)
{
    uint8_t value;

    if (!context->config.output)
        return -EPERM;
    dmosi_mutex_lock(g_lock);
    int ret = ensure_started(context);
    if (ret == 0)
        ret = reg_read(context, DMPCA9557_REG_OUTPUT, &value);
    if (ret == 0)
        ret = reg_write(context, DMPCA9557_REG_OUTPUT, (uint8_t)(value ^ context->config.pins));
    dmosi_mutex_unlock(g_lock);
    return ret;
}

/* The levels of the pins of the group, as the chip sees them (an output pin shows what it is driven to) */
static int pins_levels(dmdrvi_context_t context, uint8_t *levels)
{
    dmosi_mutex_lock(g_lock);
    int ret = ensure_started(context);
    if (ret == 0)
        ret = reg_read(context, DMPCA9557_REG_INPUT, levels);
    dmosi_mutex_unlock(g_lock);
    if (ret == 0)
        *levels &= context->config.pins;
    return ret;
}

static bool parse_mask(const char *text, unsigned long *value)
{
    unsigned base = 10;
    unsigned long result = 0;
    bool any = false;

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        base = 16;
        text += 2;
    }
    for (; *text != '\0'; text++)
    {
        unsigned digit;
        if (*text >= '0' && *text <= '9')
            digit = (unsigned)(*text - '0');
        else if (base == 16 && *text >= 'a' && *text <= 'f')
            digit = (unsigned)(*text - 'a') + 10U;
        else if (base == 16 && *text >= 'A' && *text <= 'F')
            digit = (unsigned)(*text - 'A') + 10U;
        else
            return false;
        result = result * base + digit;
        if (result > 0xFFUL)
            return false;
        any = true;
    }
    *value = result;
    return any;
}

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    (void)Config;
    DMOD_LOG_INFO("DMPCA9557 module initialized\n");
    return 0;
}

int dmod_deinit(void)
{
    DMOD_LOG_INFO("DMPCA9557 module deinitialized\n");
    return 0;
}

/* ---- DMDRVI interface ---- */

dmod_dmdrvi_dif_api_declaration(2.0, dmpca9557, dmdrvi_context_t, _create, ( dmini_context_t config, dmdrvi_dev_num_t* dev_num ))
{
    if (config == NULL || dev_num == NULL)
        return NULL;
    if (g_lock == NULL)
    {
        g_lock = dmosi_mutex_create(false);
        if (g_lock == NULL)
        {
            DMOD_LOG_ERROR("PCA9557: out of memory\n");
            return NULL;
        }
    }
    dmdrvi_context_t context = Dmod_Malloc(sizeof(struct dmdrvi_context));
    if (context == NULL)
        return NULL;
    memset(context, 0, sizeof(*context));
    context->magic = DMPCA9557_CONTEXT_MAGIC;

    if (read_config(context, config) != 0)
    {
        context->magic = 0;
        Dmod_Free(context);
        return NULL;
    }
    g_users++;
    dev_num->flags = DMDRVI_NUM_MAJOR;
    dev_num->major = (dmdrvi_dev_id_t)context->instance;
    return context;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmpca9557, void, _free, ( dmdrvi_context_t context ))
{
    if (!is_valid_context(context))
        return;
    /* The node of the bus is not touched: dmdevfs tears it down before this call, in reverse order of the sections */
    Dmod_Free(context->i2c.path);
    context->magic = 0;
    Dmod_Free(context);
    if (g_users > 0U)
        g_users--;
    if (g_users == 0U && g_lock != NULL)
    {
        dmosi_mutex_destroy(g_lock);
        g_lock = NULL;
    }
}

dmod_dmdrvi_dif_api_declaration(2.0, dmpca9557, void, _friend_changed, ( dmdrvi_context_t context, const dmdrvi_friend_info_t* info ))
{
    if (!is_valid_context(context) || info == NULL || info->friend_role == NULL)
        return;
    if (strcmp(info->friend_role, "i2c") != 0)
        return;
    dmosi_mutex_lock(g_lock);
    node_forget(&context->i2c);
    context->started = false;                       /* a new bus: the chip is set up again */
    if (info->state == dmdrvi_dev_state_ready && info->node_path != NULL)
    {
        context->i2c.path = Dmod_StrDup(info->node_path);
        if (context->i2c.path == NULL)
        {
            DMOD_LOG_ERROR("PCA9557: out of memory for the path of the bus\n");
        }
        else
        {
            (void)ensure_started(context);
        }
    }
    dmosi_mutex_unlock(g_lock);
}

dmod_dmdrvi_dif_api_declaration(2.0, dmpca9557, void*, _open, ( dmdrvi_context_t context, int flags, const dmdrvi_dev_num_t* dev_num ))
{
    return is_valid_context(context) ? context : NULL;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmpca9557, void, _close, ( dmdrvi_context_t context, void* handle ))
{
}

/* read(): the pins that are high, as "0x%02X" (as the node of dmgpio) */
dmod_dmdrvi_dif_api_declaration(2.0, dmpca9557, dmdrvi_ssize_t, _read, ( dmdrvi_context_t context, void* handle, void* buffer, size_t size, dmdrvi_offset_t offset ))
{
    char content[STATE_BUF_SIZE];
    uint8_t levels;

    if (offset < 0)
        return -EINVAL;
    if (!is_valid_context(context) || buffer == NULL)
        return -EINVAL;
    if (size == 0)
        return 0;
    int ret = pins_levels(context, &levels);
    if (ret != 0)
        return ret;
    int length = Dmod_SnPrintf(content, sizeof(content), "0x%02X", (unsigned)levels);
    if (length <= 0 || offset >= (dmdrvi_offset_t)length)
        return 0;
    size_t available = (size_t)length - (size_t)offset;
    size_t count = available < size ? available : size;
    memcpy(buffer, content + offset, count);
    return (dmdrvi_ssize_t)count;
}

/* write(): the pins of the group that are driven high, as a number (0x.. or decimal); the others of the group are driven low */
dmod_dmdrvi_dif_api_declaration(2.0, dmpca9557, dmdrvi_ssize_t, _write, ( dmdrvi_context_t context, void* handle, const void* buffer, size_t size, dmdrvi_offset_t offset ))
{
    char text[WRITE_BUF_SIZE];
    unsigned long mask;

    if (offset < 0)
        return -EINVAL;
    if (!is_valid_context(context) || buffer == NULL || size == 0)
        return -EINVAL;
    if (!context->config.output)
        return -EPERM;
    size_t length = size < sizeof(text) - 1U ? size : sizeof(text) - 1U;
    memcpy(text, buffer, length);
    text[length] = '\0';
    while (length > 0U && (text[length - 1U] == '\n' || text[length - 1U] == '\r' || text[length - 1U] == ' ' || text[length - 1U] == '\t'))
        text[--length] = '\0';
    if (!parse_mask(text, &mask))
        return -EINVAL;

    uint8_t high = (uint8_t)mask & context->config.pins;
    uint8_t value;
    dmosi_mutex_lock(g_lock);
    int ret = ensure_started(context);
    if (ret == 0)
        ret = reg_read(context, DMPCA9557_REG_OUTPUT, &value);
    if (ret == 0)
    {
        value = dmpca9557_output_value(value, context->config.pins, false);
        ret = reg_write(context, DMPCA9557_REG_OUTPUT, (uint8_t)(value | high));
    }
    dmosi_mutex_unlock(g_lock);
    return ret != 0 ? ret : (dmdrvi_ssize_t)size;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmpca9557, int, _ioctl, ( dmdrvi_context_t context, void* handle, int command, void* arg ))
{
    if (!is_valid_context(context))
        return -EINVAL;

    /* The switch is on the int, never on it cast to the enum (short enums on Cortex-M cut the commands of other classes down to ours) */
    switch (command)
    {
        case dmgpio_ioctl_cmd_toggle_pins:
            return pins_toggle(context);
        case dmgpio_ioctl_cmd_set_pins_state:
        {
            if (arg == NULL)
                return -EINVAL;
            const dmgpio_pins_state_t *state = arg;
            return pins_set(context, *state == dmgpio_pins_state_all_high);
        }
        case dmgpio_ioctl_cmd_get_high_pins_state:
        case dmgpio_ioctl_cmd_get_low_pins_state:
        {
            uint8_t levels;
            if (arg == NULL)
                return -EINVAL;
            int ret = pins_levels(context, &levels);
            if (ret != 0)
                return ret;
            dmgpio_pins_mask_t *mask = arg;
            *mask = command == dmgpio_ioctl_cmd_get_high_pins_state ? levels : (dmgpio_pins_mask_t)(context->config.pins & (uint8_t)~levels);
            return 0;
        }
        default:
            return -ENOTTY;     /* also the interrupts: the chip's INT line is not handled */
    }
}

dmod_dmdrvi_dif_api_declaration(2.0, dmpca9557, int, _flush, ( dmdrvi_context_t context, void* handle ))
{
    return is_valid_context(context) ? 0 : -EINVAL;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmpca9557, int, _stat, ( dmdrvi_context_t context, const char* path, dmdrvi_stat_t* stat ))
{
    if (!is_valid_context(context) || stat == NULL)
        return -EINVAL;
    stat->size = 4;                 /* "0x%02X" */
    stat->mode = context->config.output ? 0666 : 0444;
    return 0;
}
