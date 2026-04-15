# CreateRTOS ディスパッチコード解説

ATmega2560 上で動く自作プリエンプティブ RTOS の「タスク切替（ディスパッチ）」部分の解説です。

## 1. 全体像

```
[setup]
  └─ task_create(task1) ─┐
  └─ task_create(task2) ─┼─ 各タスク用のスタック領域に「初期フレーム」を仕込む
  └─ current_tcb = &task_table[0]
  └─ timer_init()          : 1ms 周期の TIMER1 割り込みを有効化
  └─ task_start()          : 最初のタスクに入る（戻ってこない）
                              │
                              ▼
[task1 実行中] ── 1ms ──▶ [TIMER1_COMPA_vect] ──▶ [task2 実行中] ── 1ms ──▶ ...
                          SAVE_CONTEXT
                          _ctx_sp ← 現 SP
                          do_switch()
                          SP ← _ctx_sp (次タスク)
                          RESTORE_CONTEXT
                          reti
```

主要な登場人物：

| 名前 | 役割 |
|---|---|
| `TCB` | タスク制御ブロック。スタック領域・SP・状態・優先度 |
| `task_table[MAX_TASKS]` | 全タスクの TCB 配列 |
| `current_tcb` | いま走っているタスクの TCB へのポインタ |
| `_ctx_sp` | ISR 内で SP をやり取りするための作業用変数 |
| `SAVE_CONTEXT` / `RESTORE_CONTEXT` | 全レジスタと SREG を push/pop するマクロ |

---

## 2. AVR のスタックと PC の扱い（前提知識）

ディスパッチコードを読むうえで必須の AVR 仕様：

- **スタックは高アドレス→低アドレスに伸びる**
- **SP は「次に push するときに書き込む空きスロット」を指す**
  - `PUSH`: `MEM[SP] ← Rr; SP--`
  - `POP` : `SP++; Rd ← MEM[SP]`
- **ATmega2560 は `__AVR_3_BYTE_PC__`（22-bit PC）**
  - 割り込み／CALL 時に PC は 3 バイト積まれる
  - `RETI` は以下の順で pop する（**MSB が先**）
    ```
    SP++; PC[21:16] ← STACK   (lowest addr)
    SP++; PC[15:8]  ← STACK
    SP++; PC[7:0]   ← STACK   (highest addr)
    ```
- **関数ポインタはワードアドレス**
  - `(uint32_t)task_func` はそのワードアドレスをゼロ拡張した値
  - reti でこの値が PC にロードされ、CPU は `PC × 2` のバイト番地にジャンプする

---

## 3. 初期スタックフレーム — `task_create`

`task_create` の目的は、**「タスクがさっきまで走っていて、TIMER1 ISR で中断された直後の状態」と全く同じメモリ配置を作っておく** こと。こうしておけば、`RESTORE_CONTEXT` + `reti` で自然にタスクに入れる。

### 積み方（高アドレス → 低アドレス）

```
stack[STACK_SIZE-1]  ┐
 PCL                 │   ← reti の 3 番目の pop = PC[7:0]
 PCH                 │   ← reti の 2 番目の pop = PC[15:8]
 PCE (=0)            │   ← reti の 1 番目の pop = PC[21:16]
 r0 (=0)             │   ← RESTORE_CONTEXT 最後の pop
 SREG (=0x80)        │   ← RESTORE_CONTEXT 最後から2番目の pop (I ビット有効)
 r1  (=0)            │
 r2  (=0)            │   ← 31 個並ぶ (r1〜r31)
  ...                │
 r31 (=0)            │   ← RESTORE_CONTEXT 最初の pop
stack_ptr ───────────┘   ← SP の初期値（空きスロット）
```

合計: 3 (PC) + 1 (r0) + 1 (SREG) + 31 (r1〜r31) = **36 バイト**

### 該当コード

