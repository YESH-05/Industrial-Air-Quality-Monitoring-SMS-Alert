# Working principle

1. MQ-2 and MQ-135 provide analog sensor outputs to ESP32 ADC inputs.
2. ESP32 samples the sensors periodically and reads temperature, humidity and pressure from BME280 over I2C.
3. The current readings are printed to Serial Monitor and displayed on the 16x2 I2C LCD.
4. If either raw gas-sensor reading crosses its configured threshold, the controller creates an alert message.
5. ESP32 connects to Wi-Fi and submits the message to the Twilio Messages API over HTTPS.
6. An alert latch and recovery hysteresis prevent repeated notifications while readings remain high. A cooldown limits repeated SMS attempts after a later new alert.

The example uses raw ADC thresholds, not ppm. Proper sensor warm-up, calibration and validation are required.
