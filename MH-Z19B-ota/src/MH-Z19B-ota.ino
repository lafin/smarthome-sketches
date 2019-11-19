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

byte askco2[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
byte max1k[9] = {0xFF, 0x01, 0x99, 0x00, 0x00, 0x00, 0x03, 0xE8, 0x7B};
byte max2k[9] = {0xFF, 0x01, 0x99, 0x00, 0x00, 0x00, 0x07, 0xD0, 0x8F};
byte max3k[9] = {0xFF, 0x01, 0x99, 0x00, 0x00, 0x00, 0x0B, 0xB8, 0xA3};
byte max5k[9] = {0xFF, 0x01, 0x99, 0x00, 0x00, 0x00, 0x13, 0x88, 0xCB};
byte disabled_ac[9] = {0xFF, 0x01, 0x79, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86};
byte enabled_ac[9] = {0xFF, 0x01, 0x79, 0xA0, 0x00, 0x00, 0x00, 0x00, 0xE6};
byte reset_zero[9] = {0xFF, 0x01, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78};
// checksum = (0xFF + (0xFF - [0xFF, 0x01, 0x79, 0xA0, 0x00, 0x00, 0x00, 0x00].slice(1).reduce((com, cur) => com += cur, 0)) + 2).toString(16)

String sensorId = "mh-z19b";
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

char* readSerialData()
{
  while (co2Serial.available() > 0 && (unsigned char)co2Serial.peek() != 0xFF)
  {
    co2Serial.read();
  }
  char response[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  memset(response, 0, 9);
  co2Serial.readBytes(response, 9);

  return response;
}

void beforeStart() {
  co2Serial.write(max2k, 9);
  delay(300);
  readSerialData();

  co2Serial.write(disabled_ac, 9);
  delay(300);
  readSerialData();
}

void resetZero() {
  co2Serial.write(reset_zero, 9);
  delay(300);
  readSerialData();
}

void requestData()
{
  co2Serial.write(askco2, 9);
  delay(300);
  char* response = readSerialData();

  if (response[0] != 0xFF)
  {
    Serial.println("\n\rWrong starting byte from co2 sensor!");
    return;
  }
  if (response[1] != 0x86)
  {
    Serial.println("\n\rWrong command from co2 sensor!");
    return;
  }

  int responseHigh = (int)response[2];
  int responseLow = (int)response[3];
  int ppm = (256 * responseHigh) + responseLow;
  int temp = (int)response[4] - 40;

  const int capacity = JSON_OBJECT_SIZE(6);
  StaticJsonDocument<capacity> doc;
  String clientId = getClientId();
  doc["clientId"] = clientId;
  doc["ppm"] = ppm;
  doc["temp"] = temp;
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
