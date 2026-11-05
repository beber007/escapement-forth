# Sensor examples

Forth words for I2C sensors, written with `@` and `!` on the chip's registers. The Grove Shield for Pi Pico has two I2C ports: I2C0 on GP8 (SDA) and GP9 (SCL), and I2C1 on GP6 and GP7. The bus runs at 100 kHz. Set the shield's switch to 3.3 V.

Paste `i2c.fs` into the web page's console, once. Then paste the file of each sensor. Its first lines name its port: `1 i2c-port constant bus` for I2C1. Change the number if the sensor is on the other port. Both sensors can run together, one on each port.

| File | Sensor | Address |
|---|---|---|
| `i2c.fs` | Bus set-up, and `i2c-scan`, which prints the addresses that answer on a port: `0 i2c-scan` | |
| `sht4x.fs` | SHT40 or SHT41: temperature and humidity, once a second. Port I2C1 | `$44` |
| `adxl345.fs` | ADXL345: acceleration in mg, read every 10 ms, shown once a second. Port I2C0 | `$53` |

`slots` gives the words' numbers. `promote` then reserves them their share of the processor.

Tried on a Pico 2, with a SHT41 on I2C1 and an ADXL345 on I2C0. Both ran together, with no failed read. Not yet tried on a Pico.
