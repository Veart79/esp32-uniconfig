# esp32-uniconfig
Universal firmware for esp32 with json config by API (wifi) or RS485

# Прошивка для ESP32

*Проект в процессе разработки*

## Описание конфигурации
Конфигурация для работы контроллера задается в виде JSON-объекта, который состоит их трех разделов: Датчики (sensors), Действия (actions) и Правила (rules)
 ```json
    {
      "id": 1,
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
        {"exp": "temp > 31", "action": "enableLed"}, 
        {"exp": "temp<28", "action": "disableLed"},
        {"exp": "pin4=1 & temp<30", "action": "disableLed"}
      ]
    }
```

### Код устройства (id):
Код устройства в сети RS485, целое число 1-99
Используется для идентификации устроства при отправке ему команд по RS485
Если код не указан или равен 0, то взаимодействие по RS485 осуществляться не будет.

### Датчики (sensors) 
Датчики (sensors) - объект, который содержит именованный список датчиков с указанием типа датчика и ножки GPIO к которой он подключен ("pin": 25). На данный момент поддерживаются типы:
 - "ds18b20"  ("type": "ds18b20"). Имя датчика ("temp":) используется для идентификации датчика и его использования в правилах работы контроллера.
 - "pin"  ("type": "pin"). Дискретный вход GPIO. Система следит за состоянием пина. При наличии напряжения значение переменной ("pin4" в примере) будет равно 1 иначе 0.

Таким образом в примере мы задали датчик с именем "temp", его тип "ds18b20" и он подключен к 25й ножке GPIO контроллера (желтый провод для передачи данных).  
А также "датчик" с именем "pin4", который подключен к 4й ножке GPIO и показывает уровень сигнала на ней.

Пример:
```
  "sensors": {
    "temp25":   {"type": "ds18b20", "pin": 25},
    "temp26": {"type": "ds18b20", "pin": 26},
    "pin4":   {"type": "pin", "pin": 4}          
  },
```


### Действия (actions) 
Данный объект содержит именованный список действий с указанием ножки GPIO и уровня сигнала, который будет на нее подан (1 - HIGH,  0 - LOW (GND)) при срабатывании этого действия.
Имя действия  (напр. "enableLed":) необходимо для его использования в правилах работы контроллера.
Напр. действие    "enableLed":  {"pin": 26, "level": 1}, 
означает что на ножку  GPIO 26 будет подано напряжение (высокий уровень).

### Правила (rules)
Правила представляют собой массив объектов, которые задают алгоритм работы контроллера. Они бесконечно выполняются в основном рабочем цикле контроллера. Если условия, указанные в правиле срабатывают, то выполняется указанное в правиле действие.
Выражение правила задается в параметре "exp" (expression) и представляет собой логическое выражение с использованием переменных из раздела "sensors" и операторов:
 - Сравнения. Больше  ">", Меньше "<",  Равно "=".
 - Логические операторы. Логическое "И" - "&", логическое "ИЛИ" - "|"
 - Скобки "(", ")" для указания приоритета вычислений

Действие задается в параметре "action", в котором указывается название действия из раздела “actions”.

Напр. правило:  {"exp": "pin4=1 & temp<30", "action": "disableLed"}
Означает, что если значение датчика "temp" (который описан в разделе sensors)  опустилось ниже значение 30 градусов и при этом на ножке GPIO 4 ("pin4" - тоже из раздела sensors) высокий уровень сигнала (HIGH, напряжение 3.3В), то выполнится действие "disableLed" (которое описано в разделе actions).  В данном примере отключится подача напряжения на 26й пин на котором потухнет светодиод.

Правило   {"exp": "temp<28", "action": "disableLed"}  
выключит светодиод на том же 26м пине, выполнив действие  "disableLed" если температура опустится ниже 28 градусов.


#### Грамматика для логического выражения 
Вычисление логического выражения  осуществляется методом рекурсивного спуска со следующей грамматикой:
E -> T | T '|' E | T '&' E  
T -> F | F '>' T | F '<' T | F '=' T  
F -> N | '(' E ')' | '!' F  

, где N - целое число или число с десятичной точкой

т.е. Логические операции И/ИЛИ имеют одинаковый приоритет! Используйте скобки для явного указания приоритета, если используете оба оператора И и ИЛИ в выражении. Напр. "t > 15 | (p=1 & s<30)"

*Примечание.
Старайтесь использовать понятные названия в именовании датчиков и действий, чтобы конфигурация легко читалась. В то же время не используйте длинные имена, потому что размеры конфигурации ограничены размером 2кБ (2048 символа).*

*Примечание 2.* 
*При подстановке в значений датчиков вместо переменных в выражение используется тупо String.replace,
поэтому нельзя называть датчики так, чтобы название одного датчика оказалось подстрокой другого.  
Напр. temp и temp_new. В этом случае напр. при temp=5 выражение "temp>10 | temp_new>15" будет "5>10 | 5_new>15"*


Пример конфигурации, который указан выше можно использовать для тестирования. На 26й пин подключить диод, на 25й - датчик температуры. Если нагреть датчик в руке до 30 градусов - загорится диод. Если отпустить датчик и подождать пока он остынет до 28 градусов - диод потухнет. При этом, если на ножке GPIO 4 будет высокий уровень, то диод потухнет уже при 30 градусах.


## API (WiFi)
Url - IP адрес смотрим в логе контроллера или на роутере или сканим сеть.

GET http://192.168.1.100/data  - получить данные текущей конфигурации  
GET http://192.168.1.100/sensors  - получить данные датчиков  
GET http://192.168.1.100/action/enableLed  - выполнить действие “enableLed“  

POST http://192.168.1.100/setConfig - в теле запроса передаем json с новым конфигом  
  

## Протокол взаимодействия по RS485:
Используется текстовый протокол  для совместимости и унификации форматов взаимодействия по Wifi и RS485. 
Основные принципы формата аналогичны modbus, но упрощены. Все данные представлены в тектовом виде. 
В случае необходимости передачи бинарных данных используем base64.

**Формат команд:**

``` 
^01;sensors$
```
где ^ - символ начала пакета(команды)
01 - код устройства, которому адресована команда
; - разделитель адреса, команд и параметров
sensor - команда для получения информации с датчиков (список команд аналогичен командам API)
$ - символ окончания пакета

**Ответ клиента:**
```
^01;sensors;{"temp":27.4375}$
```
содержит адрес клиента, который отправляет данные, имя команды на которую возвращается ответ и символы начала и конца пакета.

**Пример команды с параметрами (задать конфигурацию):**
```
^02;setConfig;{"sensors":{"temp":{"type":"ds18b20","pin":25}},"actions":{"disableLed":{"pin":26,"level":0},"enableLed":{"pin":26,"level":1},"enablePump":{"pin":21,"level":1}},"rules":[{"sensor":"temp","max":31,"action":"enableLed","value":27.625},{"sensor":"temp","min":29,"action":"disableLed","value":27.6875}],"id":1}$
```
Параметр идет после названия команды и разделен точкой с запятой.

*Примечание.
При отправке пакета более 256 байт по RS485 нужно отправлять данные по частям, блоками с небольшой паузой, чтобы буфер порта не переполнялся. Контроллер должен успевать вычитывать данные из буфера, чтобы освободить место для новой порции. В modbus это решено более грамотно -  отправкой по частям с подтверждением каждого пакета и следующий пакет (не более256 байт) отправляется только после подтверждения получения предыдущего. Потом части собираются в целое сообщение. В данной реализации просто делается пауза между отправками блоков - этого достаточно, потому что клиент читает каждые полсекунды.*


