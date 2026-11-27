# dmpca9557

A driver of the 8-bit I/O expander **PCA9557** (NXP; the PCA9554 / TCA9534 family works the same) on I2C. The pins of the chip are shown as
GPIO pins: the node of a section is a group of pins and understands the commands of `dmgpio`, so whatever drives a pin of `dmgpio` (the
reset line of a touch controller, say) drives a pin behind the expander as well, without knowing it.

Made for the Elecrow DIS08070H (CrowPanel 7.0"), where the reset of the touch controller GT911 is wired to the expander (see `dmgt911`).

* [docs/configuration.md](docs/configuration.md) - the ini section, the friends
* [docs/api-reference.md](docs/api-reference.md) - the functions and the commands of the node

```ini
[touch_reset]
driver_name=dmpca9557
friends_group=touch
friend_role=reset
address=24          ; 0x18
pin=0               ; IO0
mode=output
state=high
```
