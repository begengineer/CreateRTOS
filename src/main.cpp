#include <Arduino.h>
#include <timer.h>

void setup() {
    Serial.begin(115200);
    Serial.print("initialize");
    timer_init();
}

void loop() {
  static uint32_t last = 0;
  uint32_t now = timer_get_tick();

  if(now - last > 1000){
    last = now;
    Serial.print("tick : ");
    Serial.println(now);
  }
}