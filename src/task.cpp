#include "task.h"

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
    uint32_t word_addr = (uint32_t)task_func >> 1;

    *sp-- = (uint8_t)(word_addr & 0xFF);        // PCL
    *sp-- = (uint8_t)((word_addr >> 8) & 0xFF); // PCH
    *sp-- = (uint8_t)((word_addr >> 16) & 0xFF);// PCE

    *sp-- = 0x80; // SREG(Iビット₌割り込み有効)

    // R0~R31を0で積む
    for(int i = 0;i < 32;i++) *sp-- = 0;

    tcb->stack_ptr = sp; // 復元開始ポイント

    task_count++;
}