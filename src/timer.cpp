#include "timer.h"
#include <avr/interrupt.h>
#include <Arduino.h>

volatile uint32_t sys_tick = 0;

void timer_init() {
    // TIMER1をCTCモードで設定
    TCCR1A = 0;
    TCCR1B = 0;
    TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10);
    OCR1A = 249; // 1ms周期
    TIMSK1 = (1 << OCIE1A); // 比較一致の割り込み許可
    sei(); // 割り込み許可
}

ISR(TIMER1_COMPA_vect){
    sys_tick++;
}

uint32_t timer_get_tick(){
    uint32_t t;
    // tの値の取得がタイマー割り込み時と重複したときのための保護
    cli();
    t = sys_tick;
    sei();
    return t;
}