```cpp
uint8_t *sp = &tcb->stack[STACK_SIZE - 1];   // 配列の一番後ろから積み始める
uint32_t word_addr = (uint32_t)task_func;

// PC 3バイト: MSB を最後=最低アドレスに置く
*sp-- = (uint8_t)(word_addr & 0xFF);         // PCL
*sp-- = (uint8_t)((word_addr >> 8) & 0xFF);  // PCH
*sp-- = (uint8_t)((word_addr >> 16) & 0xFF); // PCE

*sp-- = 0;      // r0
*sp-- = 0x80;   // SREG: I ビット立てて割り込み許可状態で復帰させる

for (int i = 0; i < 31; i++) *sp-- = 0;      // r1〜r31

tcb->stack_ptr = sp;   // SP のスタート地点（空きスロットを指す）
```

### ポイント

- **PC バイト順**: ATmega2560 の reti 仕様に合わせて、LSB を高アドレスに、MSB を低アドレスに置く。ここを逆にすると `0x00_ABCD` が `0xCD_AB_00` として解釈されて、flash 範囲外にジャンプ→リセットループになる（まさに詰まった箇所）。
- **SREG = 0x80**: ビット 7 (I) だけ立てた値。RESTORE 後にタスクは「割り込み許可状態」で動き出す。
- **レジスタは全部 0**: 新規タスクはまだ何も計算していないので初期値は何でもよい。ただし r1 は avr-gcc の `__zero_reg__` として常に 0 であるべきなので、必ず 0 にしておく。

---

## 4. 最初のタスクに飛び込む — `task_start` / `_task_start_sp`

```cpp
void task_start(void) {
    cli();
    _task_start_sp((uint16_t)current_tcb->stack_ptr);
}

__attribute__((naked, noinline, used))
static void _task_start_sp(uint16_t sp) {
    asm volatile(
        "out __SP_H__, r25 \n\t"   // SP = 引数 sp (r25:r24)
        "out __SP_L__, r24 \n\t"
    );
    RESTORE_CONTEXT();              // 32レジスタ + SREG を pop
    asm volatile("reti");           // PC 3バイトを pop してタスクへジャンプ
}
```

### やっていること

1. `cli()` で割り込み禁止（SP を書き換える間の安全確保）
2. 引数で渡された「task1 のスタック先頭」を SP にロード
3. `RESTORE_CONTEXT` で 33 バイトを pop して全レジスタと SREG を復元
4. `reti` で PC 3 バイトを pop → **task1 の先頭にジャンプ**、同時に I ビットを 1 に

### 小ネタ

- `_task_start_sp` は **naked 関数**：コンパイラがプロローグ／エピローグを生成しない。中身は asm だけなので、余計なスタック操作が入ると即バグる。
- `noinline, used` を付けているのは、`static` + 1 回しか呼ばれない関数だと GCC が勝手にインライン展開してしまい、`r25:r24` に引数が入っている前提が崩れる事故を防ぐため。
- `task_start()` はここから **戻ってこない**。`_task_start_sp` が reti でタスクへ飛んだら、帰り番地が載っていた「元のスタック」はもう参照されない。

---

## 5. プリエンプション — TIMER1 ISR

1ms ごとに TIMER1 COMPA 割り込みが発火して、ここで実際のタスク切替が起きる。

```cpp
ISR(TIMER1_COMPA_vect, ISR_NAKED) {
    SAVE_CONTEXT();                 // ① 現タスクの全レジスタを push

    asm volatile(                   // ② 現在の SP を _ctx_sp に保存
        "in  r0, __SP_L__  \n\t"
        "in  r1, __SP_H__  \n\t"
        "sts _ctx_sp,   r0 \n\t"
        "sts _ctx_sp+1, r1 \n\t"
        "clr r1            \n\t"   //   r1 を __zero_reg__ に戻す
        ::: "r0", "r1", "memory"
    );

    asm volatile("call do_switch \n\t" ::: "memory");   // ③ スケジューリング

    asm volatile(                   // ④ 次タスクの SP を _ctx_sp から SP へ
        "lds r0, _ctx_sp   \n\t"
        "lds r1, _ctx_sp+1 \n\t"
        "out __SP_H__, r1  \n\t"
        "out __SP_L__, r0  \n\t"
        ::: "r0", "r1", "memory"
    );

    RESTORE_CONTEXT();              // ⑤ 次タスクのレジスタを復元
    asm volatile("reti");           // ⑥ 次タスクの PC にジャンプ
}
```

