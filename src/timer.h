#pragma once
#include <stdint.h>

// システムカウンタ
extern volatile uint32_t sys_tick;

// タイマの初期化
void timer_init();
// 現在のカウント数を取得
uint32_t timer_get_tick();
