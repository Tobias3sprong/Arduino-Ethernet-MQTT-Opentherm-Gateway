#include <Arduino.h>
#include <Ethernet2.h>
#include <PubSubClient.h>
#include <OpenTherm.h>


const int inPin = 2; //4
const int outPin = 3; //5
OpenTherm ot(inPin, outPin);

void handleInterrupt() {
  ot.handleInterrupt();
}

bool enableCentralHeating = false;

float setpoint = 0;

// Network defaults
char mqtt_server[40]    = "192.168.1.65";   // Public broker
char mqtt_port[6]       = "1883";
char device_name[40]    = "Opentherm";

// Global
long lastReconnectAttempt = 0;
long lastStatusSend = 0;
long lastCom = 0;
long lastCallback = 0;

// Enter a MAC address for your controller below.
// SmartCrumbs uses WIZnet W5500 chip so we recommend using their MAC pool 00:08:DC:??:??:??
byte mac[] = { 0x00, 0x08, 0xDC, 0x00, 0x40, 0x01 };

// Initialize the Ethernet client library
EthernetClient ethClient;
PubSubClient client(ethClient);

unsigned long response = ot.setBoilerStatus(enableCentralHeating);

void mqttConnected() {
  Serial.println("Connected to MQTT");
  client.subscribe("Opentherm/settings/setpoint");
  client.subscribe("Opentherm/settings/heating");
}

boolean reconnect() {
  Serial.println("Trying to (re)connect to MQTT");
  // MQTT reconnection function

  // Create a random client ID
  String clientId = "OTGW-";
  clientId += String(random(0xff), HEX) + ":" + String(random(0xff), HEX) + ":" + String(random(0xff), HEX) + ":" + String(random(0xff), HEX) + ":" + String(random(0xff), HEX) + ":" + String(random(0xff), HEX);
  // Attempt to connect
  if ( client.connect(clientId.c_str(), "Opentherm/mqttStatus", 0, false, "disconnected") ) {
    mqttConnected();
  }


  return client.connected();

}

void callback(char* topic, byte* payload, unsigned int length) {

  char c_payload[length];
  memcpy(c_payload, payload, length);
  c_payload[length] = '\0';

  String s_topic = String(topic);         // Topic
  String s_payload = String(c_payload);   // Message content

  // Handling incoming MQTT messages

  if ( s_topic == "Opentherm/settings/setpoint" ) {
    setpoint = s_payload.toFloat();
    lastCallback = millis();
  } else if ( s_topic == "Opentherm/settings/heating" ) {
    lastCallback = millis();
    if (s_payload.toInt() == 1) {
      enableCentralHeating = true;
    } else {
      enableCentralHeating = false;
    }
  }
  ot.setBoilerTemperature(setpoint);
  ot.setBoilerStatus(enableCentralHeating);
  Opentherm();
}

void setup() {
  Serial.begin(9600);
  // start the Ethernet connection:
  Ethernet.begin(mac);
  // give the Ethernet shield a second to initialize:
  delay(1000);

  client.setServer((char*)mqtt_server, atoi(&mqtt_port[0]));
  client.setCallback(callback);

  ot.begin(handleInterrupt);

}

void Opentherm() {
  //Get Boiler Temperature
  unsigned int dataRelModLevel = 0xFFFF;
  unsigned int dataCHPressure = 0xFFFF;
  response = ot.setBoilerStatus(enableCentralHeating);
  String messageJson = "{\"isFlameOn\": " + String(ot.isFlameOn(response)) + ",\"isCentralHeatingEnabled\": " + String(ot.isCentralHeatingEnabled(response)) + ",\"isFault\": \"" + String(ot.isFault(response)) + "\", \"controlSetpoint\": " + String(setpoint) + ", \"CHPressure\": " + String((ot.sendRequest(ot.buildRequest(OpenThermRequestType::READ, OpenThermMessageID::CHPressure, dataCHPressure)) & 0xFFFF) / 256.0) + ", \"relModLevel\": " + String((ot.sendRequest(ot.buildRequest(OpenThermRequestType::READ, OpenThermMessageID::RelModLevel, dataRelModLevel)) & 0xFFFF) / 256.0) + ", \"boilerTemperature\": " + String(ot.getBoilerTemperature()) + "}";
  Serial.println(messageJson);
  Serial.print(client.publish("Opentherm/status", messageJson.c_str()));
}


void loop() {

  if (!client.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    // Client connected
    client.loop();
    long now = millis();
    if (now - lastStatusSend > 30000) {
      lastStatusSend = now;
      Opentherm();
    }
    if (now - lastCom > 500) {
      lastCom = now;
      ot.setBoilerTemperature(setpoint);
      response = ot.setBoilerStatus(enableCentralHeating);
      Serial.print("Setpoint: ");
      Serial.print(setpoint);
      Serial.print(" CH: ");
      Serial.println(ot.isCentralHeatingEnabled(response));
    }

    if (now - lastCallback > 60000) {
      //Reset opentherm if no message are received for 1 minute
      //TODO: Send error message
      setpoint = 0;
      enableCentralHeating = false;
    }
  }
}
