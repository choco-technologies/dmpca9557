#ifndef DMPCA9557_TYPES_H
#define DMPCA9557_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/** Registers of the chip (the command byte) */
#define DMPCA9557_REG_INPUT         0x00U   /**< Level of the pins (read only) */
#define DMPCA9557_REG_OUTPUT        0x01U   /**< Level the output pins are driven to */
#define DMPCA9557_REG_POLARITY      0x02U   /**< 1 = the input register shows the inverted level of the pin */
#define DMPCA9557_REG_CONFIG        0x03U   /**< 1 = input (the power-on state of all pins), 0 = output */

/** Pins of the chip */
#define DMPCA9557_PINS              8U
#define DMPCA9557_ALL_PINS          0xFFU

/**
 * @brief Configuration of a group of pins of the chip (the ini section of the driver)
 *
 * The node of the driver is the group: all its pins have the same direction and are set together (as the node of dmgpio is a group of
 * the pins of one port). A chip with pins of different directions is several sections, the transfers of all of them are serialized.
 */
typedef struct
{
    uint8_t address;            /**< 7-bit I2C address of the chip (0x18 .. 0x1F: 0x18 | A2 A1 A0 ... see the data sheet) */
    uint8_t pins;               /**< Bit N = the pin IO<N> belongs to the group (not 0) */
    bool    output;             /**< The pins are outputs (otherwise inputs) */
    bool    initial_set;        /**< The configuration gives the level of the outputs at the start */
    bool    initial_high;       /**< That level */
} dmpca9557_config_t;

#endif /* DMPCA9557_TYPES_H */
