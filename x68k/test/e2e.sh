#!/bin/bash
# エミュレータ上で実際に動かし、HUD に出した内部状態を確かめる。
#
# 3 層の検証のうち、これが一番外側:
#   just test   ゲームの規則 (ホストで直接)
#   just solve  全ステージが通せるか (探索)
#   just e2e    68000 のコードとして動き、入力に反応するか  <- ここ
#
# just render-runner が既存の描画器でBG・スプライトを合成する。
# 検証ビルド専用のHUDからも座標・接地・生存を読み、入力結果を照合する。
set -euo pipefail

emu="${X68K_STACKCHAN:-$(dirname "$0")/../../../x68k-stackchan}"
build="${BUILD_DIR:-build-x68k}"
here="$(cd "$(dirname "$0")/../.." && pwd)"

shot() {
    local out="$1" cycles="$2" keys="$3"
    "$build/x68k-render-run" \
        --iplrom "$emu/rom/iplrom.dat" \
        --hdd "$build/disk.hdf" \
        --cycles "$cycles" --event-driven \
        --keys "$keys" --ppm "$out" >/dev/null 2>&1
}

fail=0

echo "e2e: GAME.X はタイトルで入力を待つ"
shot /tmp/e2e-title.ppm 390000000 $'game\n'
if ! python3 "$here/x68k/tools/checkppm.py" /tmp/e2e-title.ppm --expect title; then
    fail=1
fi

# Human68kでGAME.Xを起動し、タイトルが出た後の400Mサイクル付近で
# STARTを押す。qはゲームが使わないため、時刻調整だけに使える。
start_keys=$'game\n'
for ((i = 0; i < 15; ++i)); do
    start_keys+=q
done
start_keys+=$'\n'

echo "e2e: START後に原作のラウンド画面を表示する"
shot /tmp/e2e-round.ppm 430000000 "$start_keys"
if ! python3 "$here/x68k/tools/checkppm.py" /tmp/e2e-round.ppm --expect round; then
    fail=1
fi

echo "e2e: 起動して 1-1 の初期状態になる"
shot /tmp/e2e-boot.ppm 480000000 "$start_keys"
if ! python3 "$here/x68k/tools/checkppm.py" /tmp/e2e-boot.ppm --expect player,ground,hud; then
    fail=1
fi
state=$(python3 "$here/x68k/tools/readhud.py" /tmp/e2e-boot.ppm | head -1)
if [[ "$state" == "0120 0168 1 1 1 3 0"* ]]; then
    echo "  ok   x=120 y=168 接地 生存 ステージ1 残機3"
else
    echo "  FAIL 初期状態が違う (state=$state)"
    fail=1
fi

echo "e2e: ジャンプすると位置が変わる"
# --keys は320Mサイクルから1キーを押下/離鍵それぞれ2Mサイクルで送る。
# START後にも無操作の q を25回挟むと、504M付近で k を押せる。
# 短いタップなので、着地前の506Mサイクルで状態を読む。
#
# Why not --input-script: ゲーム作成時に使ったランナーには存在したが、
# 現行 x68k-run の公開CLIには無い。公開CLIだけで再現できる方が壊れにくい。
jump_keys="$start_keys"
for ((i = 0; i < 25; ++i)); do
    jump_keys+=q
done
jump_keys+=k
shot /tmp/e2e-jump.ppm 506000000 "$jump_keys"
state=$(python3 "$here/x68k/tools/readhud.py" /tmp/e2e-jump.ppm | head -1)
y=$(echo "$state" | awk '{print $2}')
on_ground=$(echo "$state" | awk '{print $3}')
alive=$(echo "$state" | awk '{print $4}')
if [[ "$y" =~ ^0[0-9]+$ ]] && ((10#$y < 168)) && [[ "$on_ground" == "0" ]] &&
    [[ "$alive" == "1" ]]; then
    echo "  ok   跳んでいる (y=$y 接地=$on_ground)"
else
    echo "  FAIL 跳んでいない (state=$state)"
    fail=1
fi

echo "e2e: d キーで右へ動く"
# q でラウンド表示中を待ち、d の押下/離鍵を繰り返す。
run_keys="$start_keys"
for ((i = 0; i < 20; ++i)); do
    run_keys+=q
done
for ((i = 0; i < 20; ++i)); do
    run_keys+=d
done
shot /tmp/e2e-run.ppm 540000000 "$run_keys"
state=$(python3 "$here/x68k/tools/readhud.py" /tmp/e2e-run.ppm | head -1)
px=$(echo "$state" | awk '{print $1}')
alive=$(echo "$state" | awk '{print $4}')
if [[ "$px" =~ ^0[0-9]+$ ]] && ((10#$px > 150)) && [[ "$alive" == "1" ]]; then
    echo "  ok   x=$px まで進んだ"
else
    echo "  FAIL 進めていない (state=$state)"
    fail=1
fi

if [[ $fail -ne 0 ]]; then
    echo "e2e: 失敗あり"
    exit 1
fi
echo "e2e: すべて成功"
