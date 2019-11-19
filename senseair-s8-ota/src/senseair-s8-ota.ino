#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <SoftwareSerial.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

#include "secret.h"
// #ifndef HEADER_FILE
// #define HEADER_FILE

// const char *ssid = "ssid";
// const char *password = "password";
// char *mqttServer = "mqttServer";

// #endif

WiFiClient wifiClient;
PubSubClient client(wifiClient);

const byte askco2[8] = {0xfe, 0x04, 0x00, 0x03, 0x00, 0x01, 0xd5, 0xc5};
const byte fwver[8] = {0xfe, 0x04, 0x00, 0x1c, 0x00, 0x01, 0xe4, 0x03};
const byte id_hi[8] = {0xfe, 0x04, 0x00, 0x1d, 0x00, 0x01, 0xb5, 0xc3};
const byte id_lo[8] = {0xfe, 0x04, 0x00, 0x1e, 0x00, 0x01, 0x45, 0xc3};

const byte clear_reg[8] = {0xfe, 0x06, 0x00, 0x00, 0x00, 0x00, 0x9d, 0xc5};
const byte reset_zero[8] = {0xfe, 0x06, 0x00, 0x01, 0x7c, 0x06, 0x6c, 0xc7};
const byte read_reg[8] = {0xfe, 0x03, 0x00, 0x00, 0x00, 0x01, 0x90, 0x05};
const byte disabled_ac[8] = {0xfe, 0x06, 0x00, 0x1f, 0x00, 0x00, 0xac, 0x03};

String sensorId = "senseair-s8";
SoftwareSerial co2Serial(5, 4, false, 256);

String getClientId()
{
  uint8_t mac[6];
  WiFi.macAddress(mac);

  String clientId = "";
  for (int i = 0; i < 6; ++i)
  {
    clientId += String(mac[i], 16);
  }

  return clientId;
}

int calcCrc(char* response, int len) {
  int crc = 0xFFFF;

  for (int pos = 0; pos < len; pos++) {
    crc ^= (int)response[pos];

    for (int i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }

  return crc;
}

void checkCrc(char* response) {
  int crc = calcCrc(response, 5);
  int got = (int)response[5] + (int)response[6] * 256;
  if (crc != got) {
    Serial.print("Invalid checksum.");
    return;
  }
}

char* readSerialData()
{
  while (co2Serial.available() > 0 && (unsigned char)co2Serial.peek() != 0xFF)
  {
    co2Serial.read();
  }
  char response[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  memset(response, 0, 9);
  co2Serial.readBytes(response, 9);
  checkCrc(response);
  delay(100);

  return response;
}

void beforeStart() {
  char* response;

  Serial.print("Sensor ID: ");
  co2Serial.write(id_hi, 8);
  response = readSerialData();
  Serial.printf("%02x%02x", response[3], response[4]);
  co2Serial.write(id_lo, 8);
  response = readSerialData();
  Serial.printf("%02x%02x", response[3], response[4]);
  Serial.println();

  co2Serial.write(fwver, 8);
  response = readSerialData();
  Serial.printf("Firmware: %d.%d", response[3], response[4]);
  Serial.println();

  co2Serial.write(disabled_ac, 8);
  readSerialData();
}

void resetZero() {
  co2Serial.write(clear_reg, 8);

  readSerialData();
  co2Serial.write(reset_zero, 8);

  readSerialData();
  delay(2000);
  co2Serial.write(read_reg, 8);

  readSerialData();
}

void requestData()
{
  co2Serial.write(askco2, 8);

  char* response = readSerialData();

  int responseHigh = (int)response[3];
  int responseLow = (int)response[4];
  int ppm = (256 * responseHigh) + responseLow;

  const int capacity = JSON_OBJECT_SIZE(5);
  StaticJsonDocument<capacity> doc;
  String clientId = getClientId();
  doc["clientId"] = clientId;
  doc["ppm"] = ppm;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["maxFreeBlockSize"] = ESP.getMaxFreeBlockSize();

  String output = "";
  serializeJson(doc, output);

  client.publish(sensorId.c_str(), output.c_str());
  Serial.println(output);
}

void reconnect()
{
  while (!client.connected())
  {
    if (client.connect(sensorId.c_str()))
    {
      Serial.println("connected");
      client.subscribe("inTopic");
    }
    else
    {
      Serial.print(".");
      delay(5000);
    }
  }
}

void callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (strcmp(topic, "inTopic") == 0)
  {
    const int capacity = JSON_OBJECT_SIZE(2);
    StaticJsonDocument<capacity> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error)
    {
      Serial.println("parseObject() failed");
      return;
    }

    String clientId = getClientId();
    const char *receivedClientId = doc["clientId"];
    if (strcmp(receivedClientId, clientId.c_str()) == 0)
    {
      const char *command = doc["command"];
      if (strcmp(command, "reset_zero") == 0)
      {
        resetZero();
      }
      else if (strcmp(command, "restart") == 0)
      {
        ESP.restart();
      }
    }
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  String hostname = "sensor-" + sensorId;
  ArduinoOTA.setHostname(hostname.c_str());
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
    {
      type = "sketch";
    }
    else
    { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
    {
      Serial.println("Auth Failed");
    }
    else if (error == OTA_BEGIN_ERROR)
    {
      Serial.println("Begin Failed");
    }
    else if (error == OTA_CONNECT_ERROR)
    {
      Serial.println("Connect Failed");
    }
    else if (error == OTA_RECEIVE_ERROR)
    {
      Serial.println("Receive Failed");
    }
    else if (error == OTA_END_ERROR)
    {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  client.setServer(mqttServer, 1883);
  client.setCallback(callback);

  co2Serial.begin(9600);
  delay(5000);

  beforeStart();
}

unsigned long previousTime = millis();
const unsigned long interval = 10 * 1000;

unsigned long startTime = millis();
const unsigned long intervalRestart = 24 * 60 * 60 * 1000;

void loop()
{
  ArduinoOTA.handle();
  if (!client.connected())
  {
    reconnect();
  }

  if ((millis() - startTime) > intervalRestart)
  {
    ESP.restart();
  }

  unsigned long diff = millis() - previousTime;
  if (diff > interval)
  {
    requestData();
    previousTime += diff;
  }

  client.loop();
}