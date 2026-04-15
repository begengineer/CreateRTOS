#pragma once
#include "task.h"

extern TCB * volatile current_tcb;
extern volatile uint8_t current_task;

// 次のタスクを選んでcurrent_task / current_tcbを更新する
void schedule_next(void);