### なぜ `ISR_NAKED` か

普通の ISR は GCC が自動で prologue/epilogue（レジスタ退避・復帰）を入れるが、それだと「ISR 内で SP を書き換えて別タスクのスタックに切り替える」のと相性が悪い。`ISR_NAKED` で自前管理する。

### 各ステップの詳細

**① `SAVE_CONTEXT()`**
r0, SREG, r1〜r31 の計 33 バイトを今走っているタスクのスタックに push。
SREG を保存する前に `cli` してあるので、以降 reti までは I=0。

**② SP → `_ctx_sp`**
SP を I/O レジスタから読んで、グローバル変数 `_ctx_sp` に書く。
`in r1, __SP_H__` で r1 が潰れるので、C 関数 `do_switch` を呼ぶ前に **必ず `clr r1` で 0 に戻す**（avr-gcc ABI 上 r1 は常に 0）。

**③ `call do_switch`**
```cpp
void do_switch() {
    sys_tick++;
    current_tcb->stack_ptr = (uint8_t*)_ctx_sp;  // 現タスクの SP を TCB に保存
    schedule_next();                             // current_tcb を更新
    _ctx_sp = (uint16_t)current_tcb->stack_ptr;  // 次タスクの SP を _ctx_sp へ
}
```
C で書ける部分は C で書く。`_ctx_sp` を介して「現 SP を受け取り → 次 SP を返す」インタフェースにしている。

**④ `_ctx_sp` → SP**
`do_switch` が `_ctx_sp` に入れてくれた次タスクの SP を、実際の SP レジスタに書く。
**この瞬間、スタックは次タスクのものに切り替わる**。

**⑤ `RESTORE_CONTEXT()`**
切り替わった先のスタックから 33 バイト pop。r31 から順に r0 まで戻し、SREG も復元。

**⑥ `reti`**
スタックから 3 バイト pop して PC に。同時に I ビット = 1。
→ **次タスクが「中断された場所」から再開**（新規タスクなら `task_create` で仕込んだ PC=タスク関数先頭へ）。

---

## 6. スケジューラ — `schedule_next`

```cpp
void schedule_next(void) {
    task_table[current_task].state = TASK_READY;
    current_task = (current_task + 1) % task_count;
    task_table[current_task].state = TASK_RUNNING;
    current_tcb = &task_table[current_task];
}
```

単純なラウンドロビン。優先度は今は未使用。

---

## 7. よくある詰まりどころ

| 症状 | 原因 |
|---|---|
| `initialize` がループする（リセットループ） | PC のバイト順が逆。MSB を最低アドレスに置く |
| ISR 有効化で動作が壊れる | ISR 内で r1 を潰したまま C 関数を呼んでいる (`clr r1` 忘れ) |
| 1 タスクでは動くが 2 タスク目に切り替わらない | `_ctx_sp` の経由を忘れて SP を直接書いている／`SAVE/RESTORE` のバイト数不一致 |
| ランダムに暴走 | `STACK_SIZE` 不足。`digitalWrite` や `Serial.println` はスタックを結構食う |

---

## 8. データの流れ（切替 1 回分）

```
[task1 が動いている]
  SP ─▶ task_table[0].stack の途中

  ⟶ TIMER1 割り込み発生、ハードウェアが PC(3B) を push、I=0

[ISR: TIMER1_COMPA_vect]
  ① SAVE_CONTEXT: task1 のスタックに 33B push
  ② _ctx_sp ← SP (= task1 スタックの最下点)
  ③ do_switch:
       task_table[0].stack_ptr ← _ctx_sp    (task1 の SP を保存)
       current_task = 1
       current_tcb = &task_table[1]
       _ctx_sp ← task_table[1].stack_ptr    (task2 の SP を取り出し)
  ④ SP ← _ctx_sp                            (SP が task2 のスタックに)
  ⑤ RESTORE_CONTEXT: task2 のスタックから 33B pop
  ⑥ reti: task2 のスタックから PC 3B pop → task2 に復帰

[task2 が動く]
```

