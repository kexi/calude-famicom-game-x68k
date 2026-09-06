---
type: Report
title: CoreS3のGPIPポーリング同期省略
description: イベント期限前のGPIP読取で不要な同期を省略。実機約5.09MHzで音声不足51.1%は残る。
status: draft
generated: { by: codex, at: 2026-09-05T14:01:00Z }
verified:
  - { by: process:host-ubsan-device-lcd, at: 2026-09-05T14:01:00Z }
sources:
  - id: code
    resource: /private/tmp/x68k-device-capture
    title: machine.cppとtest_gpip_poll.cppの未コミット変更
  - id: device
    resource: ../build-x68k/cores3-gpip.log
    title: CoreS3約110秒の起動・入力・LCD取得ログ
---

# 修正とホスト検証

イベント駆動実行中かつスケジューラ期限前（debt<0）のMFP GPIP読取だけmaterializeを省略。VBlankの両エッジを期限として保持し、期限到達時、外部wake、他のMFP読取は従来の同期を使う。「MFPは初期化・割込み時だけ読む」というコメントはゲームの頻繁なGPIPポーリングに合わないため訂正した。[^code]

通常実行との比較はGPIP列をrolling hash化し、8設定×3実行幅×Timer読取有無×24sliceで、CPU状態、サイクル、Timer、raster、blankを照合。途中の外部入力とTimer非ゼロ位相も含む。最初のテスト実装で存在しないtickを呼んだコンパイルエラーをtickFastへ修正。just test-host 2/2（33.92秒）、test-san 2/2（43.47秒、UBSan）、実機build成功。[^code]

# 実機の観測

app623600bytes、SHA256 `7197d21fd46879466bff5960fd9381f7f739c77eeaa0a42c4a971824119bca02`、ELF prefix `b1bd3a6f4`。復元用gpip-candidate.bin/.elfを `/private/tmp/x68k-firmware-recovery.ZPwRSY/` に保存。app0x10000のみ更新し書込ハッシュを確認。ROM/HDD変更無し、音量40/255、サーボOFF。[^device]

capture-lcdで初期待機20秒、Jgame、35秒後RETURN、stage1-movement.jsonの8入力が全てaccepted=1。約110秒で終了0、PNGを目視しゲーム画面を確認。[^device]

30–62秒窓は183376204cycles/35104ms=5.22380MHz、80–107秒窓は152835498cycles/30030ms=5.08943MHz。直前単項/shift版の同窓4.61461/4.95146MHzから増加したが、単回・壁時計基準でゲーム状態を厳密に揃えていないため再現性未確認。音声80723→106118msではsource194048、missing202752framesで不足51.0968%。最終source887808、missing779264、accepted3256、rejected0、empty_before_submit0、dropped0。[^device]

10MHzでの実時間動作と音声不足解消は未達。投入前の空状態0はDMA underrun無しを証明しない。聴音・全編・長時間検証も未実施。

[^code]: e4356d6からの未コミットworktree。GPIP以外の既存IRQ/タイミング回帰テストも実行。
[^device]: ログ全文とcores3-gpip.pngはローカルbuild-x68k成果物。capture-lcd /dev/cu.usbmodem2101 /private/tmp/cores3-gpip.ppm 'Jgame\\r' 90 35 tools/scenarios/stage1-movement.json。
