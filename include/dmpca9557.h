#ifndef DMPCA9557_H
#define DMPCA9557_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmod_types.h"
#include "dmpca9557_defs.h"
#include "dmpca9557_types.h"

/**
 * @brief Check a configuration without touching hardware
 *
 * @return true when the address is one the chip can have (0x18..0x1F) and the group has at least one pin.
 */
dmod_dmpca9557_api(1.0, bool, _validate_config, ( const dmpca9557_config_t *config ));

/**
 * @brief The value of the output register after the pins of the group are set to a level (the other pins are as they were)
 */
dmod_dmpca9557_api(1.0, uint8_t, _output_value, ( uint8_t current, uint8_t pins, bool high ));

/**
 * @brief The value of the configuration register after the pins of the group are made outputs or inputs (the other pins are as they were)
 */
dmod_dmpca9557_api(1.0, uint8_t, _config_value, ( uint8_t current, uint8_t pins, bool output ));

#endif // DMPCA9557_H
