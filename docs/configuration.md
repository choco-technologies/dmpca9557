# dmpca9557 configuration

The section of a group of pins (`driver_name=dmpca9557`):

| Key | Default | |
|---|---|---|
| `instance` | 0 | the node is `/dev/dmpca9557<instance>` |
| `address` | 0x18 | the 7-bit I2C address of the chip, 0x18 .. 0x1F |
| `pin` | | one pin of the chip, 0..7 (`IO0`..`IO7`) |
| `pins` | | the pins as a mask, bit N = `IO<N>`; used when there is no `pin` |
| `mode` | input | `input` or `output`: the direction of all the pins of the group |
| `state` | | `high` / `low`: the level an output is driven to when the group is set up (before it becomes an output, so it does not glitch) |

The node is the group: its pins have the same direction and are set together, as the node of `dmgpio` is the pins of one port. A chip
with pins of different directions is several sections. The levels are read back from the chip before they are changed, and the
transfers of all the sections are serialized, so the sections of one chip do not undo each other.

## Friends

| `friend_role` | |
|---|---|
| `i2c` | the dmi2c node the chip is on (required) |

The group is set up when its bus is there, or at the first command if the bus was reported later. The node can be given the
`friend_role` its clients ask for (`reset` for the reset line of a touch controller).

## The node

`read()` gives the pins of the group that are high, as `0x%02X`; `write()` takes a number (`0x01` or `1`): the pins of the group in it are
driven high, the others of the group low. The commands are those of `dmgpio` (`dmgpio_ioctl_cmd_toggle_pins`, `_set_pins_state`,
`_get_high_pins_state`, `_get_low_pins_state`), the interrupts are not supported (`-ENOTTY`): the interrupt line of the chip is not
handled. The changes of an input group return `-EPERM`.
