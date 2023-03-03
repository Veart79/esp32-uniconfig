/*
  1. Установить библиотеку ArduinoJSON: Меню Sketch > Include Library > Manage Libraries. Ввести: ArduinoJSON
     Выбрать ArduinoJson by Benoit Blanchon
  2. Onwire by Jim Studt
  3. DallasTemperature by Miles Burton

*/

/* Пример конфига:
{
  "sensors": {
    "temp":     {"type": "ds18b20", "pin": 25}    
  },          
  "actions": {
    "disableLed": {"pin": 26, "level": 0}, 
    "enableLed": {"pin": 26, "level": 1}, 
    "enablePump": {"pin": 21, "level": 1}
  },
  "rules":   [
    {"sensor": "temp", "max": 31, "action": "enableLed"}, 
    {"sensor": "temp", "min": 29, "action": "disableLed"}
  ],
  "id": 1
}
*/


#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <FreeRTOS.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <HardwareSerial.h>

HardwareSerial rs485(2); // use UART2

// const char *SSID = "kmiac";
// const char *PWD = "26122012";
const char *SSID = "RtVeart";
const char *PWD = "tcapacitytcapacitytcapacity";
int connectCount = 0;

int deviceId = 1; // Код клиента в сети RS485. Можно задать в конфиге, в параметре id

Preferences prefs;

DynamicJsonDocument mainConfig(1024);
StaticJsonDocument<1024> jsonDocument;
char buffer[1024];
String rs485Buffer;

typedef struct DSTemperature {
  OneWire *oneWire; // oneWire instance to communicate with any OneWire devices
  DallasTemperature *sensor; 
  String name;
  float t;
  DSTemperature (int pin, String name_) {
        oneWire = new OneWire(pin);
        sensor = new DallasTemperature(oneWire);   
        name = name_;
        t = -999;
  }
};

std::vector <DSTemperature> tempeatureSensors;

WebServer server(80);
bool wifiConnected = false;


 
void setup_routing() {       
  server.on("/data", sendData);     
  server.on("/sensors", sendSensorData);  
  server.on("/setConfig", HTTP_POST, handlePost);              
  server.begin();    
}
 

// загружаем и парсим json-конфиг с флеша 
void loadPrefs() {
  String confBuffer = "{\"id\":1,\"sensors\":{},\"actions\":{},\"rules\":[]}";
  
  // readConfig from flash
  prefs.begin("mainConfig", true); // false for RW mode
  if(prefs.isKey("jsonBuffer")) {
    confBuffer = prefs.getString("jsonBuffer"); 
  }
  prefs.end();

  deserializeJson(mainConfig, confBuffer);

  if(mainConfig.containsKey("id")) {
      deviceId =  mainConfig["id"];
  }

  JsonObject sensors = mainConfig["sensors"].as<JsonObject>();

  for (JsonPair p : sensors) { 
      if (p.value()["type"].as<String>() == String("ds18b20")) {
        int pin = p.value()["pin"];     
        tempeatureSensors.push_back(DSTemperature(pin, p.key().c_str()));      
      }      
  }  

  JsonObject actions = mainConfig["actions"]; //.as<JsonObject>();

  for (JsonPair p : actions) { 
      int pin = p.value()["pin"];
      if(pin > 0 && pin < 40) {
        Serial.print("Set mode OUTPUT for pin "); Serial.println(pin);
        pinMode(pin, OUTPUT);
      }
  } 
}

// Основной рабочий цикл (чтение датчиков, выполнение правил)
void worker(void * parameter) {
   JsonArray rules = mainConfig["rules"]; 

   for (;;) {     
      //"rules":   [{"sensor": "temp", "max": 100, "action": "disableFan"},  {"sensor": "pressure", "min": 10, "action": "enablePump"}]
      for (JsonObject p : rules) { 
          //const char* key = p.key().c_str();
          String s = p["sensor"];          

          float v = getSensorValue(p["sensor"]);
          if (v == -999) {
            Serial.print("Read sensor error: ");
            Serial.println(s.c_str());
            continue;
          }

          p["value"] = v;

          Serial.print(s.c_str());
          Serial.print(": ");
          Serial.println(v);

          if (p.containsKey("max")) {
            int max = p["max"];
            if (v >= max) {
              doAction(p["action"]);
            }
          } 

          if (p.containsKey("min")) {
            int min = p["min"];
            if (v <= min) {
              doAction(p["action"]);
            }
          }  
      }  

     vTaskDelay(6000 / portTICK_PERIOD_MS);
   }
}

float getSensorValue(String sensorName) {
  for (DSTemperature &s : tempeatureSensors) {
    if (s.name == sensorName) {
      s.sensor->requestTemperatures(); 
      s.t = s.sensor->getTempCByIndex(0);
      return s.t;
    }
  }

  return -999;
}

int doAction(String actionName) {
  Serial.println(actionName.c_str());
  JsonObject action = mainConfig["actions"][actionName]; //{"pin": 1, "level": 0}, 
  int pin = action["pin"];
  int level = action["level"];
  digitalWrite(pin, level);
} 

char * getData() {
  Serial.println("Get Data");
  //jsonDocument.clear();
  
  serializeJson(mainConfig, buffer);
  return buffer;
}

void sendData () {
  char *buf = getData(); // result in global buffer  
  server.send(200, "application/json", buffer);
}

