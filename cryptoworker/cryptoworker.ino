/*
  1. Установить библиотеку ArduinoJSON: Меню Sketch > Include Library > Manage Libraries. Ввести: ArduinoJSON
     Выбрать ArduinoJson by Benoit Blanchon
  2. Onwire by Jim Studt
  3. DallasTemperature by Miles Burton

*/

/* Пример конфига:
   {
     "id": 0,
     "ssid": "wifi",
     "pwd": "pass",
     "sensors": {
       "temp": {"type": "ds18b20", "pin": 25},
       "pin4": {"type": "pin", "pin": 4}
     },
     "actions": {
       "enableLed":  {"pin": 26, "level": 1},
       "disableLed": {"pin": 26, "level": 0},
       "enablePump": {"pin": 21, "level": 1}
     },
     "rules":   [
       {"exp": "temp > 31", "actions": ["enableLed"]},
       {"exp": "temp<28", "actions": ["disableLed"]},
       {"exp": "pin4=1 & temp<30", "actions": ["disableLed"]}
     ]
   }
*/


#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <uri/UriBraces.h>
#include <ArduinoJson.h>
#include <FreeRTOS.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <HardwareSerial.h>
#include "./libraries/Expression/Expression.h"

HardwareSerial rs485(2); // use UART2

// const char *SSID = "kmiac";
// const char *PWD = "26122012";
const char *SSID = "wifi";   // по умолчанию, если нет в json-конфиге
const char *PWD = "password";
int connectCount = 0;

int deviceId = 0; // Код клиента в сети RS485. Можно задать в конфиге, в параметре id
int mode = 0; // режим работы контроллера (обычно 0 - автоматический 1 - ручной). Переменная используется в правилах и конфиге. Хранится а ПЗУ

Preferences prefs;
Expression expression;

DynamicJsonDocument mainConfig(3072);
StaticJsonDocument<3072> jsonDocument;
char buffer[3072];
String rs485Buffer;
String varBounds("><= &|()");

typedef struct DSTemperature {
  OneWire *oneWire; // oneWire instance to communicate with any OneWire devices
  DallasTemperature *sensor;
  String name;
  float t;
  DSTemperature (int pin, String name_) {
        oneWire = new OneWire(pin);
        sensor = new DallasTemperature(oneWire);
        name = name_;
        t = -127;
  }
};

typedef struct Sensor {
  String name;
  float value;
  Sensor (String name_, float value_=-127) {
    name = name_;
    value = value_;
  }
};

std::vector <DSTemperature> tempeatureSensors;
std::vector <Sensor> sensorsData;

WebServer server(80);
bool wifiConnected = false;



void setup_routing() {
  server.on("/data", sendData);
  server.on("/sensors", sendSensorData);
  server.on(UriBraces("/action/{}"), []() {
    String action = server.pathArg(0);
    doAction(action);
  });
  server.on("/setConfig", HTTP_POST, handlePost);
  server.begin();
}


