---
type: Report
title: CoreS3のデバッグHUD差分描画
description: 同値文字と状態遷移時の全消去を削減。範囲消去版は実機約9.05MHz、音声不足約8.25%が残る。
status: draft
generated: { by: codex, at: 2026-09-05T18:48:50Z }
verified:
  - { by: process:host-tests, at: 2026-09-05T18:30:45Z }
  - { by: process:cores3-repeat, at: 2026-09-05T18:35:14Z }
  - { by: process:cores3-bounded-clear, at: 2026-09-05T18:48:50Z }
sources:
  - id: astra
    resource: conversation/astra_design_explicit
    title: AstraによるHUDキャッシュと無効化条件のread-only設計
  - id: code
    resource: ../x68k/platform/hud.c
    title: 正規化字形の列別キャッシュ
  - id: tests
    resource: ../x68k/test/test_video.cpp
    title: 実MMIOの書込み回数とTVRAM全体比較
  - id: device
    resource: ../build-x68k/cores3-hud-cache-repeat.log
    title: 同じ操作による実機2回目（初回はcores3-hud-cache.log）
---

# 原因と実装

hud_debug_lineは34文字を毎フレーム更新し、1文字16行×4プレーン=64回、合計2176回のTVRAM byte書込みを行っていた。表示を削除せず、row0の描画済み字形index+1を38byteの列別配列に保持する。0は未描画なので初回の空白も必ず描く。変更文字は従来どおり4プレーンを書き、完了後にcacheを更新する。未使用の隙間列は新しく描かない。[^astra]

hud_clearで全失効。crt0のBSS初期化で再起動時も失効。nes_charは現在y>=16で重ならないが、将来y0..15に交差する場合に失効する処理も追加した。別経路でこの領域へ直接描く場合にも失効通知が必要。パレット変更だけでは失効しない。エミュレータのfollowCursorは最後の書込みイベントではなくTVRAM内容を走査するため、同値書込み省略で追従先を変えない。[^code]

# 検証済み

just test-videoで初回2176書込み、同値0、1桁変更64、0009→0010の2桁変更128を確認。128状態の位置・負座標・地上/生存・面・残機・敵状態を差分描画し、毎回hud_clear後の全文描画とTVRAM全体が一致した。通常HUDの下方描画ではcacheが失効せず、clear後は全文を復元する。既存のタイトル3相・全4面・フェード・エンディングとFM/ドラム検査も成功。just testは217/217、Python17/17。[^tests]

元のbuild-x68k/GAME.Xとdisk.hdfを上書きせず、`/private/tmp/x68k-hud-candidate.ZDFgYx/`へdebug=1でビルド。GAME.Xは224932bytes、SHA256 `fadc68e2a3195f96db0db56b20261cbe7935e03ca932316c0faa9b91324ddced`。候補HDDは元disk.hdfへGAME.Xだけinjectし、SHA256 `57fc0b4690aa52dc8d190c02d9eebb84ba2d319d72c62d5770ce09a52e2ee7c6`。

ホストは18件のゲスト時刻入力で800000012cyclesまで実行して終了0。新GAME.XでGPIP最適化ON/OFFの最終PPMが完全一致（SHA256 `8d04c6a7a11fd3865e47a9e0cb7918eff553074fef6e6dfd06bb7ea6e6540720`）。ONのskipped=317905952cycles。旧GAME.Xとの同じcycle終了画面は一致しておらず、処理量変更で進行時刻が変わるため、これを旧版とのフレーム同値の証明にはしない。字形の同値は上記同一Game状態のMMIO試験で検証した。

# 実機更新

旧storage全体0x410000..0x1000000（12517376bytes）を `/private/tmp/x68k-firmware-recovery.ZPwRSY/storage-before-hud-fast.bin` に退避。SHA256 `b8efe963b0845570af02b60dc3835f2456f0611d0baaaa876b3289b2a2419df7`。最初の115200bps読出しは低速のため対象PIDを確認してSIGINTで中止（終了130）、921600bpsで読み直して153.7秒・終了0。どちらも読取専用で実機データは変えていない。

verify_flash_data.pyで退避データのIPL-ROMと81920セクタ全体が元ファイルと一致することを確認。候補X68Fも同じ検査で新ディスクとの全byte一致を確認。CGROMなしを含めROM構成を維持した。

候補x68kdata.binは738112bytes、SHA256 `3a426a149ec0e306166a667b32d91353ab44ab401f3ddd7fc4121244b1a48143`。0x410000へ書込・ハッシュ照合成功。アプリは音声キュー対策版（ELF prefix 2fa5a7221）を維持、音量40/255、サーボOFF。

約110秒の同じ壁時計操作を2回実行し、両方でLCDのゲーム表示を確認。後半80〜107秒の窓は初回258215424cycles/30021ms=8.601MHz、PCM不足15.667%、2回目244949820cycles/30056ms=8.150MHz、PCM不足21.013%。両方キュー破棄0、投入拒否0、投入前空0。前版の1回は6.382MHz・37.018%だった。改善の方向は2回確認したが、ゲーム状態を完全に揃えた比較ではなく、この数字を固定の性能保証にはしない。[^device]