char * getSensorData() {
  Serial.println("Get Sensor Data");
  jsonDocument.clear();

  for (DSTemperature s : tempeatureSensors) {
    // JsonObject sensor  = jsonDocument.createNestedObject(s.name);
    // sensor["name"] = s.name;
    // sensor["t"] = s.t;
    jsonDocument[s.name] = s.t;
  }  

  serializeJson(jsonDocument, buffer);
  return buffer;
}

void sendSensorData() {
  char *buf = getSensorData(); // result in global buffer
  server.send(200, "application/json", buf);
}

void handlePost() {
  String body = server.arg("plain");
  char *buf = setConfig(body);

  if (buf == NULL) { //no error
    server.send(200, "application/json", "{\"success\": true}");
    Serial.println("Config saved! Restarting...");    
    delay(2000);
    ESP.restart();

  } else {
    server.send(200, "application/json", buf);  
  }  
}


char * setConfig(String &body) {  
  DeserializationError err = deserializeJson(jsonDocument, body);

  if (err) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(err.c_str());
    
    jsonDocument.clear();
    JsonObject obj = jsonDocument.createNestedObject();
    obj["success"] = false;
    obj["msg"] = err.c_str();
    serializeJson(jsonDocument, buffer);
    return buffer; 
    
  } else {
    prefs.begin("mainConfig", false); // false for RW mode
    prefs.putString("jsonBuffer", body);   
    prefs.end();

    return NULL;
  }   
}

void sendRs485(String &action, char *data = NULL) {
      digitalWrite(32, HIGH);        
      digitalWrite(33, HIGH);  
      delay(50);
      String id(deviceId);
      rs485.write('^');
      if(deviceId < 10) rs485.write( '0' ); // 0
      rs485.write( id.c_str() ); 
      rs485.write(';');
      rs485.write(action.c_str()); 
      if (data) {
        rs485.write(';');
        rs485.write(data); 
      }
      rs485.write('$');
      
      delay(100);
      digitalWrite(32, LOW);        //  (LOW to receive value from Master)
      digitalWrite(33, LOW);        //  (LOW to receive value from Master)
}

void parseCmd (String &cmd) {
  Serial.print("Cmd: ");
  Serial.println(cmd.c_str());
  if (cmd.length() < 2) return;
  int id = cmd.substring(0,2).toInt();
  Serial.print(F("deviceId: ")); Serial.print(deviceId); Serial.print(F(", dst: ")); Serial.println(id);

  cmd.remove(0, 3); // 2 цифры - номер устройства и ;
  int end = cmd.indexOf(';');  
  String action = end > 0 ? cmd.substring(0, end) : cmd;
  if (id == deviceId) {
    
    if (action == "sensors") {
        char *buf = getSensorData(); // result in global buffer
        sendRs485(action, buf);       
    } else if (action == "data") {       
        char *buf = getData(); // result in global buffer
        sendRs485(action, buf);   
    } else if(action == "setConfig" && cmd.length() > end) {
        String body = cmd.substring(end+1);
        char *buf = setConfig(body);

        if (buf == NULL) { //no error
          sendRs485(action, "OK"); 
          Serial.println("Config saved! Restarting...");    
          delay(2000);
          ESP.restart();
        } else {
          sendRs485(action, buf);
        }  
    }

  }
  
}


void rs485Worker (void * parameter) {
   rs485Buffer.reserve(1024); // резервируем место для избежания фрагментации памяти при увеличении размера строки (rs485Buffer += c)
   pinMode(32, OUTPUT);
   digitalWrite(32, LOW);        //  (LOW to receive value from Master)
   pinMode(33, OUTPUT);
   digitalWrite(33, LOW);        //  (LOW to receive value from Master)
   rs485.begin(115200, SERIAL_8N1, 16, 17); 




   for (;;) {     
      int i=0;
      while(rs485.available()) {
        char c =  rs485.read();
        Serial.print(c);
        if(c == '^') {
          rs485Buffer = "";
        } else if(c == '$') {
          parseCmd(rs485Buffer);
        } else {
          rs485Buffer += c;
        }

        i++;
        // delay(10);      
      }  

      if (i > 0) {
        Serial.print(F("Bytes read: "));
        Serial.println(i);
      }

      vTaskDelay(500 / portTICK_PERIOD_MS);
   }
}

void setup_task() {    
  xTaskCreate(     
  worker,      
  "Main worker fn",      
  2000,      
  NULL,      
  1,     
  NULL     
  );     


  xTaskCreate(     
  rs485Worker,      
  "RS485 fn",      
  5000,      
  NULL,      
  2,     
  NULL     
  ); 
  
}

void setup() {     
  Serial.begin(115200); 
  rs485.begin(115200, SERIAL_8N1);

  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(SSID, PWD);
   
  loadPrefs();  
  setup_task();       
}    
       
void loop() {   

  if (WiFi.status() != WL_CONNECTED) { // wait for connect
    Serial.print(".");
    delay(200);
    digitalWrite(26, HIGH);
    delay(200);
    digitalWrite(26, LOW);    
    if (connectCount > 1000) {
      Serial.println("Reconnect...");    
      connectCount = 0;
      // wifiConnected = false; // comment if not need to repeat setup_routing() 
      WiFi.disconnect();
      delay(1000);
      WiFi.reconnect();
      delay(1000);
    }
    connectCount++;

  } else if (!wifiConnected) { // init webserver
    Serial.print("Connected! IP Address: ");
    Serial.println(WiFi.localIP());    
    setup_routing();     
    wifiConnected = true;
  } else { // webserver 
    server.handleClient();     
  }

  delay(100);
}