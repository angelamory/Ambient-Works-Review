/**
 * wings_scale.ino — pollution-indicator wings (Uno/Nano)
 *
 * Two modes:
 *
 *  1) BRIDGE MODE (normal running):
 *     Listens on Serial for "F:<level>\n" commands (0-100) sent by
 *     ambient_hub.py, which correlates live VOC readings from the Ambient
 *     kit with weight on this scale. The wings angle is driven directly
 *     by that level — 0 = wings up (clean air), 100 = wings fully down
 *     (severe pollution).
 *
 *     This board also prints "W:<weight>\n" once per loop, which is the
 *     wire format ambient_hub.py already expects — that part of the
 *     original sketch was fine and is unchanged.
 *
 *  2) FALLBACK MODE (bridge / Ambient kit not connected or not running):
 *     If no "F:" command has arrived for BRIDGE_TIMEOUT_MS, this board
 *     falls back to estimating pollution on its own, using the same
 *     "weight drop = solvent poured out = VOC released while painting"
 *     idea your original trigger/reset logic used — just made continuous
 *     (0-100) instead of an on/off snap between two fixed angles.
 *
 *     IMPORTANT: fallback mode does NOT measure real air quality. It's a
 *     rough stand-in so the wings still do something meaningful when the
 *     bridge/kit is offline. Tune FALLBACK_MAX_POUR_G below against your
 *     own bottle/session — see the comment on that constant.
 *
 * Wiring / pins unchanged from the original sketch.
 */

#include "HX711.h"
#include <Servo.h>

#define DOUT 3
#define CLK 2
#define SERVO_PIN 4

HX711 scale;
Servo servo;

// ── Calibration (unchanged from your original sketch) ─────────────
float calibration_factor = 16.42;

// ── Servo travel ────────────────────────────────────────────────────
const int WINGS_UP   = 120;  // clean air / no pollution signalled
const int WINGS_DOWN = 40;   // max pollution signalled

// ── Bridge protocol ──────────────────────────────────────────────────
const unsigned long BRIDGE_TIMEOUT_MS = 5000;  // no "F:" for this long -> fallback
unsigned long lastCommandMillis = 0;
bool bridgeActive = false;
int  flapLevel = 0;  // 0-100, current commanded (bridge) or estimated (fallback) level

String serialBuffer = "";

// ── Fallback tuning (no bridge / kit connected) ──────────────────────
// Same "peak then drop" idea as your original code: weight rising past
// POUR_START_G means a bottle has been placed and is "in use"; from then
// on we track how far weight has dropped from its peak (= how much has
// been poured out). That drop is mapped 0-100 onto the wings.
float peak_weight = 0.0;
bool  pouringActive = false;
const float POUR_START_G        = 100.0;  // weight rise that counts as "bottle placed / in use"
const float FALLBACK_MAX_POUR_G = 300.0;  // pour amount (g) that fully floors the wings.
                                           // TUNE THIS: weigh out roughly how much solvent
                                           // you'd expect to use in one painting session and
                                           // put that number here.

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
  // else: flapLevel already holds the value from the last "F:" command

  int angle = map(flapLevel, 0, 100, WINGS_UP, WINGS_DOWN);
  servo.write(angle);

  // Unchanged wire format — this is what ambient_hub.py reads as latest_weight
  Serial.print("W:");
  Serial.println(current_weight, 1);

  delay(200);
}

// Reads "F:<0-100>\n" lines from ambient_hub.py and updates flapLevel.
// Non-blocking: only processes whatever bytes have arrived since last loop.
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
      if (serialBuffer.length() > 16) serialBuffer = "";  // guard against garbage/noise
    }
  }
}

// Standalone pollution estimate from weight loss (solvent poured while painting).
// Used only when no bridge command has arrived recently.
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

  int level = (int)round((poured / FALLBACK_MAX_POUR_G) * 15);
  return constrain(level, 0, 100);
}
