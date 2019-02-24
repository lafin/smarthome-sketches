// Flash mode: DOUT
// Crystal freq: 26
// Flash freq: 80
// CPU freq: 160

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

#define RELAYPIN 12
const char *ssid = "";
const char *password = "";
char *mqttServer = "192.168.0.88";

WiFiClient wifiClient;
PubSubClient client(wifiClient);

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

void sendStatus(bool on)
{
  const int capacity = JSON_OBJECT_SIZE(2);
  StaticJsonBuffer<capacity> jb;
  JsonObject &obj = jb.createObject();

  String clientId = getClientId();
  obj.set("clientId", clientId.c_str());
  obj.set("on", on);

  String output = "";
  obj.printTo(output);
  Serial.println(output);

  client.publish("outTopic", output.c_str());
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

  ArduinoOTA.setHostname("socket");
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

  pinMode(RELAYPIN, OUTPUT);
  digitalWrite(RELAYPIN, LOW);
}

void reconnect()
{
  while (!client.connected())
  {
    String clientId = getClientId();
    if (client.connect(clientId.c_str()))
    {
      Serial.println("connected");
      client.subscribe("inTopic");
      sendStatus(false);
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
    StaticJsonBuffer<capacity> jb;
    JsonObject &root = jb.parseObject(payload);
    if (!root.success())
    {
      Serial.println("parseObject() failed");
      return;
    }

    String clientId = getClientId();
    const char *receivedClientId = root["clientId"];
    if (strcmp(receivedClientId, clientId.c_str()) == 0)
    {
      bool on = root["on"];
      digitalWrite(RELAYPIN, on ? HIGH : LOW);
    }
  }
}

void loop()
{
  ArduinoOTA.handle();

  if (!client.connected())
  {
    reconnect();
  }
  client.loop();
}
