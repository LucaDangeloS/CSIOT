#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>

// Configuración LEDS
#define LED_PIN 2         // GPIO2 (D4)
#define LED_COUNT 15      // Number of LEDs

enum OpMode {
  NOP = 0x00,
  BLINK = 0x01,
  TRAILING = 0x02,
  OFF = 0x03,
  ON = 0x04,
  COLOR = 0x05,
  ARRAY = 0x06
};

uint32_t* COLORS = nullptr;
int colorCount = 0;
int currentColorIdx = 0;
OpMode MODE = OFF;
OpMode PREVMODE = OFF;
int DELAY = 500;
volatile bool UPDATINGOP = false;

void colorFlash(Adafruit_NeoPixel& strip, uint32_t color) {
  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();
}

void colorWipe(Adafruit_NeoPixel& strip, uint32_t color, int delayTime) {
  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, color);
    strip.show();
    delay(delayTime);
  }
}

void setBlinking(int delayTime) {
  Serial.println("Setting blinking");
  UPDATINGOP = true;
  MODE = BLINK;
  DELAY = delayTime;
  UPDATINGOP = false;
}

void setTrailing(int delayTime) {
  Serial.println("Setting trailing");
  UPDATINGOP = true;
  MODE = TRAILING;
  DELAY = delayTime;
  UPDATINGOP = false;
}

void setOff(Adafruit_NeoPixel& strip) {
  Serial.println("Setting Off");
  UPDATINGOP = true;
  PREVMODE = MODE;
  MODE = OFF;
  colorFlash(strip, 0);
  UPDATINGOP = false;
}

void processStrip(Adafruit_NeoPixel& strip) {
  while (UPDATINGOP) {
    return;
  }
  if (colorCount <= 0) return;

  switch (MODE) {
    // now logic to flash the next color to the strip
    case BLINK: {
      colorFlash(strip, COLORS[currentColorIdx]);
      delay(DELAY);
      break;
    }

    // logic to wipe the color in the strip
    case TRAILING: {
      colorWipe(strip, COLORS[currentColorIdx], DELAY);
      break;
    }

    case OFF: {
      break;
    }
  }

  if (colorCount > 1) {
    currentColorIdx = (currentColorIdx + 1) % colorCount;
  }
}

uint32_t hexToColor(String hex) {
  long number = strtol(&hex[1], nullptr, 16);
  return Adafruit_NeoPixel::Color(
    (number >> 16) & 0xFF, 
    (number >> 8) & 0xFF, 
    number & 0xFF
  );
}

OpMode parseOp(String op) {
  if (op == "blink") return BLINK;
  if (op == "trailing") return TRAILING;
  if (op == "off") return OFF;
  if (op == "color") return COLOR;
  if (op == "array") return ARRAY;
  if (op == "on") return ON;
  
  return NOP;
}

// Configuración WiFi
const char* ssid = "...";
const char* password = "...";

// Configuración MQTT
const char* mqtt_server = "...";
const int mqtt_port = 8884;
const char* mqtt_user = "...";
const char* mqtt_password = "...";
const char* mqtt_topic = "...";

// Fingerprint del certificado
const char* fingerprint = "96 AC 20 19 3E 46 92 C8 16 A0 CF 4C 34 78 87 01 5D 5E CE 5E";

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

WiFiClientSecure espClient;
PubSubClient client(espClient);

void simulateMQTTMessage(const char* topic, const char* jsonMessage) {
    size_t length = strlen(jsonMessage);
    byte payload[length + 1];  // Create a byte array of the same length
    memcpy(payload, jsonMessage, length);  // Copy the JSON string into the byte array

    callback((char*)topic, payload, length);
}

void callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Mensaje recibido [");
    Serial.print(topic);
    Serial.print("]: ");
    
    String message = "";
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    StaticJsonDocument<256> json;
    deserializeJson(json, message);

    OpMode op = parseOp(json["op"].as<String>());
    Serial.println(json["op"].as<String>());

    switch (op) {
      case BLINK: {
        int delayTime = json["data"].as<int>();
        setBlinking(delayTime);
        break;
      }

      case TRAILING: {
        int delayTime = json["data"].as<int>();
        setTrailing(delayTime);
        break;
      }

      case OFF: {
        setOff(strip);
        break;
      }

      case ON: {
        if (MODE == OFF) {
          MODE = PREVMODE;
        } else return;
        if (MODE == BLINK) setBlinking(DELAY);
        else if (MODE == TRAILING) setTrailing(DELAY);
        break;
      }

      case COLOR: {
        UPDATINGOP = true;
        uint32_t color = hexToColor(json["data"].as<String>());
        COLORS = new uint32_t[1];
        COLORS[0] = color;
        colorCount = 1;
        UPDATINGOP = false;
        break;
      }

      case ARRAY: {
        UPDATINGOP = true;
        JsonArray colorArray = json["data"].as<JsonArray>();
        colorCount = colorArray.size();
        if (COLORS != nullptr) {
          delete[] COLORS;
        }
        COLORS = new uint32_t[colorCount];

        for (int i = 0; i < colorCount; i++) {
          COLORS[i] = hexToColor(colorArray[i].as<String>());
        }
        UPDATINGOP = false;
        break;
      }
    }
}

void reconnect() {
    while (!client.connected()) {
        Serial.print("Intentando conexión MQTT... ");

        String clientId = "ESP8266Client-";
        clientId += String(random(0xffff), HEX);

        if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
            Serial.println("Conectado al broker MQTT");
            client.subscribe(mqtt_topic);  // Suscribirse al tópico
        } else {
            Serial.print("Fallo rc=");
            Serial.print(client.state());
            Serial.println(" Intentando de nuevo en 5 segundos...");
            delay(5000);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(10);
    Serial.println('\n');

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH); // Apagar LED al inicio

    WiFi.begin(ssid, password);
    Serial.print("Conectando a WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("\nConectado a WiFi");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    // Configurar el fingerprint del certificado
    espClient.setFingerprint(fingerprint);

    // Opcional: Desactivar la verificación del certificado (para certificados autofirmados)
    espClient.setInsecure();

    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    strip.begin();
    strip.show();
}

void loop() {
    if (!client.connected()) {
        reconnect();
    }
    client.loop();
    processStrip(strip);
}

// {
//   "op":   "blink"
//   "data": "delay"
// }
// {
//   "op": "trailing"
//   "data": "number"
// }
// {
//   "op": "color"
//   "data": "#495876"
// }
// {
//   "op": "array"
//   "data": ["#124567", "#192847"]
// }
// {
//   "op": "on"
//   "data": ""
// }
// {
//   "op": "off"
//   "data": ""
// }