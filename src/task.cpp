#include "task.h"
#include "context.h"
#include "scheduler.h"
#include "timer.h"   // sys_tick を参照するため

TCB task_table[MAX_TASKS];
uint8_t task_count = 0;

void task_create(void (*task_func)(void),uint8_t priority){
    if(task_count >= MAX_TASKS) return;

    TCB *tcb = &task_table[task_count];
    tcb->state = TASK_READY;
    tcb->priority = priority;

    // スタックをゼロクリア
    for(int i = 0;i < STACK_SIZE;i++){
        tcb->stack[i] = 0;
    }

    // スタックトップから初期フレームを組み立てる
    // AVRはスタックが高アドレス→低アドレスに伸びる
    uint8_t *sp = &tcb->stack[STACK_SIZE -1];

    // タスク巻子のワードアドレス
    uint32_t word_addr = (uint32_t)task_func;

    // ATmega2560 (22-bit PC): reti は lowest addr から PC[21:16] を pop する
    // → push 順は PCL(低) → PCH(中) → PCE(高) で、MSB を最後=最低アドレスに置く
    *sp-- = (uint8_t)(word_addr & 0xFF);        // PCL  (highest addr)
    *sp-- = (uint8_t)((word_addr >> 8) & 0xFF); // PCH
    *sp-- = (uint8_t)((word_addr >> 16) & 0xFF);// PCE  (lowest addr)

    *sp-- = 0; // r0の元の値
    *sp-- = 0x80; // SREG(Iビット₌割り込み有効)

    // R0~R31を0で積む
    for(int i = 0;i < 31;i++) *sp-- = 0;

    tcb->stack_ptr = sp; // 復元開始ポイント

    task_count++;
}

// AVR呼び出し規約
__attribute__((naked, noinline, used))
static void _task_start_sp(uint16_t sp) {
    asm volatile(
        "out __SP_H__, r25 \n\t"
        "out __SP_L__, r24 \n\t"
    );
    RESTORE_CONTEXT();
    asm volatile("reti");
}

void task_start(void) {
    cli();
    _task_start_sp((uint16_t)current_tcb->stack_ptr);
}

// 現タスクを ms ミリ秒ブロックする
//   1) wake_tick と state=BLOCKED をアトミックに書く
//   2) 次の TIMER1 ISR で schedule_next が自分をスキップして他タスクに切替
//   3) wake_tick 到達後に schedule_next が state=READY→RUNNING に戻す
//   4) 再度スケジュールされるとここの while を抜けて return
void task_sleep(uint32_t ms) {
    if (ms == 0) return;

    cli();
    current_tcb->wake_tick = sys_tick + ms;
    current_tcb->state = TASK_BLOCKED;
    sei();

    // ここで ISR に拾ってもらい他タスクに切替えてもらう。
    // 起床すると current_tcb->state は RUNNING に変わっている。
    while (current_tcb->state == TASK_BLOCKED) {
        // 割り込みで切替わるのを待つ (最大 1 tick = 1ms)
    }
}