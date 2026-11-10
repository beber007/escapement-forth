# I2C examples

Forth words for I2C sensors and a display, written with `@` and `!` on the chip's registers. The Grove Shield for Pi Pico has two I2C ports: I2C0 on GP8 (SDA) and GP9 (SCL), and I2C1 on GP6 and GP7. The bus runs at 100 kHz. Set the shield's switch to 3.3 V.

Paste `i2c.fs` into the web page's console, once. Then paste the other files. Their first lines name their port: `1 i2c-port constant bus` for I2C1. Change the number if the module is on the other port. `oled.fs` has no such line: it uses the port of `hub.fs`. Each of `sht4x.fs` and `adxl345.fs` takes a port for itself. `hub.fs` serves both sensors on one port.

| File | Sensor | Address |
|---|---|---|
| `i2c.fs` | Bus set-up, and `i2c-scan`, which prints the addresses that answer on a port: `0 i2c-scan` | |
| `sht4x.fs` | SHT40 or SHT41: temperature and humidity, once a second. Port I2C1 | `$44` |
| `adxl345.fs` | ADXL345: acceleration in mg, read every 10 ms, shown once a second. Port I2C0 | `$53` |
| `hub.fs` | The SHT4x, the ADXL345 and a display on one port, through a Grove I2C Hub. One word serves the bus, one exchange every millisecond. Port I2C1 | `$44`, `$53` |
| `oled.fs` | Grove OLED Display 0.96" (SSD1315), on the bus of `hub.fs`: the temperature, the humidity and the acceleration, 8 lines of 21 characters. Paste `hub.fs` first | `$3C` |

`slots` gives the words' numbers. `promote` then reserves them their share of the processor.

Tried on a Pico 2, with a SHT41 on I2C1 and an ADXL345 on I2C0. Both ran together, with no failed read.
Then all three on I2C1, through the hub. No failed exchange. The bus word took 89 µs at most, every millisecond, against a budget of 100 µs. Not yet tried on a Pico.
