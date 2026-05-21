#include "HX711.h"



#define DOUT 3

#define CLK  2



HX711 scale;



float calibration_factor = 16.42;



void setup() {

  Serial.begin(9600);

  scale.begin(DOUT, CLK);



  scale.set_scale(calibration_factor);



  Serial.println("Remove all weight...");

  delay(2000);



  scale.tare();

  Serial.println("Tare done.");

}



void loop() {

  float weight = scale.get_units(10);



  Serial.print(weight, 1);

  Serial.println(" g");



  delay(100);

}
