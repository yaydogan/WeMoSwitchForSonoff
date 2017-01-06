#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <functional>
#include <Ticker.h>
#include <WiFiManager.h> 

void prepareIds();
void configureWifi();
boolean connectUDP();
void startHttpServer();
void turnOnRelay();
void turnOffRelay();
void buttonStateChange();
void toggleRelay();
void ledBlink();

const char* switchName = "Living Room Light";
const char* hostName = "LivingRoomSW";

unsigned int localPort = 1900;      // local port to listen on

WiFiUDP UDP;
boolean udpConnected = false;
IPAddress ipMulti(239, 255, 255, 250);
unsigned int portMulti = 1900;      // local port to listen on

ESP8266WebServer HTTP(80);
 
boolean wifiConnected = false;
boolean ledState = LOW;
boolean relayState;
Ticker  ticker;  // for LED status

char packetBuffer[UDP_TX_PACKET_MAX_SIZE]; //buffer to hold incoming packet,

String serial;
String persistent_uuid;
String device_name;
String accessPointName;


const int relayPin    = D6;
const int ledPin      = D7;
const int buttonPin   = D3;

const int BUTTON_WAIT   = 0;
const int BUTTON_CHANGE = 1;

int cmd = BUTTON_WAIT;

//inverted button state
int buttonState = HIGH;

static long startPress = 0;

void prepareIds() {
  uint32_t chipId = ESP.getChipId();
  char uuid[64];
  char apName[64];

  sprintf_P(uuid, PSTR("38323636-4558-4dda-9188-cda0e6%02x%02x%02x"),
        (uint16_t) ((chipId >> 16) & 0xff),
        (uint16_t) ((chipId >>  8) & 0xff),
        (uint16_t)   chipId        & 0xff);

  serial = String(uuid);
  persistent_uuid = "Socket-1_0-" + serial;
  device_name = switchName;

  sprintf_P(apName, PSTR("ESP-AP-%02x"), (uint16_t) chipId & 0xff);
  accessPointName = String(apName);
}

void respondToSearch() {
    Serial.println("");
    Serial.print("Sending response to ");
    Serial.println(UDP.remoteIP());
    Serial.print("Port : ");
    Serial.println(UDP.remotePort());

    IPAddress localIP = WiFi.localIP();
    char s[16];
    sprintf(s, "%d.%d.%d.%d", localIP[0], localIP[1], localIP[2], localIP[3]);

    String response = 
         "HTTP/1.1 200 OK\r\n"
         "CACHE-CONTROL: max-age=86400\r\n"
         "DATE: Fri, 15 Apr 2016 04:56:29 GMT\r\n"
         "EXT:\r\n"
         "LOCATION: http://" + String(s) + ":80/setup.xml\r\n"
         "OPT: \"http://schemas.upnp.org/upnp/1/0/\"; ns=01\r\n"
         "01-NLS: b9200ebb-736d-4b93-bf03-835149d13983\r\n"
         "SERVER: Unspecified, UPnP/1.0, Unspecified\r\n"
         "ST: urn:Belkin:device:**\r\n"
         "USN: uuid:" + persistent_uuid + "::urn:Belkin:device:**\r\n"
         "X-User-Agent: redsonic\r\n\r\n";

    UDP.beginPacket(UDP.remoteIP(), UDP.remotePort());
    UDP.write(response.c_str());
    UDP.endPacket();                    

    Serial.println("Response sent !");
}

void startHttpServer() {
    HTTP.on("/index.html", HTTP_GET, [](){
      Serial.println("Got Request index.html ...\n");
      HTTP.send(200, "text/plain", "Hello World!");
    });

    HTTP.on("/upnp/control/basicevent1", HTTP_POST, []() {
      Serial.println("########## Responding to  /upnp/control/basicevent1 ... ##########");      

      //for (int x=0; x <= HTTP.args(); x++) {
      //  Serial.println(HTTP.arg(x));
      //}
  
      String request = HTTP.arg(0);      
      Serial.print("request:");
      Serial.println(request);
 
      if(request.indexOf("<BinaryState>1</BinaryState>") > 0) {
          Serial.println("Turn on request from Alexa");
          turnOnRelay();
      }

      if(request.indexOf("<BinaryState>0</BinaryState>") > 0) {
          Serial.println("Turn off request from Alexa");
          turnOffRelay();
      }
      
      HTTP.send(200, "text/plain", "");
      
    });

    HTTP.on("/eventservice.xml", HTTP_GET, [](){
      Serial.println(" ########## Responding to eventservice.xml ... ########\n");
      String eventservice_xml = "<?scpd xmlns=\"urn:Belkin:service-1-0\"?>"
            "<actionList>"
              "<action>"
                "<name>SetBinaryState</name>"
                "<argumentList>"
                  "<argument>"
                    "<retval/>"
                    "<name>BinaryState</name>"
                    "<relatedStateVariable>BinaryState</relatedStateVariable>"
                    "<direction>in</direction>"
                  "</argument>"
                "</argumentList>"
                 "<serviceStateTable>"
                  "<stateVariable sendEvents=\"yes\">"
                    "<name>BinaryState</name>"
                    "<dataType>Boolean</dataType>"
                    "<defaultValue>0</defaultValue>"
                  "</stateVariable>"
                  "<stateVariable sendEvents=\"yes\">"
                    "<name>level</name>"
                    "<dataType>string</dataType>"
                    "<defaultValue>0</defaultValue>"
                  "</stateVariable>"
                "</serviceStateTable>"
              "</action>"
            "</scpd>\r\n"
            "\r\n";
            
      HTTP.send(200, "text/plain", eventservice_xml.c_str());
    });
    
    HTTP.on("/setup.xml", HTTP_GET, [](){
      Serial.println(" ########## Responding to setup.xml ... ########\n");

      IPAddress localIP = WiFi.localIP();
      char s[16];
      sprintf(s, "%d.%d.%d.%d", localIP[0], localIP[1], localIP[2], localIP[3]);
    
      String setup_xml = "<?xml version=\"1.0\"?>"
            "<root>"
             "<device>"
                "<deviceType>urn:Belkin:device:controllee:1</deviceType>"
                "<friendlyName>"+ device_name +"</friendlyName>"
                "<manufacturer>Belkin International Inc.</manufacturer>"
                "<modelName>Emulated Socket</modelName>"
                "<modelNumber>3.1415</modelNumber>"
                "<UDN>uuid:"+ persistent_uuid +"</UDN>"
                "<serialNumber>221517K0101769</serialNumber>"
                "<binaryState>0</binaryState>"
                "<serviceList>"
                  "<service>"
                      "<serviceType>urn:Belkin:service:basicevent:1</serviceType>"
                      "<serviceId>urn:Belkin:serviceId:basicevent1</serviceId>"
                      "<controlURL>/upnp/control/basicevent1</controlURL>"
                      "<eventSubURL>/upnp/event/basicevent1</eventSubURL>"
                      "<SCPDURL>/eventservice.xml</SCPDURL>"
                  "</service>"
              "</serviceList>" 
              "</device>"
            "</root>\r\n"
            "\r\n";
            
        HTTP.send(200, "text/xml", setup_xml.c_str());
        
        Serial.print("Sending :");
        Serial.println(setup_xml);
    });
    
    HTTP.begin();  
    Serial.println("HTTP Server started ..");
}

