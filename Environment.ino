#include <SparkFunBME280.h>
#include <SparkFunCCS811.h>
#include <WiFi.h>

#include "config.h"
#include "hardware.h"

const uint32_t READ_PERIOD_MS = 60000;
const uint32_t VALUE_NOT_SET = -1024;

uint8_t uploadWiFi = 0;
uint32_t uploadTimeMs = 0;
uint8_t readCount = 0;
float firstTempC = VALUE_NOT_SET;
float currentTempC = VALUE_NOT_SET;
float currentFloatPressure = VALUE_NOT_SET;
float currentFloatHumidity = VALUE_NOT_SET;
CCS811 myCCS811(CCS811_ADDR);
BME280 myBME280;

void setup()
{
  pinMode(LED_PIN, OUTPUT);
  blink(500, 1);
  Serial.begin(115200);
  Serial.println("Initialising...");

  if (!Wire.begin())
  {
    Serial.println("Error: I2C failed to initialise");
    digitalWrite(LED_PIN, HIGH);
  }

  if (!myCCS811.begin())
  {
    Serial.println("Error: CCS811 failed to initialise");
    digitalWrite(LED_PIN, HIGH);
  }

  if (!myBME280.beginI2C())
  {
    Serial.println("Error: BME280 failed to initialise");
    digitalWrite(LED_PIN, HIGH);
  }
  myBME280.setTemperatureCorrection(TEMP_OFFSET_FROM_CCS811);

  while (uploadWiFi < UPLOAD_WI_FI_COUNT)
  {
    WiFi.begin(UPLOAD_WI_FI_NAMES[uploadWiFi], UPLOAD_WI_FI_PASSES[uploadWiFi]);
    if (WL_CONNECTED == WiFi.waitForConnectResult())
    {
      break;
    }
    WiFi.disconnect(true, true);
    uploadWiFi++;
  }
  if (uploadWiFi >= UPLOAD_WI_FI_COUNT)
  {
    Serial.println("Error: Wi-Fi failed to initialise");
    digitalWrite(LED_PIN, HIGH);
  }
  else
  {
    Serial.printf("  Wi-Fi online (%s on %s)\n", WiFi.localIP().toString().c_str(), UPLOAD_WI_FI_NAMES[uploadWiFi]);
  }

  Serial.println("Ready");
  blink(200, 5);
}

void loop()
{
  if (myCCS811.dataAvailable() && !myBME280.isMeasuring())
  {
    readData();
    printData();
    if (readCount >= 2)
    {
      uploadData();
    }
    delay(READ_PERIOD_MS - (millis() % READ_PERIOD_MS));
  }
  else if (myCCS811.checkForStatusError())
  {
    Serial.print("Error: CCS811 error ");
    Serial.println(myCCS811.getErrorRegister());
    digitalWrite(LED_PIN, HIGH);
  }
  else
  {
    delay(10);
  }
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

void readData()
{
  readCount++;
  myCCS811.readAlgorithmResults();
  currentTempC = myBME280.readTempC();
  currentFloatPressure = myBME280.readFloatPressure();
  currentFloatHumidity = myBME280.readFloatHumidity();
  if (VALUE_NOT_SET == firstTempC)
  {
    firstTempC = currentTempC;
  }
  myCCS811.setEnvironmentalData(currentFloatHumidity, currentTempC);
}

void printData()
{
  Serial.printf(
      "T/H/P = %4.2f C (%4.2f C) / %3.2f %% / %7.2f hPa  CO2/TVOC = %4d ppm / %3d ppb  (last upload time %d ms)\n",
      currentTempC,
      firstTempC,
      currentFloatHumidity,
      currentFloatPressure / 100,
      myCCS811.getCO2(),
      myCCS811.getTVOC(),
      uploadTimeMs);
}

void uploadData()
{
  uint32_t timeSt = millis();

  WiFiClient client;
  if (!client.connect("api.thingspeak.com", 80))
  {
    Serial.println("Error: could not connect to upload host/port");
    digitalWrite(LED_PIN, HIGH);
    return;
  }

  client.printf(
      "POST /update?api_key=%s&field1=%.2f&field2=%.2f&field3=%.2f&field4=%d&field5=%d HTTP/1.1\r\nHost: api.thingspeak.com\r\nConnection: close\r\n\r\n",
      UPLOAD_KEY,
      currentTempC,
      currentFloatHumidity,
      currentFloatPressure / 100,
      myCCS811.getCO2(),
      myCCS811.getTVOC());
  uint32_t timeout = millis() + 5000;
  while (0 == client.available())
  {
    if (timeout < millis())
    {
      Serial.println("Error: read timeout");
      client.stop();
      digitalWrite(LED_PIN, HIGH);
      return;
    }
  }

  while (client.available())
  {
    client.read();
  }

  client.stop();

  uploadTimeMs = millis() - timeSt;
}
