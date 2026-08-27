#!/usr/bin/env bash
# エミュレータ上で実際に動かし、画面に何が出ているかを確かめる。
#
# 3 層の検証のうち、これが一番外側:
#   just test   ゲームの規則 (ホストで直接)
#   just solve  全ステージが通せるか (探索)
#   just e2e    68000 のコードとして動き、絵が出るか  <- ここ
#
# なぜ画面の絵 (PPM) を見るか: 以前はテキスト画面を --dump-text で
# 読み戻していたが、HUD を自前の字形で描くようにしたので、CGROM との
# 照合による逆引きに引っかからなくなった。絵が出ているかを見たいなら、
# 絵そのものを見る方が直接的で、「文字が読めるか」という別の問題に
# 巻き込まれない。
set -euo pipefail

emu="${X68K_STACKCHAN:-$(dirname "$0")/../../../x68k-stackchan}"
build="${BUILD_DIR:-build-x68k}"
here="$(cd "$(dirname "$0")/../.." && pwd)"

shot() {
    local out="$1" cycles="$2"
    shift 2
    "$emu/build-host/x68k-run" \
        --iplrom "$emu/rom/iplrom.dat" \
        --hdd "$build/disk.hdf" \
        --cycles "$cycles" --event-driven \
        --keys $'game\n' "$@" --ppm "$out" >/dev/null 2>&1
}

fail=0

echo "e2e: 起動して 1-1 の画面が出る"
shot /tmp/e2e-boot.ppm 600000000
if ! python3 "$here/x68k/tools/checkppm.py" /tmp/e2e-boot.ppm \
    --expect player,ground,block,hud; then
    fail=1
fi

echo "e2e: ジャンプすると位置が変わる"
# 入力台本でジャンプさせ、跳んでいる最中を撮る。
# 跳んでいる最中を狙う。台本は 455M サイクルで押し始めるので、
# その少し後が一番高い。
shot /tmp/e2e-jump.ppm 456000000 --input-script "$here/x68k/test/jump.script"
if ! python3 "$here/x68k/tools/checkppm.py" /tmp/e2e-jump.ppm --expect player,ground; then
    fail=1
fi

# 跳んでいることを、プレイヤーの縦位置で確かめる。
#
# 立っているときと跳んでいるときで、肌の色が出る一番上の行が変わる。
# 「絵が出ている」だけでは動いていることの証明にならないので、
# 動いた結果として位置が変わることまで見る。
top_standing=$(python3 - "$here" <<'PY'
import sys
sys.path.insert(0, sys.argv[1] + "/x68k/tools")
from pathlib import Path
src = (Path(sys.argv[1]) / "x68k/tools/checkppm.py").read_text()
exec(src.split("def main")[0])
w, h, px = read_ppm(Path("/tmp/e2e-boot.ppm"))
skin = COLORS["skin"]
for y in range(16, 240):
    row = y * w * 3
    for x in range(256):
        o = row + x * 3
        if (px[o], px[o+1], px[o+2]) == skin:
            print(y); sys.exit(0)
print(-1)
PY
)
top_jumping=$(python3 - "$here" <<'PY'
import sys
sys.path.insert(0, sys.argv[1] + "/x68k/tools")
from pathlib import Path
src = (Path(sys.argv[1]) / "x68k/tools/checkppm.py").read_text()
exec(src.split("def main")[0])
w, h, px = read_ppm(Path("/tmp/e2e-jump.ppm"))
skin = COLORS["skin"]
for y in range(16, 240):
    row = y * w * 3
    for x in range(256):
        o = row + x * 3
        if (px[o], px[o+1], px[o+2]) == skin:
            print(y); sys.exit(0)
print(-1)
PY
)

if [[ "$top_jumping" -ge 0 && "$top_standing" -ge 0 && "$top_jumping" -lt "$top_standing" ]]; then
    echo "  ok   跳んでいる (立ち y=$top_standing -> 跳び y=$top_jumping)"
else
    echo "  FAIL 跳んでいない (立ち y=$top_standing 跳び y=$top_jumping)"
    fail=1
fi

echo "e2e: 歩いてブロックを跳び越える"
# 1-1 のメタ列 13 (x=208-) に地上ブロックがある。跳ばないと越えられない。
#
# ここまで見るのは、「絵が出る」だけでは遊べることの証明にならないため。
# 障害物を越えて先へ進めることまで確かめる。
shot /tmp/e2e-run.ppm 420000000 --input-script "$here/x68k/test/play11.script"
state=$(python3 "$here/x68k/tools/readhud.py" /tmp/e2e-run.ppm | head -1)
px=$(echo "$state" | awk '{print $1}')
alive=$(echo "$state" | awk '{print $4}')
if [[ "$px" =~ ^0[0-9]+$ ]] && (( 10#$px > 250 )) && [[ "$alive" == "1" ]]; then
    echo "  ok   ブロックを越えて x=$px まで進んだ"
else
    echo "  FAIL 進めていない (state=$state)"
    fail=1
fi

if [[ $fail -ne 0 ]]; then
    echo "e2e: 失敗あり"
    exit 1
fi
echo "e2e: すべて成功"