次の割り込みでは逆方向に同じことが起きる。これを 1ms ごとに繰り返すのがプリエンプティブマルチタスクの正体。

---

# 付録: AVR アセンブラ超入門（このコードを読むのに必要な分だけ）

## A. 登場人物

### 汎用レジスタ: `r0` 〜 `r31`
AVR には 8bit の汎用レジスタが 32 本ある。CPU が計算するときは全部このレジスタ上で行い、RAM は別途 load/store する。

- `r0`, `r1`: コンパイラの予約
  - `r0`: 一時レジスタ（scratch）
  - `r1`: 常に 0 (`__zero_reg__`)。C 関数を呼ぶ前は 0 である必要がある
- `r2`〜`r31`: 汎用
- `r24`〜`r25`: 関数の第1引数 (16bit 値を r25:r24 で受ける。r25=上位、r24=下位)

### 特殊な I/O レジスタ
AVR にはレジスタとは別に「I/O 空間」があって、そこに `SP`（スタックポインタ）や `SREG`（ステータスレジスタ）が置かれている。

| 名前 | 意味 |
|---|---|
| `__SP_L__` | スタックポインタ下位 8bit |
| `__SP_H__` | スタックポインタ上位 8bit |
| `__SREG__` | ステータスレジスタ (I, T, H, S, V, N, Z, C ビット)。最上位の I ビットが「割り込み許可」 |

これらは **`in` / `out` 命令**でしかアクセスできない（通常の `mov` では触れない）。

---

## B. このコードで使う命令一覧

| 命令 | 動作 | 例 |
|---|---|---|
| `push Rr` | `MEM[SP] ← Rr; SP--` スタックに 1 バイト積む | `push r0` |
| `pop Rd` | `SP++; Rd ← MEM[SP]` スタックから 1 バイト取り出す | `pop r31` |
| `in Rd, io` | I/O レジスタ → 汎用レジスタ | `in r0, __SREG__` |
| `out io, Rr` | 汎用レジスタ → I/O レジスタ | `out __SP_H__, r25` |
| `lds Rd, addr` | RAM の 16bit 番地から 1 バイト読む | `lds r0, _ctx_sp` |
| `sts addr, Rr` | RAM の 16bit 番地に 1 バイト書く | `sts _ctx_sp, r0` |
| `clr Rd` | `Rd ← 0` (実体は `eor Rd, Rd`) | `clr r1` |
| `cli` | 割り込み禁止 (SREG の I ビットを 0 に) | `cli` |
| `sei` | 割り込み許可 (SREG の I ビットを 1 に) | `sei` |
| `call addr` | サブルーチン呼び出し。PC を push して addr へジャンプ | `call do_switch` |
| `reti` | 割り込みから復帰。PC を pop してジャンプ＋ I ビットを 1 に | `reti` |

**`push`/`pop` が「SP の意味」を定義している** のが重要。  
AVR の SP は「**次に push する空きスロット**」を指しており、

```
push: まず書いて、SP を下げる  (MEM[SP] ← Rr; SP--)
pop : まず SP を上げて、読む   (SP++; Rd ← MEM[SP])
```

だから push と pop の回数さえ合っていれば SP は元に戻る。

---

## C. `asm volatile(...)` の読み方

C/C++ の中にアセンブラを埋め込む GCC のインラインアセンブリ。

```cpp
asm volatile(
    "命令1 \n\t"        // 文字列を連結して 1 つのアセンブリブロックになる
    "命令2 \n\t"
    ::: "r0", "r1", "memory"   // このブロックで「壊す」リソースの宣言
);
```

- `\n\t`: 出力される .s ファイルで改行＋タブにするためのお作法
- `volatile`: 「最適化で消したり並び替えたりするな」の意思表示
- 末尾の `::: "clobber"`: **コンパイラに「この中で r0/r1 の値を破壊するよ。memory も書き換えるよ」と伝える**。これを書かないとコンパイラが r0/r1 に別の値が入ったままだと誤解する

---

## D. 各アセンブラブロックの逐行解説