boolean connectUDP(){
  boolean state = false;
  
  Serial.println("");
  Serial.println("Connecting to UDP");
  
  if (UDP.beginMulticast(WiFi.localIP(), ipMulti, portMulti)) {
    Serial.println("Connection successful");
    state = true;
  }
  else {
    Serial.println("Connection failed");
  }
  
  return state;
}

void turnOnRelay() {
 relayState = HIGH;
 Serial.println("Relay ON");
 digitalWrite(relayPin, relayState); // turn on relay with voltage HIGH 
}

void turnOffRelay() {
  relayState = LOW;
  Serial.println("Relay OFF");
  digitalWrite(relayPin, relayState);  // turn off relay with voltage LOW
}

void buttonStateChange() {
  cmd = BUTTON_CHANGE;
}

void ledBlink()
{
  //toggle state
  int state = digitalRead(ledPin);  // get the current state of led pin
  digitalWrite(ledPin, !state);     // set pin to the opposite state
}

void toggleRelay() {
  Serial.println("Toggle relay");
  if (relayState == HIGH) {
    turnOffRelay();
  } else {
    turnOnRelay();
  }
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach_ms(100, ledBlink);
}

void configureWifi() {

  WiFiManager wifiManager;  

  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConfigPortalTimeout(180);

  Serial.println("Starting WiFi Manager");

  wifiConnected = wifiManager.autoConnect(accessPointName.c_str());

  if (wifiConnected) {
    // Stop blinking LED
    ticker.detach(); 
    digitalWrite(ledPin, LOW); 

    udpConnected = connectUDP();

    if (udpConnected) {
      startHttpServer();
    }
  } else {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }
}

void setup() {
  Serial.begin(115200);

  // Setup LED
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, ledState);
  ticker.attach_ms(400, ledBlink);

  // Prepare IDs
  prepareIds();

  // Setup Relay
  pinMode(relayPin, OUTPUT);
  turnOffRelay();

  // Setup  button
  pinMode(buttonPin, INPUT);
  attachInterrupt(buttonPin, buttonStateChange, CHANGE);
  
  // WIFI
  configureWifi();

}

void loop() {

  HTTP.handleClient();
  delay(1);
  
  
  // if there's data available, read a packet
  // check if the WiFi and UDP connections were successful
  if(wifiConnected){
    if(udpConnected){    
      // if there’s data available, read a packet
      int packetSize = UDP.parsePacket();
      
      if(packetSize) {
        Serial.println("");
        Serial.print("Received packet of size ");
        Serial.println(packetSize);
        Serial.print("From ");
        IPAddress remote = UDP.remoteIP();
        Serial.print(UDP.remoteIP());
        Serial.print(":");
        Serial.println(UDP.remotePort());
        
        int len = UDP.read(packetBuffer, 255);
        
        if (len > 0) {
            packetBuffer[len] = 0;
        }

        String request = packetBuffer;
        //Serial.println("Request:");
        //Serial.println(request);
         
        if(request.indexOf('M-SEARCH') > 0) {
            if(request.indexOf("urn:Belkin:device:**") > 0) {
                Serial.println("Responding to search request ...");
                respondToSearch();
            }
        }
      }
      delay(10);
    }
  } 

  switch (cmd) {
    case BUTTON_WAIT:
      break;
    case BUTTON_CHANGE:
      int currentState = digitalRead(buttonPin);
      if (currentState != buttonState) {
        if (buttonState == LOW && currentState == HIGH) {
          long duration = millis() - startPress;
          if (duration < 1000) {
            Serial.println("short press - toggle relay");
            toggleRelay();
          } else if (duration > 3000 & duration < 10000) {
            Serial.println("medium press - restart");
            ESP.restart();
          } else {
            Serial.println("long press - configure wifi");
            configureWifi();
          }
        } else if (buttonState == HIGH && currentState == LOW) {
          startPress = millis();
        }
        buttonState = currentState;
      }
      break;
  }
}
