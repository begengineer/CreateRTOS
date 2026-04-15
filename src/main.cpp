#include <Arduino.h>
#include "timer.h"
#include "task.h"
#include "scheduler.h"


void task1(void) {
    Serial.println("task1 enter");
    uint16_t cnt = 0;
    while (1) {
        Serial.print("task1 tick: ");
        Serial.println(cnt++);
        task_sleep(500);
    }
}

void task2(void) {
    Serial.println("task2 enter");
    uint16_t cnt = 0;
    while (1) {
        Serial.print("task2 tick: ");
        Serial.println(cnt++);
        task_sleep(500);        // 500ms 寝る
    }
}


void setup() {
    Serial.begin(115200);
    Serial.println("initialize");
    pinMode(12, OUTPUT);

    task_create(task1, 1);
    task_create(task2, 1);

    current_task = 0;
    task_table[0].state = TASK_RUNNING;
    current_tcb = &task_table[0];

    timer_init();
    task_start();
}

void loop() {

}