#include "timer.h"
#include "scheduler.h"
#include "context.h"
#include "task.h"
#include <avr/interrupt.h>
#include <Arduino.h>

volatile uint32_t sys_tick = 0;
volatile uint16_t _ctx_sp; // SP受け渡し用

// スケジュール
extern "C" void do_switch() __attribute__((used, noinline));
void do_switch(){
    sys_tick++;
    current_tcb->stack_ptr = (uint8_t *)_ctx_sp; // 現在のタスクのSPを保存
    schedule_next();                             // 次のタスクを選ぶ
    _ctx_sp = (uint16_t)current_tcb->stack_ptr;  // 次のタスクのSPを取得
}

void timer_init() {
    // TIMER1をCTCモードで設定
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1 = 0;
    TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10);
    OCR1A = 249; // 1ms周期
    TIMSK1 = (1 << OCIE1A); // 比較一致の割り込み許可
    sei(); // 割り込み許可
}

uint32_t timer_get_tick(){
    uint32_t t;
    // tの値の取得がタイマー割り込み時と重複したときのための保護
    cli();
    t = sys_tick;
    sei();
    return t;
}

ISR(TIMER1_COMPA_vect, ISR_NAKED) {
    SAVE_CONTEXT();

    // 現在のSPを_ctx_spに保存
    asm volatile(
        "in  r0, __SP_L__  \n\t"
        "in  r1, __SP_H__  \n\t"
        "sts _ctx_sp,   r0 \n\t"
        "sts _ctx_sp+1, r1 \n\t"
        "clr r1            \n\t"   // __zero_reg__ を 0 に戻してから C を呼ぶ
        ::: "r0", "r1", "memory"
    );

    asm volatile("call do_switch \n\t" ::: "memory");   // スケジューリング（_ctx_spが次タスクのSPに更新される）

    // 次タスクのSPをロード
    asm volatile(
        "lds r0, _ctx_sp   \n\t"
        "lds r1, _ctx_sp+1 \n\t"
        "out __SP_H__, r1  \n\t"
        "out __SP_L__, r0  \n\t"
        ::: "r0", "r1", "memory"
    );

    RESTORE_CONTEXT();
    asm volatile("reti");
}