#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmpca9557.h"

static dmpca9557_config_t reset_line(void)
{
    dmpca9557_config_t c = { .address = 0x18, .pins = 0x01, .output = true, .initial_set = true, .initial_high = true };
    return c;
}

DMOD_TEST_STEP(validate_accepts_the_board)
{
    dmpca9557_config_t c = reset_line();
    DMOD_TEST_EXPECT_TRUE(dmpca9557_validate_config(&c));
    c.pins = 0xFF;
    DMOD_TEST_EXPECT_TRUE(dmpca9557_validate_config(&c));
    c.address = 0x1F;
    DMOD_TEST_EXPECT_TRUE(dmpca9557_validate_config(&c));
}

DMOD_TEST_STEP(validate_rejects_bad_configurations)
{
    dmpca9557_config_t c = reset_line();
    DMOD_TEST_EXPECT_FALSE(dmpca9557_validate_config(NULL));
    c.pins = 0;
    DMOD_TEST_EXPECT_FALSE(dmpca9557_validate_config(&c));
    c = reset_line();
    c.address = 0x17;
    DMOD_TEST_EXPECT_FALSE(dmpca9557_validate_config(&c));
    c.address = 0x20;
    DMOD_TEST_EXPECT_FALSE(dmpca9557_validate_config(&c));
}

DMOD_TEST_STEP(output_value_changes_only_the_pins_of_the_group)
{
    DMOD_TEST_EXPECT_EQ(dmpca9557_output_value(0x00, 0x01, true), 0x01);
    DMOD_TEST_EXPECT_EQ(dmpca9557_output_value(0xF0, 0x01, true), 0xF1);
    DMOD_TEST_EXPECT_EQ(dmpca9557_output_value(0xFF, 0x01, false), 0xFE);
    DMOD_TEST_EXPECT_EQ(dmpca9557_output_value(0xA5, 0x0F, false), 0xA0);
    DMOD_TEST_EXPECT_EQ(dmpca9557_output_value(0xA5, 0x0F, true), 0xAF);
}

DMOD_TEST_STEP(config_value_one_is_an_input)
{
    /* The power-on state is all inputs: an output pin is a cleared bit, an input a set one */
    DMOD_TEST_EXPECT_EQ(dmpca9557_config_value(0xFF, 0x01, true), 0xFE);
    DMOD_TEST_EXPECT_EQ(dmpca9557_config_value(0xFE, 0x01, false), 0xFF);
    DMOD_TEST_EXPECT_EQ(dmpca9557_config_value(0xFF, 0x03, true), 0xFC);
    DMOD_TEST_EXPECT_EQ(dmpca9557_config_value(0x0C, 0x03, false), 0x0F);
}
