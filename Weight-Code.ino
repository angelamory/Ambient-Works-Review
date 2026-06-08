#include "HX711.h"
#include <Servo.h>

#define DOUT 3
#define CLK 2
#define SERVO_PIN 4

HX711 scale;
Servo servo;

float calibration_factor = 16.42;
float weight_trigger = 100.0; 
float peak_weight = 0.0;      
bool isRotated = false;

void setup() {
  Serial.begin(9600);
  scale.begin(DOUT, CLK);
  scale.set_scale(calibration_factor);

  servo.attach(SERVO_PIN);
  servo.write(120);

  Serial.println("Resetting scale...");
  delay(2000);
  scale.tare();
  Serial.println("System Ready!");
}

void loop() {
  float current_weight = scale.get_units(5); // Faster sampling for peak tracking

  // --- PHASE 1: TRIGGER ---
  if (current_weight >= weight_trigger && !isRotated) {
    Serial.println(">>> Weight Triggered!");
    servo.write(40);
    isRotated = true;
    peak_weight = current_weight;
  }

  // --- PHASE 2: PEAK TRACKING & RESET ---
  if (isRotated) {
    if (current_weight > peak_weight) {
      peak_weight = current_weight;
    }

    // Reset only if the weight drops 100g below the highest point reached
    if (current_weight <= (peak_weight - 100.0)) {
      Serial.print("<<< Thanks for using less solvent!");
      Serial.println(" Protect the environment!");

      servo.write(120);
      isRotated = false;
      peak_weight = 0; // Clear peak for the next cycle
    }
  }

 Serial.print("Weight: ");
  Serial.print(current_weight, 1);
  Serial.println(" g");

  delay(200); // Slightly faster loop to ensure we catch the peak accurately
}