// загружаем и парсим json-конфиг с флеша
void loadPrefs() {
  String confBuffer = "{\"id\":1,\"sensors\":{},\"actions\":{},\"rules\":[]}";

  // readConfig from flash
  prefs.begin("mainConfig", true); // false for RW mode
  if(prefs.isKey("mode")) {
    mode = prefs.getInt("mode");
  }

  if(prefs.isKey("jsonBuffer")) {
    confBuffer = prefs.getString("jsonBuffer");
  }
  prefs.end();

  deserializeJson(mainConfig, confBuffer);

  Serial.print("Connecting to Wi-Fi");
  if (mainConfig.containsKey("ssid") && mainConfig.containsKey("pwd")) {
    WiFi.begin(mainConfig["ssid"].as<String>().c_str(), mainConfig["pwd"].as<String>().c_str());
  } else {
    WiFi.begin(SSID, PWD);
  }

  if(mainConfig.containsKey("id")) {
      deviceId =  mainConfig["id"];
  }

  JsonObject sensors = mainConfig["sensors"].as<JsonObject>();

  for (JsonPair p : sensors) {
      sensorsData.push_back( Sensor(p.key().c_str()) );

      if (p.value()["type"].as<String>() == String("ds18b20")) {
        int pin = p.value()["pin"];
        tempeatureSensors.push_back(DSTemperature(pin, p.key().c_str()));
        Serial.print("Add sensor: ");  Serial.println(p.key().c_str());
      }

      if (p.value()["type"].as<String>() == String("pin")) {
        int pin = p.value()["pin"];
        pinMode(pin, INPUT);
      }
  }
  sensorsData.push_back( Sensor("mode", mode) );

  std::sort(sensorsData.begin(), sensorsData.end(), [] (Sensor const& a, Sensor const& b) { return a.name.length() > b.name.length(); });
  for (Sensor s : sensorsData) {
    Serial.print("Sorted: "); Serial.println(s.name.c_str());
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


float getSensorValue(String sensorName = "") {
  for (DSTemperature &s : tempeatureSensors) {

    if (sensorName.length() == 0 || s.name == sensorName) {
      Serial.print(s.name.c_str());
      Serial.print(": ");

      try {
        s.sensor->requestTemperatures();
        s.t = s.sensor->getTempCByIndex(0);
        setSensorValueByName(s.name, s.t);
        Serial.println(s.t);
      } catch(...) {
        Serial.println("error");
      }

      if (sensorName.length() > 0) return s.t;
    }
  }

  return -127;
}

void getAllSensorValues () {
  getSensorValue();

  JsonObject sensors = mainConfig["sensors"];
  for (JsonPair p : sensors) {
      if (p.value()["type"].as<String>() == String("pin")) {
        int pin = p.value()["pin"];
        int v = digitalRead(pin);
        setSensorValueByName(p.key().c_str(), v);
      }

      if (p.value()["type"].as<String>() == String("adc")) {
        int pin = p.value()["pin"];
        float k = p.value()["k"];
        float v = (k ? k : 1) * analogRead(pin);
        setSensorValueByName(p.key().c_str(), v);
      }

      if (p.value()["type"].as<String>() == String("mv")) {
        int pin = p.value()["pin"];
        float k = p.value()["k"];
        float v = (k ? k : 1) * analogReadMilliVolts(pin); // analogRead(pin);
        setSensorValueByName(p.key().c_str(), v);
      }
  }
  setSensorValueByName("mode", mode);
}

void setSensorValueByName(const String &name, float value) {
    for (Sensor &s : sensorsData) {
      if (s.name == name) {
        s.value = value;
      }
    }
}

/*
float getSensorValueByName(const String &name) {
    JsonObject sensors = mainConfig["sensors"];

    for (DSTemperature &s : tempeatureSensors) {
      if (s.name == name) {
        return s.t;
      }
    }

    for (JsonPair p : sensors) {
        if ( name == p.key().c_str() && p.value()["type"].as<String>() == String("pin")) {
          int pin = p.value()["pin"];
          int v = digitalRead(pin);
          return v;
        }
    }

    return -127;
}
*/

bool hasVar( String const &exp,  String const &name) {
  int pos =  exp.indexOf(name);
  int posEnd = pos + name.length(); // next pos after last char of var

  if (pos > -1) {
    bool leftOk = (pos == 0) || (varBounds.indexOf( exp[pos-1] ) > -1);
    bool rightOk = (pos == exp.length()) || (varBounds.indexOf( exp[posEnd] ) > -1);
    return leftOk && rightOk;
  }

  return false;
}

// Основной рабочий цикл (чтение датчиков, выполнение правил)
void worker(void * parameter) {
   JsonArray rules = mainConfig["rules"];

   for (;;) {

      getAllSensorValues(); // read all sensors once


      bool firstLoop = true;
      for (JsonObject p : rules) {
          String exp = p["exp"];
          Serial.print("Rule: ");  Serial.println(exp.c_str());

          bool ok = true;

      /*  Используется String.replace имен датчиков при подстановке в выражение.
          Если название одного датчика окажется подстрокой другого, то будет ошибка при реплейсе
          напр. temp и temp_new. В этом случае напр. при temp=5 выражение "temp>10 | temp_new>15" будет "5>10 | 5_new>15"
          Поэтому сортируем список имен датчиков по длине строки и меняем начиная от самых длинных.
      */
          for (Sensor s : sensorsData) {
            float v = s.value;

            if(hasVar(exp, s.name)) {
              if (v < -126) { // 127 float 126.999 - 127.001
                Serial.print("Rule skipped. No sensor value: ");
                Serial.println(s.name.c_str());
                ok = false;
                break;
              }

              exp.replace(s.name, String(v, (v==1 || v==0) ? 0 : 2));
            }
          }

          // переменная init хранит признак первого запуска (1|0). Т.е. в правиле можно разместить действия при инициализации.
          // Напр.  exp: "init"  или  exp: "init=1 & mode=1"
          exp.replace("init", firstLoop ? "1" : "0");

          if(ok) {
            Serial.print("Evaluating: ");  Serial.println(exp.c_str());
            float result = expression.evaluate(exp);
            Serial.print( "Result: "); Serial.println( result );
            if(result == 1) {
                if (p.containsKey("actions")) {
                  JsonArray actions = p["actions"];
                  for (String action : actions) {
                    doAction(action);
                  }
                }
            } else if(result == 0) {
                if (p.containsKey("else")) {
                  JsonArray actions = p["else"];
                  for (String action : actions) {
                    doAction(action);
                  }
                }
            }
          }

          firstLoop = false;
      }

     vTaskDelay(6000 / portTICK_PERIOD_MS);
   }
}


void doAction(String actionName) {
  Serial.println(actionName.c_str());
  JsonObject action = mainConfig["actions"][actionName]; //{"pin": 1, "level": 0},

  if(action.containsKey("pin")) {
    int pin = action["pin"];
    int level = action["level"];
    digitalWrite(pin, level);
  } else if (action.containsKey("var")) {
      if (action["var"] == "mode") {
          int value = action["value"];
          if (mode != value) {
              mode = value;
              setSensorValueByName("mode", mode);
              prefs.begin("mainConfig", false); // false for RW mode
              prefs.putInt("mode", mode);
              prefs.end();
          }
      }
  }
}

char * getData() {
  Serial.println("Get Data");
  //jsonDocument.clear();

  serializeJson(mainConfig, buffer);
  return buffer;
}

void sendData () {
  char *buf = getData(); // result in global buffer
  server.send(200, "application/json", buf);
}

char * getSensorData() {
  Serial.println("Get Sensor Data");
  jsonDocument.clear();


  for (Sensor s : sensorsData) {
    jsonDocument[s.name] = s.value;
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
  if (id == deviceId || !id) {

    if (action == "sensors") {
        char *buf = getSensorData(); // result in global buffer
        sendRs485(action, buf);
    } else if (action == "data") {
        char *buf = getData(); // result in global buffer
        sendRs485(action, buf);
    } else if (action == "action") {
        String act = cmd.substring(end+1);
        doAction(act);
        sendRs485(act, "OK");
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
    } else if (action == "wifi") {
        cmd.remove(0, 5); // отрезаем: wifi;
        end = cmd.indexOf(';');  // cmd: ssidName;password
        String ssid = cmd.substring(0, end);
        String pwd = cmd.substring(end+1);

        mainConfig["ssid"] = ssid;
        mainConfig["pwd"] = pwd;
        serializeJson(mainConfig, buffer);
        prefs.begin("mainConfig", false); // false for RW mode
        prefs.putString("jsonBuffer", buffer);
        prefs.end();
        Serial.print(F("Set wifi: ")); Serial.print(ssid.c_str()); Serial.print(F(", password: ")); Serial.println(pwd);
        Serial.print(F("Reconnect... "));
        WiFi.disconnect();
        delay(1000);
        WiFi.begin(ssid.c_str(), pwd.c_str());
    }

  }

}

void readCommandFromSerial () {
   rs485Buffer.reserve(127); // Используем буфер от 485. Резервируем место для избежания фрагментации памяти при увеличении размера строки (rs485Buffer += c)

    int i=0;
    while(Serial.available()) {
      char c =  Serial.read();

      if(c == '^') {
        rs485Buffer = "";
      } else if(c == '$') {
        parseCmd(rs485Buffer);
      } else {
        rs485Buffer += c;
      }

      i++;
      delay(20);
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


  if (deviceId) {
    xTaskCreate(
    rs485Worker,
    "RS485 fn",
    5000,
    NULL,
    2,
    NULL
    );
  }

}

void setup() {
  Serial.begin(115200);
  if (deviceId) rs485.begin(115200, SERIAL_8N1);

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
    if (connectCount > 50) {
      Serial.println("Reconnect...");
      connectCount = 0;
      // wifiConnected = false; // comment if not need to repeat setup_routing()
      WiFi.disconnect();
      delay(1000);
      WiFi.reconnect();
      delay(1000);
    }

    readCommandFromSerial();

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