初回の遅い91〜101秒では描画時間よりrun時間が増えているが、この区間のPCはログに無く、原因関数の確定には追加計測が必要。

# 状態遷移の全消去削減

Astraの静的調査で、差分文字版ELFの相対0x31d8..0x31e2に512KiB全TVRAMをゼロにする262144周のループを確認した。既存の命令時間表で約944万guest cycles/回。mainは全state変更でhud_clearを呼ぶ。これが遅い実機窓の原因だとはPC未採取のため断定せず、確定している不要作業の削減として別候補を実装した。

初回はHuman68kの文字位置を仮定せず全消去。以後はHUDがTVRAMを専有し、put_char/nes_charが書くbyte単位半開矩形のunionだけを4planes消す。空範囲でも両cacheを失効し、dirty_right=0で領域をリセットする。APIの所有契約をhud.hにも明記。Astraのread-onlyレビューで実装上の修正必須事項なし。

test-videoで初回262144word書込、空の再clearは0書込、通常HUD+debugのclearは20000byte未満かつ全TVRAMが0になることを確認。追加sentinel試験は、clear直後のbbox内0・bbox外A5を4planesで直接検査した。その他の128状態・タイトル・全4面などの描画検査も成功。

`/private/tmp/x68k-hud-clear.tLW876/`へ別ビルド。GAME.Xは225392bytes、SHA256 `01875b54a2ec9742c732030b1767435a81ce725bcef29d97ae5772e5d23ab973`。HDD SHA256 `4312784677ad2da63e1c5b34d68200eb430049ca91d50f3bf509ee1614c1bf8f`。hostは同じ18入力で800000000cycles・終了0、ゲーム画面を取得・目視。

X68Fは740672bytes、SHA256 `7c896a960f778efeee350cf0b4e8bc9a6b77f40488806480e2b84d6acd41ad4b`。ROMと全81920セクタの入力一致を確認し、0x410000へ書込・ハッシュ照合成功。元storageと差分文字版も復元可能なまま保持。

実機の同じ約110秒操作で後半80〜107秒は271832468cycles/30036ms=9.050MHz、PCM不足8.249%。キュー破棄0、投入拒否0、投入前空0。max_slice_cycles=60096。ログbuild-x68k/cores3-hud-clear.logは65716文字で欠落マーカー無し、対応PNGを目視してゲーム画面を確認。差分文字版の約8.15〜8.60MHz・不足約16〜21%から改善した単発の参考値であり、状態・場面を一致させた因果比較や残余不足の原因確定ではない。

10MHz定常・PCM不足ゼロ・DMA underrun・聴感・長時間・全編は未達または未検証。commit/pushは未実施。

# 引継ぎ時点（2026-09-05T18:55:30Z）

実機は範囲消去版GAME.Xとキュー待機修正版appで、GPIP/JITを有効にした直近試験の状態。これらの診断トグルは再起動で既定OFFに戻る。音量40/255・サーボOFFを維持。次の診断はAstraにより起動後guest cyclesへ18入力と600M/800M計測境界を合わせる案まで設計したが、未実装。GAME.X変更前後ではguest cyclesを合わせてもゲーム状態まで一致する保証はない。

エミュ変更は `/private/tmp/x68k-device-capture` のe4356d6ベース未コミットtreeにあり、本体 `/Users/kei/ghq/github.com/kexi/x68k-stackchan` のfix-scc-rr0-comment（0179106）は未変更。両者のmerge-baseはb6c8d83。本体ブランチへ無断で上書き統合していない。

一時worktreeの消失に備え、gitで列挙した変更・未追跡ソース全体をbuild-x68k/cores3-runtime-source-20260906-0355.tgzへ退避。SHA256 `08853689d8269a2d8474b695193e0d7b9ba3320f4f24ea02c717deaedf7d579b`。これはe4356d6へ重ねる作業状態の保存であり、採用しなかった未追跡ファイルも含む。レビュー済みの公開パッチや単独でビルド可能な完全リポジトリではない。ROM・HDD・build生成物はgitの除外対象で含まない。

[^astra]: GPIPの完全周回一括実行と同じ、ユーザーがplzで承認したAstra設計セッション。設計のみ委任し実装は親が担当。
[^code]: hud_clear、put_char、nes_char。数値変換や表示文字は変更していない。
[^tests]: CALUDE_HOST_VIDEOのpoke8を数え、エミュレータ実TVRAM全体を比較。任意の外部TVRAM上書きまで検出する設計ではない。
[^device]: 初回ログ65644文字・2回目65678文字、どちらもtruncatedマーカー無し。対応PNGもbuild-x68k/に保存。古いプレーンを0xa5で満たした追加MMIO試験でも、変更文字の上位3plane消去と未使用列・行外の保持を確認。