### D-1. `SAVE_CONTEXT` マクロ（context.h）

現在走っているタスクの全レジスタ + SREG をスタックに退避する。

```
push r0               ; r0 の元の値を保存
in   r0, __SREG__     ; r0 に SREG を読む（I/T/Z/C などのフラグ）
cli                   ; 割り込み禁止（以降 reti まで安全に操作）
push r0               ; さっき読んだ SREG をスタックに保存
push r1               ; r1 の元の値を保存
clr  r1               ; r1 = 0 に戻す（avr-gcc の __zero_reg__ 契約）
push r2
push r3
 ...
push r31              ; r2〜r31 を全部スタックに
```

**合計 33 バイトを push**（r0, SREG, r1, r2, …, r31）。

なぜ SREG をこの順で？
- `in r0, __SREG__` で r0 に SREG を入れる → r0 の「元の値」はもう push 済みなので潰しても OK
- その後 `cli` するが、先にコピー済みなので「cli する前の I ビット状態」が保存される
  → 後で復元すれば「この関数に入る直前の割り込み状態」に戻せる

### D-2. `RESTORE_CONTEXT` マクロ

`SAVE_CONTEXT` の逆順で全部戻す。

```
pop  r31              ; 後に push したものから先に pop（スタックは LIFO）
pop  r30
 ...
pop  r1
pop  r0               ; ← これは「SREG だった値」が入る
out  __SREG__, r0     ; SREG に書き戻す → I ビットもここで戻る
pop  r0               ; ← これが「r0 の元の値」
```

**合計 33 バイトを pop**。`SAVE_CONTEXT` と 1 対 1 対応している。

---

### D-3. `_task_start_sp` の中身（task.cpp）

```cpp
__attribute__((naked, noinline, used))
static void _task_start_sp(uint16_t sp) {
    asm volatile(
        "out __SP_H__, r25 \n\t"
        "out __SP_L__, r24 \n\t"
    );
    RESTORE_CONTEXT();
    asm volatile("reti");
}
```

**引数 `sp` は r25:r24 に入っている**（AVR C 呼び出し規約。16bit 値は r25 が上位、r24 が下位）。

| 行 | 動作 |
|---|---|
| `out __SP_H__, r25` | SP の上位バイトに r25（引数の上位）を書く |
| `out __SP_L__, r24` | SP の下位バイトに r24（引数の下位）を書く |

この 2 命令で、**CPU のスタックポインタが「task1 の TCB の中にある初期フレームのてっぺん」に切り替わる**。以降の push/pop は全部 task1 のスタック上で起きる。

そのあと `RESTORE_CONTEXT` で 33 バイト pop、`reti` で PC 3 バイト pop → task1 先頭へジャンプ。

> 💡 なぜ `cli` が要らないか？
> 呼び出し側 `task_start()` で既に `cli()` している。SP を 2 回に分けて書く間に割り込みが入ると SP が半端な値になって事故るので、SP 書き換え時は必ず割り込み禁止にする。

---

### D-4. TIMER1 ISR の asm ブロック（timer.cpp）

#### ブロック ①: 現在の SP を `_ctx_sp` に保存

```asm
in  r0, __SP_L__       ; r0 ← SP 下位
in  r1, __SP_H__       ; r1 ← SP 上位 （※これで r1 の 0 が壊れる！）
sts _ctx_sp,   r0      ; RAM[_ctx_sp+0] ← r0  （_ctx_sp は uint16_t、リトルエンディアンで下位が先）
sts _ctx_sp+1, r1      ; RAM[_ctx_sp+1] ← r1  （上位バイト）
clr r1                 ; r1 = 0 に戻す ← これがないと次の do_switch が壊れる
```

- **なぜ `_ctx_sp` と `_ctx_sp+1` に分けて書くか**：AVR の `sts` は 1 バイトしか書けないので、16bit 値は 2 回に分ける
- **なぜ下位が先か**：AVR はリトルエンディアン。`uint16_t _ctx_sp` のメモリ配置は `[下位バイト][上位バイト]` の順
- **`clr r1` を忘れるとどうなるか**：
  - avr-gcc は「r1 は常に 0」と仮定してコードを生成する（例：`adc rX, r1` で r1 を 0 として加算キャリーだけ足す、など）
  - r1 に SP 上位バイトが入ったまま `call do_switch` すると、do_switch 内の加算やポインタ計算がすべて狂う
  - 今回の「timer_init 有効化で壊れる」のが典型症状

