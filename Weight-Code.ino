
#include "HX711.h"
#include <Servo.h>

#define DOUT 3
#define CLK 2
#define SERVO_PIN 4

HX711 scale;
Servo servo;

float calibration_factor = 16.42;

const int WINGS_UP   = 120; 
const int WINGS_DOWN = 40;   

const unsigned long BRIDGE_TIMEOUT_MS = 5000;  
unsigned long lastCommandMillis = 0;
bool bridgeActive = false;
int  flapLevel = 0; 
String serialBuffer = "";


float peak_weight = 0.0;
bool  pouringActive = false;
const float POUR_START_G        = 100.0; 
const float FALLBACK_MAX_POUR_G = 300.0;  

void setup() {
  Serial.begin(9600);
  scale.begin(DOUT, CLK);
  scale.set_scale(calibration_factor);

  servo.attach(SERVO_PIN);
  servo.write(WINGS_UP);

  Serial.println("Resetting scale...");
  delay(2000);
  scale.tare();
  Serial.println("READY");
}

void loop() {
  float current_weight = scale.get_units(5);

  readBridgeCommands();

  bridgeActive = (millis() - lastCommandMillis) < BRIDGE_TIMEOUT_MS;

  if (!bridgeActive) {
    flapLevel = fallbackFlapLevel(current_weight);
  }

  int angle = map(flapLevel, 0, 100, WINGS_UP, WINGS_DOWN);
  servo.write(angle);

  Serial.print("W:");
  Serial.println(current_weight, 1);

  delay(200);
}

void readBridgeCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      serialBuffer.trim();
      if (serialBuffer.startsWith("F:")) {
        int level = serialBuffer.substring(2).toInt();
        flapLevel = constrain(level, 0, 100);
        lastCommandMillis = millis();
      }
      serialBuffer = "";
    } else if (c != '\r') {
      serialBuffer += c;
      if (serialBuffer.length() > 16) serialBuffer = ""; 
    }
  }
}


int fallbackFlapLevel(float current_weight) {
  if (!pouringActive) {
    if (current_weight >= POUR_START_G) {
      pouringActive = true;
      peak_weight = current_weight;
    }
    return 0;
  }

  if (current_weight > peak_weight) {
    peak_weight = current_weight; 
  }

  if (current_weight < POUR_START_G - 20.0) {
    pouringActive = false;
    peak_weight = 0.0;
    return 0;
  }

  float poured = peak_weight - current_weight;  
  if (poured <= 0) return 0;

  int level = (int)round((poured / FALLBACK_MAX_POUR_G) * 20);
  return constrain(level, 0, 100);
}
