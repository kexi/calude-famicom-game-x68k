---
type: Report
title: CoreS3音声不足のFable独立調査
description: Fableの原因候補をソースとログで照合。描画・JIT脱出・無音投入の追加計測が必要で、修正完了ではない。
status: draft
generated: { by: codex, at: 2026-09-05T19:23:23Z }
verified:
  - { by: process:source-and-log-crosscheck, at: 2026-09-05T19:23:23Z }
sources:
  - id: fable
    resource: ../build-x68k/cores3-fable-review-raw.md
    title: claude-fable-5-1による読取専用の調査原文（未検証の断定を含む）
  - id: device
    resource: ../build-x68k/cores3-hud-clear.log
    title: 範囲消去版の実機ログ
  - id: video
    resource: ../x68k/platform/video.c
    title: ビットマップ転写とパレット更新
  - id: emulator
    resource: /private/tmp/x68k-device-capture
    title: 調査対象エミュレータ作業ツリー
---

# 調査条件

ユーザーの明示承認後、対象ソース・既存知識・実機テキストログをAnthropicへ送信した。ROM・HDD・認証情報は対象外。Claude CLIのRead/Grep/Globだけを許可し、MCPなし、変更・ビルド・実機操作なしで実行。モデル識別子claude-fable-5-1、終了0、successを確認した。原文は機械生成の仮説を含み、以下の照合結果を優先する。[^fable]

# 有用な原因候補

- PCM不足が短い区間に集中するため、全体平均だけでなく、同じゲーム状態ごとの実行時間・描画時間・不足増分を比較する。
- show_graphic_bitmapは256×240画素の61,440回のGVRAM word書込みを行う。転写、全面描画要求、MMIOアクセスに伴うJIT guard脱出が重なる区間を計測する。全面damage要求は合流し得るため、書込み回数と描画回数は同一ではない。[^video]
- AudioPlayback::submitはリングに完成ブロックが無ければ、sink.writeの待機より前に512frameの無音補完を決定する。短い供給遅れを補完1ブロックに拡大する可能性があり、再生側の残量と合わせた計測候補。ただし、聴感上の途切れや恒久的な遅延増加は未検証。[^emulator]

場面の推定はPC・状態の同時計測が無いため未確定。Fableの「死亡」「再開」「4〜5MHzが原因」などは確定診断として採用しない。最新の約9.05MHzもゲーム状態を揃えた性能保証ではない。

# 原文の訂正・留保

1. 原文の「後半87ブロック」は既存の80〜107秒窓の集計と一致しない。80,264msから106,085msのsource差分370,176、missing差分33,280で、512frame単位では65ブロック、不足率33,280 / 403,456 = 約8.249%。原文の表にはこの窓外の57秒・75秒が混在する。[^device]
2. 「empty_before_submitは構造上0にしかならない」は誤り。speaker_m5.hは既投入かつgetPlayingChannels()==0を検出し、受付成功時に加算する。test_speaker_m5.cppにも1へ増えるケースがある。本照合ではテストソースを確認しただけで再実行はしていない。観測0が実DMAアンダーラン0を証明しない点は正しい。[^emulator]
3. 「tick長不明」は訂正。sdkconfigとsdkconfig.defaultsはCONFIG_FREERTOS_HZ=1000。公称tickは1msだが、vTaskDelayの実際の再開時刻を保証するものではない。[^emulator]
4. 「時計ペーシングは一度も発火しない」「MMIOループが毎周JITへ入り直す」「翻訳領域が常時満杯」は、集計ログだけでは全時点・特定PCへの帰属を証明できない。専用カウンタとPC別測定が必要。
5. 固定24msの音声待機は未採用。まず実際のM5Unified実装のバッファ残量・API契約を確認し、DMA残量を無視した待機で途切れを増やさない設計が必要。スタブで期待仕様を先に作っても実装の仕様確認にはならない。

# 次の検証条件

状態・PC別の実行と描画を、不足増分と同じ時間窓で計測する。音声タスク所有の非atomicカウンタをCore1から直接読まない。HUDの状態桁は文字コードではなく字形画像なので、TVRAMの単一値では取得できず、消去中はunknownとして扱う。

ゲーム変更後は同じguest cycleでも状態が違い得るため、状態を揃えた比較と命令・画素の正しさの検査を分ける。JITの負のキャッシュ案も、同じPCでレジスタ値によりRAM/MMIOのアクセス先が変わる場合を誤って除外しない検証が必要。

今回は調査記録のみ。ファームウェア、ゲーム、音量、サーボ設定は変更していない。全場面の音声供給、聴感、長時間安定性は未完了。

[^fable]: Claude Fable調査原文。ローカルbuild-x68kの生成物でありgit管理外。
[^device]: cores3-hud-clear.logのaudio-continuity行を直接照合。
[^video]: x68k/platform/video.cのshow_graphic_bitmapを直接確認。
[^emulator]: src/x68k/platform/audio_playback.h、speaker_m5.h、test/test_speaker_m5.cpp、sdkconfigを直接照合。作業ツリーは一時ディレクトリなので、既存cores3-hud-cache.mdのソース退避情報も参照。
