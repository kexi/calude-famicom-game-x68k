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

# キーは --keys ではなく --input-script で送る。
#
# Why not --keys: あちらは「320M サイクルから 2M 間隔」という固定の刻みで
# 打つので、起動の速さが変わると全部の時刻がずれる。実際、高色アセットで
# GAME.X が育ったときに、タイトルが出る前に game\n を打ち終えていた。
# --input-script なら 1 打ごとにサイクルを書けるので、起動時間が変わっても
# 「タイトルが出てから押す」を保てる。
shot() {
    local out="$1" cycles="$2" script="$3"
    "$build/x68k-render-run" \
        --iplrom "$emu/rom/iplrom.dat" \
        --hdd "$build/disk.hdf" \
        --cycles "$cycles" --event-driven \
        --input-script "$script" --ppm "$out" >/dev/null 2>&1
}

# Human68k のプロンプトへ "game" と RETURN を打つ台本を作る。
#
# 330M から打ち始めるのは、それより前だと A> が出ていないため。
# タイトルが出るのは 580M 付近なので、以降の操作はそこから後ろに置く。
boot_script() {
    local out="$1"
    shift
    python3 - "$out" "$@" <<'PYEOF'
import sys
out, *rest = sys.argv[1:]
lines = []
cycle = 330_000_000
for ch in "game":
    lines.append(f"{cycle} down {ch}"); cycle += 1_000_000
    lines.append(f"{cycle} up {ch}"); cycle += 1_000_000
lines.append(f"{cycle} down 0x1D"); cycle += 1_000_000
lines.append(f"{cycle} up 0x1D")
# 追加の操作は "<cycle> <down|up> <key>" のまま渡す。
lines.extend(rest)
open(out, "w").write("\n".join(lines) + "\n")
PYEOF
}

# タイトルで 4bit (16 色) を選んで開始する。
#
# Why not 既定のまま RETURN を押すか: 既定の選択は 65536 色で、そちらの絵は
# 原作 NES パレットの色をそのまま持たない。checkppm.py は NES パレットとの
# 完全一致で要素を数えるので、高色の絵では player も ground も見つけられない。
# 4bit を選べば同じ判定器がそのまま使える。高色側の画は test-video が
# 全画素で見ている。
start_16color_ops=(
    "640000000 down w" "643000000 up w"
    "660000000 down 0x1D" "663000000 up 0x1D"
)

fail=0

echo "e2e: GAME.X はタイトルで入力を待つ"
boot_script /tmp/e2e-title.script
shot /tmp/e2e-title.ppm 600000000 /tmp/e2e-title.script
if ! python3 "$here/x68k/tools/checkppm.py" /tmp/e2e-title.ppm --expect title; then
    fail=1
fi

# タイトルが出た後 (640M/660M) に上キーと決定を送る台本。
boot_script /tmp/e2e-start.script "${start_16color_ops[@]}"

echo "e2e: START後に原作のラウンド画面を表示する"
shot /tmp/e2e-round.ppm 690000000 /tmp/e2e-start.script
if ! python3 "$here/x68k/tools/checkppm.py" /tmp/e2e-round.ppm --expect round; then
    fail=1
fi

echo "e2e: 起動して 1-1 の初期状態になる"
shot /tmp/e2e-boot.ppm 780000000 /tmp/e2e-start.script
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
# ゲームが始まる (780M 付近) より後に k をタップし、着地前に読む。
#
# Why not いきなり k を押さないか: ジャンプは BTN_A の立ち上がりで始まる
# (player.c の is_jump_start)。タイトルの決定に使った RETURN と同じ扱いで
# 押しっぱなしと判定される間は立ち上がりが作れないので、先に d を短く入れて
# 入力が読まれ始めたことを確かめてから k を押す。
boot_script /tmp/e2e-jump.script "${start_16color_ops[@]}" \
    "790000000 down d" "795000000 up d" \
    "800000000 down k" "806000000 up k"
shot /tmp/e2e-jump.ppm 806000000 /tmp/e2e-jump.script
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
# 開始後に d を押しっぱなしにして、右へ十分進む時間を与える。
boot_script /tmp/e2e-run.script "${start_16color_ops[@]}" \
    "790000000 down d" "830000000 up d"
shot /tmp/e2e-run.ppm 830000000 /tmp/e2e-run.script
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
