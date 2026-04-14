#include <Arduino.h>
#include "timer.h"
#include "task.h"

// タスク
void task1(void){while(1);}
void task2(void){while(1);}


void setup() {
    // シリアル通信の初期化
    Serial.begin(115200);
    // タイマーの初期化
    timer_init();
    // タスクの初期化
    task_create(task1,1);
    task_create(task2,2);

    for (int i = 0; i < task_count; i++) {
        Serial.print("task[");
        Serial.print(i);
        Serial.print("] state: ");
        Serial.println(task_table[i].state);   // 0 = READY
        Serial.print("task[");
        Serial.print(i);
        Serial.print("] stack_ptr: 0x");
        Serial.println((uint16_t)task_table[i].stack_ptr, HEX);
    }
}

void loop() {
}