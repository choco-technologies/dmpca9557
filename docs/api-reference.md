# dmpca9557 API

```c
#include "dmpca9557.h"
```

| Function | |
|---|---|
| `bool dmpca9557_validate_config(const dmpca9557_config_t *config)` | the address is 0x18..0x1F and the group has a pin |
| `uint8_t dmpca9557_output_value(uint8_t current, uint8_t pins, bool high)` | the output register after the pins are set to a level |
| `uint8_t dmpca9557_config_value(uint8_t current, uint8_t pins, bool output)` | the configuration register after the pins are made outputs (bit cleared) or inputs (bit set) |

The registers are in `dmpca9557_types.h` (`DMPCA9557_REG_INPUT`, `_OUTPUT`, `_POLARITY`, `_CONFIG`). The node is described in
[configuration.md](configuration.md).
