#include <Arduino.h>
#include <ArduinoJson.h>
#ifdef ESP32
#include <WiFi.h>
#include <AsyncTCP.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>
#include <EEPROM.h>


#define LOW_LEVEL_TRIGGERED_RELAYS true
#define ALL_RELAY_PINS_INDEX -1

const char* ssid     = "your wifi ssid";
const char* password = "your wifi password";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

struct MY_RELAY_PINS
{
  String  relayPinName;
  int     relayPinNumber;
  int     relayStatus;  
  String  buttonDescription;
  int     inputSwitchNumber;
  int     inputSwitchStatus;  
};
//Set up your relay pins here 
std::vector<MY_RELAY_PINS> myRelayPins = 
{
  {"Relay1",  4, 0, "Bulb", 27, 0},
  {"Relay2", 16, 0, "Lamp", 26, 0},
  {"Relay3", 17, 0, "Christmas Light", 25, 0},
  {"Relay4", 18, 0, "Blue Light", 33, 0},    
};

const char* myInitialPage PROGMEM = R"HTMLHOMEPAGE(
<html>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <head>
    <style>
      body 
      {
        max-width: max-content;
        margin: auto;
        background-color: powderblue;
      }
      th
      {
        width:300px;font-size:30;height:70px;font-family:arial;
      }
      input[type=button]
      {
        background-color:red;color:white;border-radius:30px;width:100px;height:40px;font-size:20;
      }
    </style>
    <h2 style="color: teal;text-align:center;">Welcome To Home Automation</h2>
    <h3  style="color: teal;text-align:center;">Using Local Async Webserver - Websocket</h3>
  </head>

  <body>

    %ALL_BUTTONS_PLACEHOLDER%

    <script>
      var webSocketUrl = "ws:\/\/" + window.location.hostname + "/ws";
      var websocket;
      
      function initWebSocket() 
      {
        websocket = new WebSocket(webSocketUrl);
        websocket.onopen    = function(event){};
        websocket.onclose   = function(event){setTimeout(initWebSocket, 2000);};
        websocket.onmessage = function(event) 
        {
          var relayArray = JSON.parse(event.data);
          for (var i = 0; i < relayArray.length ; i++)
          {
            var myButton = document.getElementById(relayArray[i].relayPinName);
            myButton.value = (relayArray[i].relayStatus ? "ON" : "OFF");
            myButton.style.backgroundColor = (myButton.value == "ON" ? "green" : "red");
          }
        };
      }

      function onClickSetRelayPinStatus(button) 
      {
        var value = (button.value == "ON") ? 0 : 1 ;
        var msg = 
        {
          relayPinName : button.id,
          relayStatus : value,
        };
        websocket.send(JSON.stringify(msg));
      }
          
      window.onload = initWebSocket;
    </script>
  </body>
</html>       
)HTMLHOMEPAGE";

//This function replaces %ALL_BUTTONS_PLACEHOLDER% and creates buttons from relay pins declared above
String processor(const String& var)
{
  String htmlHomePageButtons = "";
  if(var == "ALL_BUTTONS_PLACEHOLDER")
  {
    htmlHomePageButtons += "<table>";
    
    for (int i = 0; i < myRelayPins.size(); i++)
    {
      htmlHomePageButtons += "<tr>";
      htmlHomePageButtons += "<th>" + myRelayPins[i].buttonDescription + "</th>  <td><input type='button' id='" + myRelayPins[i].relayPinName  + "' value='OFF' onclick='onClickSetRelayPinStatus(this)'></td>";
      htmlHomePageButtons += "</tr>";
    }
    htmlHomePageButtons += "</table>";
  }
  return htmlHomePageButtons;
}

