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
  uint8_t uploadTries;
  uint32_t uploadTimeMs;
} environment_data_t;

void setup()
{
  pinMode(LED_PIN, OUTPUT);
  environment_data_t env = {};
  if (begin() && readData(env) && reconnectWiFi())
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
  log_d("Initialising...");

  if (!Wire.begin())
  {
    log_e("Error: I2C failed to initialise");
    return false;
  }

  if (!myCCS811.begin())
  {
    log_e("Error: CCS811 failed to initialise");
    return false;
  }
  myCCS811.setDriveMode(1);

  if (!myBME280.beginI2C())
  {
    log_e("Error: BME280 failed to initialise");
    return false;
  }
  myBME280.setFilter(0);
  myBME280.setTemperatureCorrection(TEMP_OFFSET_FROM_CCS811);

  log_d("Ready");
  return true;
}

bool measure()
{
  environment_data_t env = {};

  if (!readData(env))
  {
    return false;
  }

  while (!uploadData(env))
  {
    if (env.uploadTries >= 5 || !reconnectWiFi())
    {
      return false;
    }
  }

  return printData(env);
}

void blink(uint8_t sleep, uint8_t count)
{
  for (auto i = 0; i < count; i++)
  {
    digitalWrite(LED_PIN, HIGH);
    delay(sleep);
    digitalWrite(LED_PIN, LOW);
    delay(sleep);
  }
}

bool reconnectWiFi()
{
  auto uploadWiFi = 0;
  while (uploadWiFi < UPLOAD_WI_FI_COUNT)
  {
    WiFi.disconnect(true, true);
    WiFi.begin(UPLOAD_WI_FI_NAMES[uploadWiFi], UPLOAD_WI_FI_PASSES[uploadWiFi]);
    if (WL_CONNECTED == WiFi.waitForConnectResult())
    {
      log_i("Wi-Fi connected to %s as %s", UPLOAD_WI_FI_NAMES[uploadWiFi], WiFi.localIP().toString().c_str());
      return true;
    }
    uploadWiFi++;
  }
  log_e("Error: Wi-Fi failed to connect");
  return false;
}

bool readData(environment_data_t &env)
{
  auto tries = 0;
  while (!myCCS811.dataAvailable() && ++tries <= 1000)
  {
    delay(1);
  }
  if (!myCCS811.dataAvailable())
  {
    if (myCCS811.checkForStatusError())
    {
      log_e("Error: CCS811 error %d", myCCS811.getErrorRegister());
      return false;
    }
    log_e("Error: Timeout waiting for CCS811 data");
    return false;
  }

  env.millis = millis();
  myCCS811.readAlgorithmResults();
  env.temperatureC = myBME280.readTempC();
  env.humidityPct = myBME280.readFloatHumidity();
  env.pressurePa = myBME280.readFloatPressure();
  env.co2PPM = myCCS811.getCO2();
  env.tvocPPB = myCCS811.getTVOC();
  myCCS811.setEnvironmentalData(env.humidityPct, env.temperatureC);

  return true;
}

bool printData(environment_data_t &env)
{
  char msg[128] = {};
  auto ch = sprintf(
      msg,
      "[%04d:%02d] %4.2f C|%3.2f %%|%7.2f hPa|%4d ppm|%3d ppb",
      env.millis / 3600000,
      env.millis / 60000 % 60,
      env.temperatureC,
      env.humidityPct,
      env.pressurePa / 100,
      env.co2PPM,
      env.tvocPPB);
  if (env.uploadTimeMs >= UPLOAD_MIN_SUCCESS)
  {
    ch += sprintf(msg + ch, "|%d ms (try %d)", env.uploadTimeMs, env.uploadTries);
  }
  else if (env.uploadTimeMs == UPLOAD_FAILED)
  {
    ch += sprintf(msg + ch, "|failed");
  }
  log_i("%s", msg);
  return true;
}

bool uploadData(environment_data_t &env)
{
  env.uploadTries++;
  env.uploadTimeMs = UPLOAD_FAILED;
  uint32_t timeSt = millis();

  WiFiClient client;
  if (!client.connect("api.thingspeak.com", 80))
  {
    log_e("Error: could not connect to upload host/port");
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
      log_e("Error: read timeout");
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
