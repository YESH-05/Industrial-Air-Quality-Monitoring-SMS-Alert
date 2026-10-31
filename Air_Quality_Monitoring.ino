/*
  Industrial Air Quality Monitoring and SMS Alert System
  Platform: ESP32 WROOM
  Sensors: MQ-2, MQ-135, BME280
  Display: 16x2 I2C LCD
  SMS: Twilio REST API over Wi-Fi

  IMPORTANT:
  - Pin assignments below are proposed defaults; verify against your actual wiring.
  - MQ sensor values are raw ADC readings, NOT calibrated ppm values.
  - Set thresholds only after observing/calibrating your own sensors.
  - For production, replace tlsClient.setInsecure() with certificate validation.
  - Do not commit real Wi-Fi/Twilio credentials to a public repository.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------- USER CONFIGURATION: fill locally, never publish real secrets ----------
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* TWILIO_ACCOUNT_SID = "YOUR_TWILIO_ACCOUNT_SID";
const char* TWILIO_AUTH_TOKEN  = "YOUR_TWILIO_AUTH_TOKEN";
const char* TWILIO_FROM_NUMBER = "+1XXXXXXXXXX"; // Twilio number, international format
const char* ALERT_TO_NUMBER    = "+91XXXXXXXXXX"; // destination number, international format

// ---------- PIN CONFIGURATION (verify with your circuit) ----------
constexpr int MQ2_PIN  = 34; // proposed ESP32 ADC1 input
constexpr int MQ135_PIN = 35; // proposed ESP32 ADC1 input
constexpr int I2C_SDA_PIN = 21;
constexpr int I2C_SCL_PIN = 22;

// ---------- SENSOR / ALERT CONFIGURATION ----------
constexpr int MQ2_THRESHOLD = 1800;   // example raw ADC threshold; calibrate experimentally
constexpr int MQ135_THRESHOLD = 1800; // example raw ADC threshold; calibrate experimentally
constexpr int MQ_HYSTERESIS = 150;    // readings must fall below threshold-hysteresis to re-arm
constexpr unsigned long SAMPLE_INTERVAL_MS = 2000;
constexpr unsigned long SMS_COOLDOWN_MS = 5UL * 60UL * 1000UL;

Adafruit_BME280 bme;
LiquidCrystal_I2C lcd(0x27, 16, 2);

bool bmeAvailable = false;
bool alertLatched = false;
unsigned long lastSampleTime = 0;
unsigned long lastSmsTime = 0;
bool smsSentBefore = false;

String urlEncode(const String& input) {
  const char hex[] = "0123456789ABCDEF";
  String encoded;
  for (size_t i = 0; i < input.length(); i++) {
    uint8_t c = static_cast<uint8_t>(input[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += static_cast<char>(c);
    } else if (c == ' ') {
      encoded += '+';
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

String base64Encode(const String& input) {
  const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String output;
  int val = 0, valb = -6;
  for (uint8_t c : input) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      output += table[(val >> valb) & 0x3F];
      valb -= 6;
    }
  }
  if (valb > -6) output += table[((val << 8) >> (valb + 8)) & 0x3F];
  while (output.length() % 4) output += '=';
  return output;
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected. IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("Wi-Fi connection failed.");
  return false;
}

bool sendTwilioSMS(const String& message) {
  if (!connectWiFi()) return false;

  WiFiClientSecure tlsClient;
  // Convenience setting for initial testing only. Use CA certificate validation
  // before deploying this system in a real industrial environment.
  tlsClient.setInsecure();

  HTTPClient https;
  String url = "https://api.twilio.com/2010-04-01/Accounts/";
  url += TWILIO_ACCOUNT_SID;
  url += "/Messages.json";

  if (!https.begin(tlsClient, url)) {
    Serial.println("Unable to start HTTPS request.");
    return false;
  }

  String credentials = String(TWILIO_ACCOUNT_SID) + ":" + TWILIO_AUTH_TOKEN;
  String authHeader = "Basic " + base64Encode(credentials);

  https.addHeader("Authorization", authHeader);
  https.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body = "From=" + urlEncode(TWILIO_FROM_NUMBER) +
                "&To=" + urlEncode(ALERT_TO_NUMBER) +
                "&Body=" + urlEncode(message);

  int httpCode = https.POST(body);
  String response = https.getString();

  Serial.print("Twilio HTTP status: ");
  Serial.println(httpCode);
  if (httpCode < 200 || httpCode >= 300) {
    Serial.println("Twilio response:");
    Serial.println(response);
  }

  https.end();
  return (httpCode >= 200 && httpCode < 300);
}

void showReadings(int mq2, int mq135, float temperature, float humidity) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("MQ2:");
  lcd.print(mq2);
  lcd.print(" MQ135:");
  lcd.print(mq135);

  lcd.setCursor(0, 1);
  if (bmeAvailable) {
    lcd.print(temperature, 1);
    lcd.print("C ");
    lcd.print(humidity, 0);
    lcd.print("% RH");
  } else {
    lcd.print("BME280 not found");
  }
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12); // ESP32 ADC range: 0-4095

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Air Quality Sys");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  bmeAvailable = bme.begin(0x76);
  if (!bmeAvailable) {
    bmeAvailable = bme.begin(0x77);
  }

  if (bmeAvailable) {
    Serial.println("BME280 detected.");
  } else {
    Serial.println("BME280 not detected. Check wiring/address.");
  }

  connectWiFi();
  delay(1500);
  lcd.clear();
}

void loop() {
  if (millis() - lastSampleTime < SAMPLE_INTERVAL_MS) return;
  lastSampleTime = millis();

  int mq2Value = analogRead(MQ2_PIN);
  int mq135Value = analogRead(MQ135_PIN);

  float temperature = NAN;
  float humidity = NAN;
  float pressure = NAN;

  if (bmeAvailable) {
    temperature = bme.readTemperature();
    humidity = bme.readHumidity();
    pressure = bme.readPressure() / 100.0F;
  }

  Serial.print("MQ-2 raw: ");
  Serial.print(mq2Value);
  Serial.print(" | MQ-135 raw: ");
  Serial.println(mq135Value);

  if (bmeAvailable) {
    Serial.print("Temperature: "); Serial.print(temperature);
    Serial.print(" C | Humidity: "); Serial.print(humidity);
    Serial.print(" % | Pressure: "); Serial.print(pressure);
    Serial.println(" hPa");
  }

  showReadings(mq2Value, mq135Value, temperature, humidity);

  bool gasAlert = (mq2Value >= MQ2_THRESHOLD) || (mq135Value >= MQ135_THRESHOLD);

  if (gasAlert && !alertLatched) {
    alertLatched = true;

    String message = "INDUSTRIAL AIR QUALITY ALERT! ";
    message += "MQ2 raw=" + String(mq2Value);
    message += ", MQ135 raw=" + String(mq135Value);

    if (bmeAvailable) {
      message += ", Temp=" + String(temperature, 1) + "C";
      message += ", Humidity=" + String(humidity, 1) + "%";
    }

    bool cooldownPassed = !smsSentBefore || (millis() - lastSmsTime >= SMS_COOLDOWN_MS);
    if (cooldownPassed) {
      Serial.println("Threshold exceeded. Sending SMS...");
      if (sendTwilioSMS(message)) {
        Serial.println("SMS request accepted by Twilio.");
        lastSmsTime = millis();
        smsSentBefore = true;
      } else {
        Serial.println("SMS failed. Check Wi-Fi, Twilio credentials and account settings.");
        // Keep alert latched to avoid repeatedly sending requests every sample.
      }
    } else {
      Serial.println("Alert detected, but SMS cooldown is active.");
    }
  }

  bool sensorsRecovered =
      (mq2Value < (MQ2_THRESHOLD - MQ_HYSTERESIS)) &&
      (mq135Value < (MQ135_THRESHOLD - MQ_HYSTERESIS));

  if (alertLatched && sensorsRecovered) {
    alertLatched = false;
    Serial.println("Readings returned below recovery thresholds. Alert re-armed.");
  }
}
