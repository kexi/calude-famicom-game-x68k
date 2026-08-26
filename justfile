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
cflags := "-m68000 -O2 -fomit-frame-pointer -ffreestanding -nostdlib -fno-builtin -fno-common -fno-pic -fno-PIC -fno-stack-protector -Wall -Wextra"

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

# ゲーム本体をビルドする。
#
# core/ はプラットフォーム非依存、platform/ が X68000 のハードを叩く。
# アセットは tools/ が生成した .inc.c を混ぜる。
game_srcs := "x68k/platform/crt0.S x68k/platform/main.c x68k/platform/video.c x68k/platform/input.c x68k/core/level.c x68k/core/player.c x68k/core/enemy.c x68k/core/arrow.c x68k/core/item.c x68k/core/boss.c x68k/core/game.c x68k/core/sound.c x68k/platform/audio.c x68k/platform/hud.c x68k/assets/levels.inc.c"

[doc('アセット (レベル・フォント) を生成する')]
assets:
    python3 x68k/tools/mklevels.py assets/levels.s x68k/assets/levels.inc.c

[doc('ゲーム本体 (GAME.X) をビルドする')]
build: assets
    mkdir -p {{build}}
    for f in {{game_srcs}}; do \
      o={{build}}/$(basename $f | tr '.' '_').o; \
      {{cross}}-gcc {{cflags}} -c $f -o $o || exit 1; \
    done
    {{cross}}-ld --emit-relocs -n -T x68k/ld/game.ld \
      -o {{build}}/game.elf {{build}}/*_S.o {{build}}/*_c.o \
      $({{cross}}-gcc -m68000 -print-libgcc-file-name)
    python3 x68k/tools/elf2x.py {{build}}/game.elf {{build}}/GAME.X

[doc('ゲームをエミュレータで走らせる')]
run *ARGS: build
    just image {{build}}/GAME.X
    {{emu}}/build-host/x68k-run --iplrom {{emu}}/rom/iplrom.dat \
      --hdd {{build}}/disk.hdf --cycles 900000000 --event-driven \
      --keys $'game\n' {{ARGS}}

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

# 窓を出して実際に遊ぶ。
#
# 操作: A/D または矢印 = 左右、K = ジャンプ、J = 矢、ESC = 終了。
#
# x68k-play はエミュレータ側にある。SDL2 が要るので、無い環境では
# エミュレータ側の just build-play が「作らない」と言って終わる。
[doc('窓を出してゲームを遊ぶ')]
play: build
    just image {{build}}/GAME.X
    cd {{emu}} && just build-play
    {{emu}}/build-host/x68k-play --iplrom {{emu}}/rom/iplrom.dat \
      --hdd {{justfile_directory()}}/{{build}}/disk.hdf --keys $'game\n'

# ───── テスト ──────────────────────────────────────────────────────────────

# core/ はプラットフォーム非依存の C なので、ホストの clang でそのまま
# ビルドしてテストできる。68000 に載せてエミュレータで回すより桁違いに速く、
# 失敗が「ロジックの誤り」か「載せ方の誤り」かを切り分けられる。
[doc('core/ のホストネイティブテストを実行する')]
test:
    mkdir -p {{build}}
    clang -std=c17 -O1 -g -Wall -Wextra -Werror \
      -o {{build}}/test x68k/test/test_main.c x68k/core/level.c \
      x68k/core/player.c x68k/core/enemy.c x68k/core/arrow.c \
      x68k/core/item.c x68k/core/boss.c x68k/core/game.c \
      x68k/core/sound.c x68k/assets/levels.inc.c
    ./{{build}}/test

# 全ステージが本当にクリアできるかを探索で確かめる。
#
# 「遊べる」の最低条件は、詰まずに最後まで行けること。手で台本を書いて
# 失敗したとき、それが「ゲームが詰んでいる」のか「台本が下手」なのかは
# 区別できない。探索させれば区別が付く。
[doc('全ステージ + ボスが通せることを探索で確かめる')]
solve:
    mkdir -p {{build}}
    clang -std=c17 -O2 -Wall -Wextra -o {{build}}/solve \
      x68k/test/solve.c x68k/core/level.c x68k/core/player.c \
      x68k/core/enemy.c x68k/core/arrow.c x68k/core/item.c \
      x68k/core/boss.c x68k/core/game.c x68k/core/sound.c x68k/assets/levels.inc.c
    ./{{build}}/solve

[doc('エミュレータ上で実際に動かして状態を検査する')]
e2e: build
    just image {{build}}/GAME.X
    X68K_STACKCHAN={{emu}} BUILD_DIR={{build}} bash x68k/test/e2e.sh

# ───── lint / format ───────────────────────────────────────────────────────

# 生成物 (*.inc.c) は対象から外す。
#
# Why: 形は生成側 (tools/) が決めている。整形器に通すと、生成した直後に
# 差分が出る状態になり、「生成し直したのに差分が出る」の意味が
# 「データが変わった」なのか「整形が違う」なのか区別できなくなる。
[doc('C とアセンブラを整形する')]
fmt:
    fd -e c -e h -E '*.inc.c' . x68k --exec clang-format -i

[doc('整形されているかを検査する (書き換えない)')]
fmt-check:
    fd -e c -e h -E '*.inc.c' . x68k --exec clang-format --dry-run --Werror

[doc('シークレットスキャンを全履歴に対して回す')]
gitleaks:
    gitleaks git --no-banner --redact

# ───── 後始末 ──────────────────────────────────────────────────────────────

[doc('ビルド成果物を消す')]
clean:
    rm -rf {{build}}
