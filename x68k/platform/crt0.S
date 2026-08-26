| SPDX-License-Identifier: MIT
|
| Human68k の X 形式実行ファイルのスタートアップ。
|
| Human68k はプログラムを「メモリ管理ブロックの直後」へ読み込み、
| A0 にその管理ブロック、A1 にコマンドラインの末尾、A2 に環境を入れて
| 先頭から実行を始める。ここでは以下だけをやる:
|
|   1. bss をゼロで埋める
|   2. main() を呼ぶ
|   3. 戻ってきたら DOS _EXIT2 で終了コードを返す
|
| Why not スタックを自前で張り替えないか: Human68k は実行前にスタックを
| 用意して A7 に入れてくれる。張り替えると、確保していない領域を
| 踏む危険を自分で背負うことになる。
|
| Why not ここで _SUPER (スーパーバイザ移行) をするか: ハードウェアを
| 直に叩くときだけ要るもので、DOS コールしか使わない間は不要。
| 必要になった時点で C 側から呼ぶ方が、どの区間が特権付きかが読める。
|
| DOS コールは TRAP ではなく F-line 命令 (.dc.w $FF00+n) で発行する。
| Human68k の F-line ハンドラが「例外を起こした命令語そのもの」を読んで
| コール番号を得るため。TRAP #15 は IOCS 用で、別物。

    | 実行可能スタックは要らない。付けないとリンカが警告を出す。
    .section .note.GNU-stack,"",%progbits

    | crt0 は専用セクションに置く。リンカスクリプトがこれを text の
    | 先頭へ固定するので、実行開始位置が「イメージの先頭」で決まる。
    |
    | Why not .text.startup を使わないか: gcc が main を .text.startup へ
    | 置くことがあり、リンク順によっては main が crt0 より前に来る。
    | 実際それが起きて、エントリが 0 にならなかった。
    .section .text.crt0,"ax",%progbits
    .globl  _start
    .globl  _dos_exit2
    .globl  _dos_print
    .globl  _dos_super

_start:
    | bss をゼロで埋める。
    |
    | X 形式のヘッダは bss のサイズを持つが、中身はファイルに入らない。
    | Human68k が確保だけして内容を保証しないので、C の
    | 「初期化なしの静的変数は 0」という前提はここで自分で作る。
    | .l を付けて絶対ロングのアドレッシングにする。
    |
    | Why not `lea _bss_start, %a0` と書かないか: gas は既定で PC 相対に
    | しようとし、リンク時に「PC 相対を絶対へ変換できない」で落ちる。
    | ここで欲しいのは絶対番地への参照そのもの (= 再配置表に載る参照) なので、
    | 明示的に絶対ロングを指定する。
    lea     _bss_start.l, %a0
    lea     _bss_end.l, %a1
1:  cmpa.l  %a1, %a0
    bcc     2f
    clr.b   (%a0)+
    bra     1b
2:

    | main() を呼ぶ。引数は取らない。
    jsr     main.l

    | 戻り値 (d0) をそのまま終了コードにして Human68k へ返す。
    move.w  %d0, -(%sp)
    .dc.w   0xFF4C                  | DOS _EXIT2

| --- DOS コールの薄いラッパ ------------------------------------------------
|
| C から呼べるようにしておく。引数はスタック渡し (m68k の既定の ABI では
| 4(%sp) が第 1 引数)。

| void _dos_print(const char *s);
_dos_print:
    move.l  4(%sp), -(%sp)
    .dc.w   0xFF09                  | DOS _PRINT
    addq.l  #4, %sp
    rts

| int _dos_super(int stack);  0 を渡すとスーパーバイザへ移行する。
| 戻り値は「元の USP」で、これを渡し返すとユーザモードへ戻る。
_dos_super:
    move.l  4(%sp), -(%sp)
    .dc.w   0xFF20                  | DOS _SUPER
    addq.l  #4, %sp
    rts

| void _dos_exit2(int code);
_dos_exit2:
    move.w  6(%sp), -(%sp)
    .dc.w   0xFF4C                  | DOS _EXIT2
