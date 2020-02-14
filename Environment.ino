#include <SparkFunBME280.h>
#include <SparkFunCCS811.h>
#include <WiFi.h>

#include "config.h"
#include "hardware.h"

const uint32_t READ_PERIOD_MS = 60000;
const uint32_t VALUE_NOT_SET = -1024;
const uint32_t UPLOAD_NONE = 0;
const uint32_t UPLOAD_FAILED = 1;
const uint32_t UPLOAD_MIN_SUCCESS = 10;

float firstTempC = VALUE_NOT_SET;
CCS811 myCCS811(CCS811_ADDR);
BME280 myBME280;

typedef struct
{
  uint32_t millis;
  float temperatureC;
  float humidityPct;
  float pressurePa;
  uint16_t co2PPM;
  uint16_t tvocPPB;
  uint32_t uploadTimeMs;
} environment_data_t;

void setup()
{
  pinMode(LED_PIN, OUTPUT);
  environment_data_t env = {};
  if (begin() && readData(env))
  {
    printData(env);
    blink(1000, 3);
  }
  else
  {
    digitalWrite(LED_PIN, HIGH);
  }
}

void loop()
{
  digitalWrite(LED_PIN, measure() ? LOW : HIGH);
  delay(READ_PERIOD_MS - (millis() % READ_PERIOD_MS));
}

bool begin()
{
  Serial.begin(115200);
  Serial.println("Initialising...");

  if (!Wire.begin())
  {
    Serial.println("Error: I2C failed to initialise");
    return false;
  }

  if (!myCCS811.begin())
  {
    Serial.println("Error: CCS811 failed to initialise");
    return false;
  }

  if (!myBME280.beginI2C())
  {
    Serial.println("Error: BME280 failed to initialise");
    return false;
  }
  myBME280.setTemperatureCorrection(TEMP_OFFSET_FROM_CCS811);

  Serial.println("Ready");
  return true;
}

bool measure()
{
  environment_data_t env = {};
  return readData(env) && connectToWiFi() && uploadData(env) && printData(env);
}

void blink(uint8_t sleep, uint8_t count)
{
  for (uint8_t i = 0; i < count; i++)
  {
    digitalWrite(LED_PIN, HIGH);
    delay(sleep);
    digitalWrite(LED_PIN, LOW);
    delay(sleep);
  }
}

bool connectToWiFi()
{
  if (WL_CONNECTED == WiFi.status())
  {
    return true;
  }

  uint8_t uploadWiFi = 0;
  while (uploadWiFi < UPLOAD_WI_FI_COUNT)
  {
    WiFi.disconnect(true, true);
    WiFi.begin(UPLOAD_WI_FI_NAMES[uploadWiFi], UPLOAD_WI_FI_PASSES[uploadWiFi]);
    if (WL_CONNECTED == WiFi.waitForConnectResult())
    {
      Serial.printf("Wi-Fi connected to %s as %s\n", UPLOAD_WI_FI_NAMES[uploadWiFi], WiFi.localIP().toString().c_str());
      return true;
    }
    uploadWiFi++;
  }
  Serial.println("Error: Wi-Fi failed to connect");
  return false;
}

bool readData(environment_data_t &env)
{
  uint8_t tries = 0;
  env.millis = millis();

  while (!myCCS811.dataAvailable() && ++tries <= 1000)
  {
    delay(1);
  }
  if (!myCCS811.dataAvailable())
  {
    if (myCCS811.checkForStatusError())
    {
      Serial.print("Error: CCS811 error ");
      Serial.println(myCCS811.getErrorRegister());
      return false;
    }
    Serial.println("Error: Timeout waiting for CCS811 data");
    return false;
  }
  myCCS811.readAlgorithmResults();
  env.temperatureC = myBME280.readTempC();
  env.humidityPct = myBME280.readFloatHumidity();
  env.pressurePa = myBME280.readFloatPressure();
  env.co2PPM = myCCS811.getCO2();
  env.tvocPPB = myCCS811.getTVOC();

  if (VALUE_NOT_SET == firstTempC)
  {
    firstTempC = env.temperatureC;
  }
  myCCS811.setEnvironmentalData(env.humidityPct, env.temperatureC);
  return true;
}

bool printData(environment_data_t &env)
{
  Serial.printf(
      "%04d:%02d:%02d.%03d  T/H/P = %4.2f C (%4.2f C) / %3.2f %% / %7.2f hPa  CO2/TVOC = %4d ppm / %3d ppb",
      env.millis / 3600000,
      env.millis / 60000 % 60,
      env.millis / 1000 % 60,
      env.millis % 1000,
      env.temperatureC,
      firstTempC,
      env.humidityPct,
      env.pressurePa / 100,
      env.co2PPM,
      env.tvocPPB);
  if (env.uploadTimeMs >= UPLOAD_MIN_SUCCESS)
  {
    Serial.printf("  (upload took %d ms)", env.uploadTimeMs);
  }
  else if (env.uploadTimeMs == UPLOAD_FAILED)
  {
    Serial.print("  (upload failed)");
  }
  Serial.println("");
  return true;
}

bool uploadData(environment_data_t &env)
{
  env.uploadTimeMs = UPLOAD_FAILED;
  uint32_t timeSt = millis();

  WiFiClient client;
  if (!client.connect("api.thingspeak.com", 80))
  {
    Serial.println("Error: could not connect to upload host/port");
    return false;
  }

  client.printf(
      "POST /update?api_key=%s&field1=%.2f&field2=%.2f&field3=%.2f&field4=%d&field5=%d HTTP/1.1\r\nHost: api.thingspeak.com\r\nConnection: close\r\n\r\n",
      UPLOAD_KEY,
      env.temperatureC,
      env.humidityPct,
      env.pressurePa / 100,
      env.co2PPM,
      env.tvocPPB);
  uint32_t timeout = millis() + 5000;
  while (0 == client.available())
  {
    if (timeout < millis())
    {
      client.stop();
      Serial.println("Error: read timeout");
      return false;
    }
  }

  while (client.available())
  {
    client.read();
  }

  client.stop();

  env.uploadTimeMs = millis() - timeSt;
  return true;
}
