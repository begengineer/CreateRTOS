#pragma once
#include <stdint.h>

#define MAX_TASKS 4
#define STACK_SIZE 512 // 1タスクあたりのスタックサイズ

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
    volatile TaskState state;     // ISRとユーザコードで共有するのでvolatile
    uint8_t priority;
    volatile uint32_t wake_tick;  // TASK_BLOCKED中にこのtickに達したら起きる
}TCB;

extern TCB task_table[MAX_TASKS];
extern uint8_t task_count;

// タスクの登録する関数
void task_create(void (*task_func)(void),uint8_t priority);
// タスク開始関数
void task_start(void);
// 現タスクを ms ミリ秒ブロックする
void task_sleep(uint32_t ms);