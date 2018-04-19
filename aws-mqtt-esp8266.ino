#include <Arduino.h>
#include <Stream.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

//AWS
#include "sha256.h"
#include "Utils.h"

//WEBSockets
#include <Hash.h>
#include <WebSocketsClient.h>

//MQTT PAHO
#include <SPI.h>
#include <IPStack.h>
#include <Countdown.h>
#include <MQTTClient.h>


//AWS MQTT Websocket
#include "Client.h"
#include "AWSWebSocketClient.h"
#include "CircularByteBuffer.h"

//JSON
#include<ArduinoJson.h>

#include <MillisTimer.h>
#include "Credentials.h"
#include <LiquidCrystal_I2C.h>



//MQTT config
const int maxMQTTpackageSize = 512;
const int maxMQTTMessageHandlers = 1;

ESP8266WiFiMulti WiFiMulti;

AWSWebSocketClient awsWSclient(1000);

IPStack ipstack(awsWSclient);
MQTT::Client<IPStack, Countdown, maxMQTTpackageSize, maxMQTTMessageHandlers> *client = NULL;

StaticJsonBuffer<2000> jsonBuffer;

MillisTimer healthyTimer = MillisTimer(1000);
LiquidCrystal_I2C lcd(0x3F, 16, 2);
char device_ctrl_topic[100];

void setMacAddress() {
  uint8_t MAC_array[6];
  WiFi.macAddress(MAC_array);
  for (int i = 0; i < sizeof(MAC_array); ++i) {
    sprintf(MAC_char, "%s%02x:", MAC_char, MAC_array[i]);
  }
  Serial.println(MAC_char);
}

//generate random mqtt clientID
char* generateClientID () {
  char* cID = new char[23]();
  for (int i=0; i<22; i+=1)
    cID[i]=(char)random(1, 256);
  return cID;
}

//count messages arrived
int arrivedcount = 0;

//callback to handle mqtt messages
void messageArrived(MQTT::MessageData& md)
{
  MQTT::Message &message = md.message;

  Serial.print("Message ");
  Serial.print(++arrivedcount);
  Serial.print(" arrived: qos ");
  Serial.print(message.qos);
  Serial.print(", retained ");
  Serial.print(message.retained);
  Serial.print(", dup ");
  Serial.print(message.dup);
  Serial.print(", packetid ");
  Serial.println(message.id);
  Serial.print("Payload ");
  char* msg = new char[message.payloadlen+1]();
  memcpy (msg,message.payload,message.payloadlen);
  Serial.println(msg);
  JsonObject& root = jsonBuffer.parseObject(msg);
  if (root.success()) {
    const char* msg_payload = root["message"];
    lcd.clear();
    lcd.setCursor(0, 0); 
    lcd.print(msg_payload); 
  }
    
  delete msg;
}

//connects to websocket layer and mqtt layer
bool connect () {

    if (client == NULL) {
      client = new MQTT::Client<IPStack, Countdown, maxMQTTpackageSize, maxMQTTMessageHandlers>(ipstack);
    } else {

      if (client->isConnected ()) {    
        client->disconnect ();
      }  
      delete client;
      client = new MQTT::Client<IPStack, Countdown, maxMQTTpackageSize, maxMQTTMessageHandlers>(ipstack);
    }

    int rc = ipstack.connect(aws_endpoint, port);
    if (rc != 1)
    {
      Serial.println("error connection to the websocket server");
      return false;
    } else {
      Serial.println("websocket layer connected");
    }


    Serial.println("MQTT connecting");
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    char* clientID =  generateClientID();
    data.clientID.cstring = clientID;
    rc = client->connect(data);
    delete[] clientID;
    if (rc != 0)
    {
      Serial.print("error connection to MQTT server");
      Serial.println(rc);
      return false;
    }
    Serial.println("MQTT connected");
    return true;
}

//subscribe to a mqtt topic
void subscribe () {
   //subscript to a topic
    int rc = client->subscribe(device_ctrl_topic, MQTT::QOS0, messageArrived);
    if (rc != 0) {
      Serial.print("rc from MQTT subscribe is ");
      Serial.println(rc);
      return;
    }
    Serial.println("MQTT subscribed");
}

//send a message to a mqtt topic
void sendRegMessage () {
    //send a message
      MQTT::Message message;

     
     JsonObject& root = jsonBuffer.createObject();
     JsonObject& state=root.createNestedObject("state");
     JsonObject& reported=state.createNestedObject("reported");
     reported["deviceId"] = MAC_char;
     reported["deviceDesc"]="DEVICE DESCITPRION";
     reported["arnEndpoint"]=aws_endpoint;
     reported["topic"]=device_ctrl_topic;
     char buf[300]; 
     root.printTo((char*)buf, root.measureLength() + 1);
        
    message.qos = MQTT::QOS0;
    message.retained = false;
    message.dup = false;
    message.payload = (void*)buf;
    message.payloadlen = strlen(buf)+1;
    int rc = client->publish(aws_topic, message); 
    jsonBuffer.clear();
}

void sendDeviceHealthMsg(){
  Serial.println ("Sending DeviceHealth message.");
   MQTT::Message message;
   message.qos = MQTT::QOS0;
   message.retained = false;
   message.dup = false;
   message.payload = (void*)MAC_char;
   message.payloadlen = strlen(MAC_char)+1;
   int rc = client->publish(healthy_reports_topic, message); 
}

void healthyTimerExpiredHanlder(MillisTimer &mt){
   
  sendDeviceHealthMsg();
}


void setup() {
    Serial.begin (115200);
    delay (2000);
    Serial.setDebugOutput(1);
    
    lcd.begin(16,2);
    lcd.init();
    lcd.backlight();

    //fill with ssid and wifi password
    WiFiMulti.addAP(wifi_ssid, wifi_password);
    Serial.println ("connecting to wifi");
    lcd.setCursor(1, 0);      
    lcd.print("WiFi connecting");
    
    while(WiFiMulti.run() != WL_CONNECTED) {
        delay(100);
        Serial.print (".");
    }
    Serial.println ("\nconnected");
    lcd.clear();
    lcd.setCursor(1, 0); 
    lcd.print("Connected to Wifi !");
    
    setMacAddress();

    sprintf(device_ctrl_topic, "%s%s", device_ctrl_topic_prefix, MAC_char);
    
    //fill AWS parameters    
    awsWSclient.setAWSRegion(aws_region);
    awsWSclient.setAWSDomain(aws_endpoint);
    awsWSclient.setAWSKeyID(aws_key);
    awsWSclient.setAWSSecretKey(aws_secret);
    awsWSclient.setUseSSL(true);

    lcd.clear();
    lcd.setCursor(1, 0); 
    lcd.print("AWS connecting...");

    if (connect ()){
      subscribe ();
      sendRegMessage ();
    }

    lcd.clear();
    lcd.setCursor(1, 0); 
    lcd.print("AWS connected ! ");
    lcd.setCursor(1, 1);
    lcd.print("Waitng for message.. "); 

    healthyTimer.setInterval(1000 *60*10);
    healthyTimer.expiredHandler(healthyTimerExpiredHanlder);
    healthyTimer.start();

}

void loop() {
  //keep the mqtt up and running
  if (awsWSclient.connected ()) {    
      client->yield();
  } else {
    //handle reconnection
    if (connect ()){
      subscribe ();      
    }
  }
  healthyTimer.run();

}
