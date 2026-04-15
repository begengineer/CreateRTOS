#include "scheduler.h"
#include "timer.h"   // sys_tick を参照するため

volatile uint8_t current_task = 0;
TCB * volatile current_tcb = nullptr;

void schedule_next(void){
    // (1) 現タスクが RUNNING なら READY に戻す
    //     BLOCKED の場合はそのまま (task_sleep 中なので起こさない)
    if (task_table[current_task].state == TASK_RUNNING) {
        task_table[current_task].state = TASK_READY;
    }

    // (2) BLOCKED タスクの起床判定
    //     wake_tick に達していたら READY に戻す
    for (uint8_t i = 0; i < task_count; i++) {
        if (task_table[i].state == TASK_BLOCKED &&
            sys_tick >= task_table[i].wake_tick) {
            task_table[i].state = TASK_READY;
        }
    }

    // (3) 次の READY タスクをラウンドロビンで探す (自分以外を優先)
    for (uint8_t i = 1; i <= task_count; i++) {
        uint8_t idx = (current_task + i) % task_count;
        if (task_table[idx].state == TASK_READY) {
            current_task = idx;
            task_table[idx].state = TASK_RUNNING;
            current_tcb = &task_table[idx];
            return;
        }
    }

    // (4) READY タスクが 1 つもない場合
    //     - 現タスクが READY (自分だけ動ける) → RUNNING に戻して継続
    //     - 現タスクも BLOCKED (全員寝ている) → そのまま。次の tick で再判定
    if (task_table[current_task].state == TASK_READY) {
        task_table[current_task].state = TASK_RUNNING;
    }
    current_tcb = &task_table[current_task];
}