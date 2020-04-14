#include <ESP8266WiFi.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266httpUpdate.h>
#include <PubSubClient.h>

#include "securities.h"

#define fw_vers "ctlrel_0.0.0.2a"
// Firmware id  : ctlrel
// Firmware vers: 0.0.0.2a a=autoupdate

#define leaseDuration 6 // hour DHCP lease
unsigned int leaseDurationMillis;
unsigned int prevLeaseDurationMillis;

#define checkMqttClient 60 // ms interval for check mqtt messages
unsigned int checkMqttClientMillis;
unsigned int prevCheckMqttClientMillis;

#define sendStatus 2 // min interval for sending relay status
unsigned int sendStatusMillis;
unsigned int prevSendStatusMillis;

#define TESTtoggleRelay 3 // min interval for toggle relay status
unsigned int TESTtoggleRelayMillis;
unsigned int TESTprevToggleRelayMillis;

#define CMDON  "RELAYON"
#define CMDOFF "RELAYOFF"

char mqttClientId[200];
char mqttTopicOut[200];
char mqttTopicCmds[200];
#define mqttClientIdPrfx "ctlrel" // integrato con il chipid
#define mqttTopicAnnounce "announcement/clientid"
#define mqttTopicMedPrfx    "home/ctlrel/"  // integrato con il chipid

// #define delay1 300
#define delay2 1000
#define delay3 10
#define maxRetr 10
#define RELAY 0 // relay connected to  GPIO0

int retr=0;
ESP8266WiFiMulti wifiMulti;     // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'
WiFiClient espClient;
PubSubClient mqttClient;

int status; // stato del sistema
#define statusOK 0
#define statusErrNoNet 1
#define statusErrNoMq 2
#define statusErrDallas 3
#define statusErrGen 4


void checkOTAupdates() {
  String otaurl;
  
  otaurl = String(otaprotocol);
  otaurl = String(otaurl + "://");
  otaurl = String(otaurl + otahost);
  otaurl = String(otaurl + ":");
  otaurl = String(otaurl + otaport);
  otaurl = String(otaurl + otapath);
  Serial.print("Check for update OTA URL: ");
  Serial.println(otaurl);
  ESPhttpUpdate.rebootOnUpdate(false);
  t_httpUpdate_return ret = ESPhttpUpdate.update(espClient, otaurl, fw_vers); 
  /*upload information only */
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s",
                    ESPhttpUpdate.getLastError(),
                    ESPhttpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("HTTP_UPDATE_NO_UPDATES");
      break;
    case HTTP_UPDATE_OK:
      // actually this branch is never activated because the board restarts immediately after update
      Serial.println("HTTP_UPDATE_OK");
      Serial.println("Restarting ....");
      delay(5000);
      ESP.restart();
      break;
  }
} // checkOTAupdates

 
void setup() 
{
  status=statusErrGen;
  
  delay(5000);
  Serial.begin(115200); // must be same baudrate with the Serial Monitor
  delay(15000);
  
  strcpy(mqttClientId, mqttClientIdPrfx);
  strcat(mqttClientId, String(ESP.getChipId()).c_str());
  
  strcpy(mqttTopicOut, mqttTopicMedPrfx);
  strcat(mqttTopicOut, String(ESP.getChipId()).c_str());
  strcat(mqttTopicOut, "/out");
  
  strcpy(mqttTopicCmds, mqttTopicMedPrfx);
  strcat(mqttTopicCmds, String(ESP.getChipId()).c_str());
  strcat(mqttTopicCmds, "/cmds");
  
  pinMode(RELAY,OUTPUT);
  digitalWrite(RELAY, HIGH);
  
  Serial.println();
  Serial.println();
  Serial.print("ClientId: ");
  Serial.println(mqttClientId);
  /*
  Serial.print("TopicOut: ");
  Serial.println(mqttTopicOut);
  Serial.print("TopicCmds: ");
  Serial.println(mqttTopicCmds);
  */
  
  Serial.print("Versione: ");
  Serial.println(fw_vers);
  Serial.println("Connecting ");

  status=myconnect(); 
  Serial.print("Status: ");
  Serial.println(status);
  
  leaseDurationMillis=leaseDuration*60*60*1000;
  prevLeaseDurationMillis=millis();
  
  checkMqttClientMillis=checkMqttClient;
  prevCheckMqttClientMillis=millis();
  
  sendStatusMillis=sendStatus*60*1000;
  prevSendStatusMillis=millis();

  TESTtoggleRelayMillis=TESTtoggleRelay*60*1000;
  TESTprevToggleRelayMillis=millis();

  if (status <= statusOK) {
    Serial.printf("Use this topic for commands: %s%s\n", mqttTopicPrefix, mqttTopicCmds);
    Serial.printf("Use this topic for output  : %s%s\n", mqttTopicPrefix, mqttTopicOut);
    sendRelayStatus();
  }
} // setup


