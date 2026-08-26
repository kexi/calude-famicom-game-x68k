#!/usr/bin/env bash
# エミュレータ上で実際に動かし、状態の出力を期待値と突き合わせる。
#
# ネイティブテスト (x68k/test/test_main.c) はロジックだけを見る。
# こちらは「68000 のコードとして正しく動き、キー入力が届き、
# 画面の初期化が通る」ところまでを見る。両方が要る。
#
# ゲームは 30 フレームごとに "XXXX YYYY G A FFFF" を DOS _PRINT で出す。
# --dump-text はテキスト画面を CGROM の字形と照合して ASCII へ逆引きする。
# 改行が落ちてレコードが繋がり、先頭側の空白も 1 つ詰まるので、
# 見えるのは "012000168 1 100030..." のような並びになる。
# 期待値は実際に出た並びをそのまま使う。
set -euo pipefail

emu="${X68K_STACKCHAN:-$(dirname "$0")/../../../x68k-stackchan}"
build="${BUILD_DIR:-build-x68k}"

# テキスト画面を 1 本の文字列にして返す。行番号の桁は落とす。
run_game() {
    local keys="$1"
    "$emu/build-host/x68k-run" \
        --iplrom "$emu/rom/iplrom.dat" \
        --hdd "$build/disk.hdf" \
        --cycles 900000000 --event-driven \
        --keys "$keys" --dump-text 2>/dev/null |
        sed -n '/A>game/,/^----/p' | sed 's/^ *[0-9]*|//' | tr -d '\n\r'
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
check "挨拶が出る" "$out" "CALUDE KODO X68000"
# 初期状態は x=120 y=168 で接地・生存。
check "初期位置 x=120 y=168 接地 生存" "$out" "012000168 1 1"

echo "e2e: d キーで右へ動く"
out=$(run_game $'game\nddddddddddddddddddddd')
check "右へ移動している" "$out" "019200168 1 1"

echo "e2e: k キーでジャンプする"
out=$(run_game $'game\nkkkkkkkkkk')
# 跳んでいるフレームは y が 168 より小さく、接地フラグが 0。
#
# なぜ空白ごと照合するか: 空白を潰した並びに正規表現をかけると桁の
# 境目が分からず、跳んでいない実行でも一致するゆるい条件になる。
# 実際にそれを書いてしまい、跳ばない実行でも通ることを確かめてから直した。
if [[ "$out" =~ 01[0-6][0-9]\ 0\ 1 ]]; then
    echo "  ok   空中に居るフレームがある"
else
    echo "  FAIL 空中に居るフレームがある"
    echo "       y<170 かつ接地=0 のレコードが無い"
    fail=1
fi

if [[ $fail -ne 0 ]]; then
    echo "e2e: 失敗あり"
    exit 1
fi
echo "e2e: すべて成功"
