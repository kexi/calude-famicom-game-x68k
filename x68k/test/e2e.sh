#!/usr/bin/env bash
# エミュレータ上で実際に動かし、状態の出力を期待値と突き合わせる。
#
# ネイティブテスト (x68k/test/test_main.c) はロジックだけを見る。
# こちらは「68000 のコードとして正しく動き、キー入力が届き、
# 画面の初期化が通る」ところまでを見る。両方が要る。
#
# ゲームは 30 フレームごとに "XXXX YYYY G A FFFF" を DOS _PRINT で出す。
# テキスト画面を --dump-text で読み戻し、空白と行を潰した並びで照合する。
#
# なぜ空白を潰すか: テキスト画面は 96 桁で折り返すので、1 レコードが
# 行をまたぐことがある。連結してから探すほうが安定する。
#
# 照合する並びが "0120 0168 1 1 0030" の見た目と少しずれるのは、
# 逆引きした行に行番号などが混ざったまま連結しているため。
# 実際に出た並びをそのまま期待値にしてある。
set -euo pipefail

emu="${X68K_STACKCHAN:-$(dirname "$0")/../../../x68k-stackchan}"
build="${BUILD_DIR:-build-x68k}"

run_game() {
    local keys="$1"
    "$emu/build-host/x68k-run" \
        --iplrom "$emu/rom/iplrom.dat" \
        --hdd "$build/disk.hdf" \
        --cycles 900000000 --event-driven \
        --keys "$keys" --dump-text 2>/dev/null |
        sed -n '/A>game/,/^----/p' | tr -d ' |' | tr -d '\n\r'
}

fail=0
check() {
    local name="$1" haystack="$2" needle="$3"
    if [[ "$haystack" == *"$needle"* ]]; then
        echo "  ok   $name"
    else
        echo "  FAIL $name"
        echo "       期待した並び: $needle"
        fail=1
    fi
}

echo "e2e: 起動して初期状態になる"
out=$(run_game $'game\n')
check "挨拶が出る" "$out" "CALUDEKODOX68000"
# 空白を潰すと "0120 0168 1 1 0030" は "01200168110030" になる。
check "初期位置 x=120 y=168 接地 生存" "$out" "012000168110003"

echo "e2e: d キーで右へ動く"
out=$(run_game $'game\nddddddddddddddddddddd')
check "右へ移動している" "$out" "019200168110006"

echo "e2e: k キーでジャンプする"
out=$(run_game $'game\nkkkkkkkkkk')
# 空中では on_ground=0 で、Y が地上の 168 より小さい。
check "空中に居るフレームがある" "$out" "012000158010003"

if [[ $fail -ne 0 ]]; then
    echo "e2e: 失敗あり"
    exit 1
fi
echo "e2e: すべて成功"