void loop() {
  if (status <= statusOK) {
    // Serial.println("Qui");
    if ((millis() - prevCheckMqttClientMillis) >= checkMqttClientMillis) {
      // mqttClient.loop serve a inviare e recuperare i messaggi 
      // se il tempo fra 2 loop è troppo lungo, si perdono i
      // i messaggi, soprattutto quelli in ingresso
      prevCheckMqttClientMillis=millis();
      // Serial.println("Qui2");
      mqttClient.loop();
    }
    // Serial.println("Qui3");
    if ((millis() - prevLeaseDurationMillis) >= leaseDurationMillis) {
      prevLeaseDurationMillis=millis();
      mydisconnect();
      ESP.restart();
    }
    // Serial.println("Qui4");
    if ((millis() - prevSendStatusMillis) >= sendStatusMillis) {
      prevSendStatusMillis=millis();
      sendRelayStatus();
    }
    // Serial.println("Qui5");
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("No Wifi!!");
      mydisconnect();
      ESP.restart();
    }
    // Serial.println("Qui6");
    if (!mqttClient.connected()) {
      Serial.println("No MQTT broker!!!");
      mydisconnect();
      ESP.restart();
    }
    // Serial.println("Qui7");
    /*
    if ((millis() - TESTprevToggleRelayMillis) >= TESTtoggleRelayMillis) {
      int relStat;

      Serial.println("Toggle Status");
      TESTprevToggleRelayMillis=millis();
      relStat=digitalRead(RELAY);
      if (relStat == HIGH) {
        digitalWrite(RELAY, LOW);
      }
      else {
        digitalWrite(RELAY, HIGH);
      }
      sendRelayStatus();
    }
    */
    delay(delay3);
  }
  else {
    // condizione di errore
    delay(5*delay2);
    Serial.print("Condizione di errore, stato: ");
    Serial.println(status);
    mydisconnect();
    ESP.restart();
  }
} // loop

 
int myconnect() {
  int retr;
  char textAnnounce[100];
  char topic[200];

  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(ssid1, password1);
  // wifiMulti.addAP(ssid2, password2);
  
  retr=0;
  while ((wifiMulti.run() != WL_CONNECTED) && (retr<maxRetr)){
    delay(500);
    Serial.print(".");
    retr++;
  }
  
  Serial.println("");
  Serial.print("WiFi connected: ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  delay(2000);
  checkOTAupdates();

  // if update found, checkOTAupdates() restarts the board
  mqttClient.setCallback(callback);
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setClient(espClient);
  Serial.print("Connecting to MQTT server ");
  Serial.print(mqttServer);
  Serial.print(" as clientid '");
  Serial.print(mqttClientId);
  Serial.print("' ");
  retr=0;
  while ((!mqttClient.connected()) && (retr<maxRetr)) {
     if(mqttClient.connect(mqttClientId, mqttUser, mqttPassword )) {
        Serial.println("connected");
     }
     else {
       Serial.print(".");
       delay(delay2*2);
     }
     retr++;
  }
  if (!mqttClient.connected()) {
     Serial.println("!!!Failed to connect to MQTT broker!!!");
     Serial.print("Failed state ");
     Serial.println(mqttClient.state());
     return statusErrNoMq;
  }
  sprintf(textAnnounce,"Hello, here %s", mqttClientId);
  mqttClient.publish(mqttTopicAnnounce,textAnnounce);
  strcpy(topic,mqttTopicPrefix);
  strcat(topic,mqttTopicCmds);
  Serial.print("Subscribing ");
  Serial.println(topic);
  mqttClient.subscribe(topic);
  return statusOK;
} // myconnect


void mydisconnect () {
  mqttClient.disconnect();
  delay(delay2);
  WiFi.disconnect();
} // mydisconnect


void analyzePayload(char *payload, unsigned int length) {
  int i;

  /*
  Serial.print("In analyze1: ");
  for(i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println(); 
  Serial.print("Lunghezza: ");
  Serial.println(length);
  Serial.printf("%s :%d\n", CMDON, strlen(CMDON));
  */
  if (strlen(CMDON) >= length) {
    if (strstr(payload, CMDON)) {
      // arrivata la richiesta
      digitalWrite(RELAY,LOW);
      Serial.println("Richiesta Accensione");
      sendRelayStatus();
    }
  }
  
  /*
  Serial.print("In analyze2: ");
  for(i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println(); 
  Serial.print("Lunghezza: ");
  Serial.println(length);
  Serial.printf("%s:%d\n", CMDOFF, strlen(CMDOFF));
  */
  if (strlen(CMDOFF) >= length) {
    if (strstr(payload, CMDOFF)) {
      // arrivata la richiesta
      digitalWrite(RELAY,HIGH);
      Serial.println("Richiesta Spegnimento");
      sendRelayStatus();
    }
  }
} // analyzePayload


void callback(char* topic, byte* payload, unsigned int length) {
  unsigned int i;
  char localPayload[200];
  
  Serial.print("Message received in topic: ");
  Serial.println(topic);
  Serial.print("Message: ");
  for(i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    localPayload[i]=(char)payload[i];
  }
  localPayload[i]=0;
  Serial.println();
  analyzePayload(localPayload, length);
  // delay(delay1);
  delay(delay3);
} // callback


void sendRelayStatus() {
  int relayStatus;
  char topic[200];
  char message[200];
  
  relayStatus=digitalRead(RELAY);
  strcpy(topic, mqttTopicPrefix);
  strcat(topic, mqttTopicOut);
  if (relayStatus == HIGH) {
    Serial.println("Relay: Off");
    strcpy(message, CMDOFF);
  }
  else {
    Serial.println("Relay: On");
    strcpy(message, CMDON);
  }
  mqttClient.publish(topic,message);
  Serial.println(topic);
  Serial.println(message);
} // sendRelayStatus
