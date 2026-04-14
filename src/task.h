#pragma once
#include <stdint.h>

#define MAX_TASKS 4
#define STACK_SIZE 256 // 1タスクあたりのスタックサイズ

// タスクの状態
typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
}TaskState;

// タスク制御ブロック
typedef struct {
    uint8_t *stack_ptr; // 現在のスタックのポインタ
    uint8_t stack[STACK_SIZE]; // タスク専用のスタック領域
    TaskState state;
    uint8_t priority;
}TCB;

extern TCB task_table[MAX_TASKS];
extern uint8_t task_count;

// タスクの登録する関数
void task_create(void (*task_func)(void),uint8_t priority);