#### ブロック ②: `call do_switch`

```asm
call do_switch
```

ただの関数呼び出し。`call` は
1. 戻り番地 (PC+4 や PC+2) を 3 バイト push
2. `do_switch` の先頭にジャンプ

`do_switch` の中で `_ctx_sp` が「次に走らせるべきタスクの SP」に書き換わる。  
終了時 `ret` で戻ってきて、スタックから戻り番地が pop される（push と pop が対称なので SP は元通り）。

#### ブロック ③: `_ctx_sp` から次タスクの SP をロード

```asm
lds r0, _ctx_sp        ; r0 ← RAM[_ctx_sp+0]（下位バイト）
lds r1, _ctx_sp+1      ; r1 ← RAM[_ctx_sp+1]（上位バイト）
out __SP_H__, r1       ; SP 上位 ← r1
out __SP_L__, r0       ; SP 下位 ← r0
```

**この 4 命令の 3 行目が実行された瞬間、スタックは次タスクのものに切り替わっている**。

`r0`/`r1` が破壊されるが、直後の `RESTORE_CONTEXT` で `pop r1`（新タスクの r1）・`pop r0`（新タスクの r0 兼 SREG）によって新タスクの値で上書きされるので問題ない。

#### ブロック ④: 復帰

```
RESTORE_CONTEXT() → 新タスクのスタックから 33 バイト pop して全レジスタ復元
reti              → 3 バイト pop して PC を新タスクの続きへ＋割り込み許可
```

---

## E. 全体のタイムライン（アセンブラ視点）

task1 → task2 に切り替わる 1 回分を命令レベルで追う：

```
; ── task1 実行中 ──
; (タスクのコードが動いている)

; 1ms 経過 → TIMER1 COMPA 割り込み発火
; ハードウェアが PC(3B) を push、I=0 にして ISR へジャンプ

; ── ISR 突入（SP は task1 のスタックを指している）──
push r0
in   r0, __SREG__
cli                    ; ← でも既にハードウェアが I=0 にしてる
push r0                ; SREG
push r1
clr  r1
push r2
 ...
push r31               ; ここまでで task1 のスタックに 33B 積んだ

in   r0, __SP_L__      ; r0 ← SP_L（task1 スタックの最下点）
in   r1, __SP_H__      ; r1 ← SP_H
sts  _ctx_sp,   r0     ; _ctx_sp に保存
sts  _ctx_sp+1, r1
clr  r1                ; r1 を 0 に戻す

call do_switch
    ; do_switch 内:
    ;   sys_tick++;
    ;   task_table[0].stack_ptr = _ctx_sp;   ← task1 の SP を TCB に記録
    ;   schedule_next(): current_tcb = &task_table[1]
    ;   _ctx_sp = task_table[1].stack_ptr;   ← task2 の SP を取り出す
    ; ret

lds  r0, _ctx_sp       ; r0 ← _ctx_sp（今は task2 の SP）
lds  r1, _ctx_sp+1
out  __SP_H__, r1      ; ← SP がここで task2 のスタックに切り替わる！！
out  __SP_L__, r0

pop  r31               ; 以降 task2 のスタックから pop
pop  r30
 ...
pop  r1
pop  r0                ; ← task2 の SREG だった値
out  __SREG__, r0
pop  r0                ; ← task2 の r0

reti                   ; task2 のスタックから PC(3B) pop → task2 の続きへジャンプ
                       ; と同時に I=1
; ── task2 実行中 ──
```

ポイントは、**「SP を 1 本書き換えるだけで全てが別タスクのスタックに切り替わる」**こと。レジスタは全部スタックに退避してあるので、SP さえ繋ぎ替えれば、コンテキストはそのスタックの中身で決まる。

これが「コンテキストスイッチ」の正体。
