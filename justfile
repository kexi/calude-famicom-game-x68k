# 狩人行動 X68000 版のタスクランナー。
#
# 【方針】実行するコマンドはすべてここに集約する。生の gcc / python を
# 直接叩かない。CI もローカルも `just <task>` だけを呼ぶ
# (= コマンド文字列の単一情報源)。ツールは flake.nix の devShell が供給する。
#
# NES 版 (src/ assets/ Makefile) は凍結しており、こちらは触らない。

# 68000 のクロスツールチェーンの接頭辞。
cross := "m68k-unknown-linux-gnu"

# ビルド成果物の置き場。
build := "build-x68k"

# エミュレータ (x68k-stackchan) の場所。イメージ作成と実行に使う。
emu := env_var_or_default("X68K_STACKCHAN", justfile_directory() / ".." / "x68k-stackchan")

# 既定ターゲット: 引数なしなら一覧を出す。
default:
    @just --list

# ───── ビルド ──────────────────────────────────────────────────────────────

# 68000 のコンパイルオプション。
#
# -m68000       : 68030 以降の命令を出させない (X68000 の初代は 68000)
# -ffreestanding: libc がある前提を外す
# -nostdlib     : スタートアップも libc もリンクしない (crt0.S が担う)
# -fno-builtin  : memcpy 等へ勝手に置き換えさせない (libc が無いので落ちる)
# -fno-common   : 未初期化のグローバルを .bss へ確実に置く
# -fno-pic は必須。
#
# nixpkgs のクロス gcc は既定で PIC を有効にしており、そのままだと
# 絶対番地への参照が GOT (Global Offset Table) 経由になる。X 形式の
# 実行ファイルに GOT は無いので、実行時に GOT を引いた先が全部 0 になり、
# 関数ポインタが 0 になって「PC=$000002 で halt」という形で落ちる。
# X 形式は再配置表で絶対番地を直す仕組みなので、PIC は不要かつ有害。
cflags := "-m68000 -O2 -fomit-frame-pointer -ffreestanding -nostdlib -fno-builtin -fno-common -fno-pic -fno-PIC -Wall -Wextra"

# リンクは gcc ではなく ld を直に呼ぶ。
#
# Why not gcc にリンクまでさせるか: gcc のドライバは -nostdlib を付けても
# ターゲット既定のリンク指定 (PHDR の作り方を含む) を足すため、
# 自前のリンカスクリプトと衝突して
# "PHDR segment not covered by LOAD segment" で落ちる。
# ld を直に呼べばスクリプトの記述だけが効く。
[doc('hello.x をビルドする (ツールチェーンの貫通確認用)')]
build-hello:
    mkdir -p {{build}}
    {{cross}}-gcc {{cflags}} -c x68k/platform/crt0.S -o {{build}}/crt0.o
    {{cross}}-gcc {{cflags}} -c x68k/platform/hello.c -o {{build}}/hello.o
    {{cross}}-ld --emit-relocs -n -T x68k/ld/game.ld \
      -o {{build}}/hello.elf {{build}}/crt0.o {{build}}/hello.o
    python3 x68k/tools/elf2x.py {{build}}/hello.elf {{build}}/HELLO.X

# ───── ディスクイメージと実行 ──────────────────────────────────────────────

# 既存の起動可能イメージへ .X を入れた新しいイメージを作る。
#
# Human68k の配布物 (/tmp/h302 等) が手元に無くてもよいように、
# 既存の hdd0.hdf から HUMAN.SYS / COMMAND.X を抽出して組み直す。
[doc('.X を入れた SASI HDD イメージを作る')]
image X:
    python3 {{emu}}/tools/make_sasi_image.py inject \
      {{emu}}/rom/hdd0.hdf {{build}}/disk.hdf --add {{X}}

# --keys は「実際の改行」を含む文字列を渡す必要がある。
# just の "..." では \n がエスケープとして解釈されないので、
# シェルの $'...' を使って本物の改行を作る。
[doc('hello.x をエミュレータで走らせて結果を出す')]
run-hello: build-hello
    just image {{build}}/HELLO.X
    {{emu}}/build-host/x68k-run --iplrom {{emu}}/rom/iplrom.dat \
      --hdd {{build}}/disk.hdf --cycles 900000000 --event-driven \
      --keys $'hello\n' --dump-text

# ───── lint / format ───────────────────────────────────────────────────────

[doc('C とアセンブラを整形する')]
fmt:
    fd -e c -e h . x68k --exec clang-format -i

[doc('整形されているかを検査する (書き換えない)')]
fmt-check:
    fd -e c -e h . x68k --exec clang-format --dry-run --Werror

[doc('シークレットスキャンを全履歴に対して回す')]
gitleaks:
    gitleaks git --no-banner --redact

# ───── 後始末 ──────────────────────────────────────────────────────────────

[doc('ビルド成果物を消す')]
clean:
    rm -rf {{build}}