//Create relay status like :  [{"relayPinName":"Relay1", "relayStatus":1}, {"relayPinName":"Relay2", "relayStatus":0}]
//if relayPinArrayIndex is -1 then get all relay pins status. Else just get the values for relayPinArrayIndex relay
String getRelayPinsStatusJson(int relayPinArrayIndex)
{
  int loopStartIndex = (relayPinArrayIndex == ALL_RELAY_PINS_INDEX ? 0 : relayPinArrayIndex) ;
  int loopSize = (relayPinArrayIndex == ALL_RELAY_PINS_INDEX ? myRelayPins.size() : relayPinArrayIndex+1) ; 
  
  String delimeter, outputString;
  outputString = "[";
  for (int i = loopStartIndex; i < loopSize; i++)
  {
    String relayStatusString = "{\"relayPinName\":\"" + myRelayPins[i].relayPinName + "\", \"relayStatus\":" + myRelayPins[i].relayStatus + "}" ;
    delimeter = (i == loopSize - 1) ? "" : ",";
    outputString += relayStatusString + delimeter;                
  } 
  outputString += "]";
  return outputString;
}

void updateRelay(int i)
{
  EEPROM.write(i, byte(myRelayPins[i].relayStatus));
  EEPROM.commit();       
  digitalWrite(myRelayPins[i].relayPinNumber, (LOW_LEVEL_TRIGGERED_RELAYS ? !(myRelayPins[i].relayStatus) :  myRelayPins[i].relayStatus));     
}

void handleRoot(AsyncWebServerRequest *request) 
{
  request->send_P(200, "text/html", myInitialPage, processor);
}

void handleNotFound(AsyncWebServerRequest *request) 
{
    request->send(404, "text/plain", "File Not Found");
}


void onWebSocketEvent(AsyncWebSocket *server, 
                      AsyncWebSocketClient *client, 
                      AwsEventType type,
                      void *arg, 
                      uint8_t *data, 
                      size_t len) 
{                      
  switch (type) 
  {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      client->text(getRelayPinsStatusJson(ALL_RELAY_PINS_INDEX));
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      AwsFrameInfo *info;
      info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) 
      {
        // Deserialize the JSON document
        StaticJsonDocument<200> doc;
        DeserializationError error = deserializeJson(doc, data);
        if (error) 
        {
          Serial.print(F("deserializeJson() failed: "));
          Serial.println(error.f_str());
          return;
        }
        String relayPinName = doc["relayPinName"];
        for (int i = 0; i < myRelayPins.size(); i++)
        {
          if (myRelayPins[i].relayPinName == relayPinName)
          {
            myRelayPins[i].relayStatus = doc["relayStatus"]; 
            updateRelay(i);
            ws.textAll(getRelayPinsStatusJson(i));
          }
        }        
      }
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
    default:
      break;  
  }
}

void setUpPinModes()
{
  EEPROM.begin(myRelayPins.size());
   
  for (int i = 0; i < myRelayPins.size(); i++)
  {
    myRelayPins[i].relayStatus = (EEPROM.read(i) == 1 ? 1 : 0);
    pinMode(myRelayPins[i].relayPinNumber, OUTPUT); 
    updateRelay(i);

    pinMode(myRelayPins[i].inputSwitchNumber, INPUT_PULLUP); 
    myRelayPins[i].inputSwitchStatus = digitalRead(myRelayPins[i].inputSwitchNumber);       
  }
}


void readSwitchesAndSetRelayStatus()
{
  for (int i = 0; i < myRelayPins.size(); i++)
  {
    int switchStatus = digitalRead(myRelayPins[i].inputSwitchNumber);  
    //If switch status changed, then toggle the relay status and send notification to all
    if (switchStatus != myRelayPins[i].inputSwitchStatus)
    {
      myRelayPins[i].inputSwitchStatus = switchStatus;  
      myRelayPins[i].relayStatus = !(myRelayPins[i].relayStatus) ;
      updateRelay(i);    
      ws.textAll(getRelayPinsStatusJson(i));
    }
  }  
}

void setup(void) 
{
  setUpPinModes();
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  if (WiFi.waitForConnectResult() != WL_CONNECTED) 
  {
    Serial.printf("WiFi Failed!\n");
    return;
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, handleRoot);
  server.onNotFound(handleNotFound);
  
  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);
  
  server.begin();
  Serial.println("HTTP server started");
}

void loop() 
{
  ws.cleanupClients(); 
  readSwitchesAndSetRelayStatus(); 
}
