# Circuit connections (verify against your actual prototype)

The following are proposed defaults used in the example sketch, not a verified record of the original wiring.

| Module | Module pin | ESP32 |
|---|---|---|
| MQ-2 | AO | GPIO 34 (through safe voltage conditioning) |
| MQ-135 | AO | GPIO 35 (through safe voltage conditioning) |
| BME280 | SDA | GPIO 21 |
| BME280 | SCL | GPIO 22 |
| I2C LCD | SDA | GPIO 21 |
| I2C LCD | SCL | GPIO 22 |
| I2C modules | VCC/GND | According to module specifications; common ground |

**Voltage warning:** ESP32 GPIO pins are not 5 V tolerant. Many MQ modules operate from 5 V and may output analog voltages above 3.3 V. Use a suitable voltage divider/level conditioning and verify with a multimeter before connecting AO to the ESP32.

BME280 and LCD share the I2C bus. LCD address is set to `0x27`; scan the bus if your module uses another address. BME280 address is tried as `0x76`, then `0x77`.
