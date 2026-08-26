/* Human68k の X 形式実行ファイルを作るためのリンカスクリプト。
 *
 * X 形式は「text / data / bss のサイズ」と「再配置表」をヘッダに持ち、
 * Human68k が実行時に空いている番地へ読み込んで再配置する。
 * したがってリンク時のベースは 0 でよく、絶対番地への参照は
 * すべて再配置表に載せて elf2x.py がヘッダへ書き出す。
 *
 * Why not ベースを固定して再配置なしにするか:
 *   Human68k がどこへ読み込むかはメモリの空き具合で決まる。
 *   固定を前提にすると、その番地が空いていない環境で動かなくなる。
 *   再配置表方式が X 形式の正道で、tools/make_sasi_image.py の
 *   XFile クラスが既にこの形式を解釈できる (= 検証手段が手元にある)。
 *
 * セクションの順序は text -> data -> bss。X 形式のヘッダが
 * この 3 つのサイズを順に持つので、並びを変えると変換できない。
 */

OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)

SECTIONS
{
    /* ELF ヘッダとプログラムヘッダのぶんだけ空ける。
     *
     * Why not 0 から始めないか: 0 に置くと ELF 自身のヘッダを置く場所が
     * 無くなり、ld が "PHDR segment not covered by LOAD segment" で落ちる。
     * ここで空けた番地は ELF の内部事情でしかなく、X 形式へ変換するときは
     * text の先頭を 0 とみなして詰め直すので、実行時の番地には影響しない。 */
    . = SIZEOF_HEADERS;
    . = ALIGN(4);
    _text_start = .;

    /* crt0.o を先頭に置く。X 形式には「エントリポイント」の欄があるが、
     * 先頭から実行が始まる作りにしておくと elf2x 側の検証が単純になる。 */
    .text : {
        /* crt0 を必ず先頭に置く。実行はイメージの先頭から始まる。 */
        *(.text.crt0)
        *(.text.startup)
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(2);
    }

    _text_end = .;

    .data : {
        *(.data)
        *(.data.*)
        /* small data。-fno-common でも gcc が .sdata へ置くことがある。 */
        *(.sdata)
        *(.sdata.*)
        . = ALIGN(2);
    }

    _data_end = .;

    .bss : {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
        . = ALIGN(2);
        _bss_end = .;
    }

    /* ELF が付ける、実行に要らないもの。X 形式には持っていかない。 */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.debug*)
    }
}
