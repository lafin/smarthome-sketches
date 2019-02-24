#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <SoftwareSerial.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

const char *ssid = "";
const char *password = "";
char *mqttServer = "192.168.0.88";

WiFiClient wifiClient;
PubSubClient client(wifiClient);

byte askco2[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
byte max1k[9] = {0xFF, 0x01, 0x99, 0x00, 0x00, 0x00, 0x03, 0xE8, 0x7B};
byte max2k[9] = {0xFF, 0x01, 0x99, 0x00, 0x00, 0x00, 0x07, 0xD0, 0x8F};
byte max3k[9] = {0xFF, 0x01, 0x99, 0x00, 0x00, 0x00, 0x0B, 0xB8, 0xA3};
byte max5k[9] = {0xFF, 0x01, 0x99, 0x00, 0x00, 0x00, 0x13, 0x88, 0xCB};
byte ac[9] = {0xFF, 0x01, 0x79, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86};

SoftwareSerial co2Serial(5, 4, false, 256);

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

  ArduinoOTA.setHostname("sensor-mh-z19b");
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

  co2Serial.begin(9600);
  delay(2000);
  co2Serial.write(max2k, 9);
  // disable auto calibration
  co2Serial.write(ac, 9);
}

void requestMHZ19B()
{
  char response[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  co2Serial.write(askco2, 9);
  delay(100);
  while (co2Serial.available() > 0 && (unsigned char)co2Serial.peek() != 0xFF)
  {
    co2Serial.read();
  }
  memset(response, 0, 9);
  co2Serial.readBytes(response, 9);
  if (response[0] != 0xFF)
  {
    Serial.println("\n\rWrong starting byte from co2 sensor!");
    Serial.println((int)response[0]);
    return;
  }
  if (response[1] != 0x86)
  {
    Serial.println("\n\rWrong command from co2 sensor!");
    Serial.println((int)response[1]);
    return;
  }

  int responseHigh = (int)response[2];
  int responseLow = (int)response[3];
  int ppm = (256 * responseHigh) + responseLow;
  int temp = (int)response[4] - 40;

  const int capacity = JSON_OBJECT_SIZE(2);
  StaticJsonBuffer<capacity> jb;
  JsonObject &obj = jb.createObject();
  obj.set("ppm", ppm);
  obj.set("temp", temp);

  String output = "";
  obj.printTo(output);

  client.publish("mh-z19b", output.c_str());
  Serial.println(output);
}

void reconnect()
{
  while (!client.connected())
  {
    String clientId = "mh-z19b";
    if (client.connect(clientId.c_str()))
    {
      Serial.println("connected");
    }
    else
    {
      Serial.print(".");
      delay(5000);
    }
  }
}

unsigned long previousTime = millis();
const unsigned long interval = 5 * 1000;

unsigned long startTime = millis();
const unsigned long intervalRestart = 24 * 60 * 60 * 1000;

void loop()
{
  ArduinoOTA.handle();

  if (!client.connected())
  {
    reconnect();
  }
  client.loop();

  if ((millis() - startTime) > intervalRestart)
  {
    ESP.restart();
  }

  unsigned long diff = millis() - previousTime;
  if (diff > interval)
  {
    requestMHZ19B();
    previousTime += diff;
  }
}
