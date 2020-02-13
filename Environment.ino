#include <SparkFunBME280.h>
#include <SparkFunCCS811.h>
#include <WiFi.h>

#include "config.h"
#include "hardware.h"

const uint32_t READ_PERIOD_MS = 60000;
const uint32_t VALUE_NOT_SET = -1024;

uint32_t uploadTimeMs = 0;
uint8_t readCount = 0;
float firstTempC = VALUE_NOT_SET;
CCS811 myCCS811(CCS811_ADDR);
BME280 myBME280;

typedef struct
{
  float temperatureC;
  float humidityPct;
  float pressurePa;
  uint16_t co2PPM;
  uint16_t tvocPPB;
} environment_data_t;

void setup()
{
  pinMode(LED_PIN, OUTPUT);
  if (begin())
  {
    blink(1000, 3);
  }
  else
  {
    digitalWrite(LED_PIN, HIGH);
  }
}

void loop()
{
  if (measure())
  {
    digitalWrite(LED_PIN, LOW);
    delay(READ_PERIOD_MS - (millis() % READ_PERIOD_MS));
  }
  else
  {
    digitalWrite(LED_PIN, HIGH);
    delay(10);
  }
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
  if (myCCS811.dataAvailable() && !myBME280.isMeasuring())
  {
    environment_data_t env = readData();
    return connectToWiFi() && printData(env) && uploadData(env);
  }
  if (myCCS811.checkForStatusError())
  {
    Serial.print("Error: CCS811 error ");
    Serial.println(myCCS811.getErrorRegister());
  }
  return false;
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

environment_data_t readData()
{
  readCount++;
  myCCS811.readAlgorithmResults();
  environment_data_t env = {
      myBME280.readTempC(),
      myBME280.readFloatHumidity(),
      myBME280.readFloatPressure(),
      myCCS811.getCO2(),
      myCCS811.getTVOC(),
  };
  if (VALUE_NOT_SET == firstTempC)
  {
    firstTempC = env.temperatureC;
  }
  myCCS811.setEnvironmentalData(env.humidityPct, env.temperatureC);
  return env;
}

bool printData(environment_data_t env)
{
  Serial.printf(
      "T/H/P = %4.2f C (%4.2f C) / %3.2f %% / %7.2f hPa  CO2/TVOC = %4d ppm / %3d ppb  (last upload time %d ms)\n",
      env.temperatureC,
      firstTempC,
      env.humidityPct,
      env.pressurePa / 100,
      env.co2PPM,
      env.tvocPPB,
      uploadTimeMs);
  return true;
}

bool uploadData(environment_data_t env)
{
  if (readCount < 2)
  {
    return true;
  }

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

  uploadTimeMs = millis() - timeSt;
  return true;
}
