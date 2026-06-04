#include "HX711.h"
#include <Servo.h>

#define DOUT 3
#define CLK 2
#define SERVO_PIN 4

HX711 scale;
Servo servo;

float calibration_factor = 16.42; 
float weight_trigger = 100.0; // Initial trigger to move to 180
float peak_weight = 0.0;      // Stores the highest weight seen while rotated
bool isRotated = false;         

void setup() {
  Serial.begin(9600);
  scale.begin(DOUT, CLK);
  scale.set_scale(calibration_factor);

  servo.attach(SERVO_PIN);
  servo.write(0); 

  Serial.println("Taring scale...");
  delay(2000);
  scale.tare();
  Serial.println("System Ready!");
}

void loop() {
  float current_weight = scale.get_units(5); // Faster sampling for peak tracking

  // --- PHASE 1: TRIGGER ---
  // If weight hits the threshold, rotate and start tracking the peak
  if (current_weight >= weight_trigger && !isRotated) {
    Serial.println(">>> Weight Triggered! Rotating to 180.");
    servo.write(180);
    isRotated = true;
    peak_weight = current_weight; 
  } 

  // --- PHASE 2: PEAK TRACKING & RESET ---
  if (isRotated) {
    // Update the peak weight if the load keeps increasing
    if (current_weight > peak_weight) {
      peak_weight = current_weight;
    }

    // Reset only if the weight drops 100g below the highest point reached
    if (current_weight <= (peak_weight - 100.0)) {
      Serial.print("<<< Weight dropped 100g from peak (");
      Serial.print(peak_weight);
      Serial.println("g). Resetting to 0.");
      
      servo.write(0);
      isRotated = false;
      peak_weight = 0; // Clear peak for the next cycle
    }
  }

  Serial.print("Weight: ");
  Serial.print(current_weight, 1);
  Serial.print(" g | Peak: ");
  Serial.println(peak_weight, 1);

  delay(200); // Slightly faster loop to ensure we catch the peak accurately
}
