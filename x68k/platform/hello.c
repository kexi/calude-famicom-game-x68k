// SPDX-License-Identifier: MIT
//
// ツールチェーンの貫通を確かめるためだけのプログラム。
//
// 確かめること:
//   1. gcc m68k でコンパイルしたコードが .X になり、Human68k が読み込めること
//   2. 読み込んだ先で再配置が効いていること (文字列の番地は絶対参照)
//   3. DOS コール (F-line) が通ること
//   4. bss が 0 で埋まっていること
//
// ゲーム本体はここから始まるわけではない。経路が通ることの証明が仕事。

void _dos_print(const char *s);

// bss に置かれる。crt0 がゼロ埋めしていなければ 0 にならない。
//
// volatile を付けるのは、コンパイラに「中身は 0 のはず」と決め打ちさせない
// ため。付けないと「静的変数の初期値は 0 だから合計も 0」と畳み込まれ、
// 変数そのものが消えて bss が空になる (= 何も検査しないテストになる)。
static volatile int g_zero_check[16];

// data に置かれる。再配置が効いていなければ、この文字列を指すポインタが
// 壊れて別の場所を印字する。
static const char *const g_greeting = "HELLO FROM X68000\r\n";

int main(void)
{
    _dos_print(g_greeting);

    int sum = 0;
    for (int i = 0; i < 16; ++i)
    {
        sum += g_zero_check[i];
    }

    // bss が 0 なら sum は 0。ここが 0 でなければ crt0 のゼロ埋めが
    // 効いていない (= 静的変数の初期値を信用できない状態)。
    _dos_print(sum == 0 ? "BSS OK\r\n" : "BSS NG\r\n");

    return 0;
}
