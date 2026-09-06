---
type: Report
title: CoreS3音声の停止・再開とストリーム寿命
description: 停止中の時計、古いPCM、スピーカー空成功を修正。ホスト検証と実機起動は成功したが実時間供給は未達。
status: draft
generated: { by: codex, at: 2026-09-05T14:05:35Z }
verified:
  - { by: process:host-ubsan-device-lcd, at: 2026-09-05T14:05:35Z }
sources:
  - id: code
    resource: /private/tmp/x68k-device-capture
    title: audio/guest_audio/audio_playback/speaker_m5とmainの未コミット修正
  - id: tests
    resource: /private/tmp/x68k-device-capture/test
    title: test_audio_clock・test_audio_playback・test_speaker_m5とM5 stub
  - id: device
    resource: ../build-x68k/cores3-audio-lifecycle.log
    title: 約110秒のCoreS3起動・入力・音声供給ログ
---

# 修正

AudioPacerは一時停止中の壁時計を実行余裕へ加算せず、ゲスト時刻の巻戻りで基準を張り直す。先行抑制による待機はユーザー停止と区別し、壁時計が追いつけるままにする。GuestAudioProducerの停止切替とresetは部分PCMを破棄し、AudioChannelへ新ストリームを通知する。再開では音源時刻を巻き戻さない。[^code]

AudioChannelは公開済みPCMの破棄境界を生産側から通知し、消費側が読取indexを進める。貸出中の読取バッファは生産側から取り消さず、解放後に古いprefixだけを破棄する。AudioPlaybackはストリーム変更時にsinkを再初期化し、旧フェード履歴も消す。コピー中の変更も検査する。最後の検査直後に変更が来る場合は次のsubmitで破棄するため、ゼロ遅延の停止を保証するものではない。[^code]

M5SpeakerSinkの再初期化はM5.Speaker.endで出力タスク終了・保持参照解放を待ってからbeginする。PCMのPSRAM確保と累積統計は保持し、音量40を再設定する。タスクが停止している場合はplayRawの空成功を受け入れず、保持中の領域へコピーもしない。sink再初期化失敗後は次のストリーム変更まで送信しない。自動再試行や任意時点のドライバ障害復旧は未実装。[^code]

# 検証

just test-host 2/2（33.48秒）、just test-san 2/2（43.93秒、UBSan）、just build成功。テストは10時間停止後の先行制限、ゼロ/非ゼロ時計の巻戻り、満杯の読取leaseをまたぐ破棄、100回のストリーム変更、pause/reset/再開のフェード、初期化失敗と次epoch復旧、停止ドライバの空成功拒否、M5保持PCM解放順、再開後の音源PCM一致を確認。git diff --checkも成功。[^tests]

app624832bytes（0x988c0）、SHA256 `6212312880ca89b6ace2a36ceadf91c3ce304bf83b8f57770d34466cafbff78a`、ELF prefix `5d0388d94`。復元用audio-lifecycle-candidate.bin/.elfは `/private/tmp/x68k-firmware-recovery.ZPwRSY/`。app0x10000のみ書込み・ハッシュ照合済み。音量40/255、サーボOFF、ROM/HDD変更無し。[^device]

20秒初期待機後Jgame、35秒後RETURN、stage1-movement.jsonの8件は全てaccepted=1。約110秒でLCD取得終了0、PNGを目視しゲーム画面を確認。80–107秒runtime窓は160817798cycles/30047ms=5.35221MHz。音声80737→106132msではsource205824、missing190976frames、不足48.1290%。最終source904192、missing762880、accepted3256、rejected0、empty_before_submit0、dropped0。[^device]

# 未完了

実機ではstream restarts=0、paused/failed frames=0。この操作シナリオはエミュレータ停止・再開・resetを通っていない。ライフサイクル修正はホストでの検証であり、実機でその経路が動作したという証拠ではない。約5.35MHzは単回・壁時計基準・場面差あり。直前5.09MHzとの差をこの修正の性能改善と断定しない。

約48%のゲストPCM不足が残り、目標10MHzと定常供給不足ゼロは未達。実DMA underrun、聴音、全編、10/30分安定動作、状態を揃えた複数回計測も未検証。大きな設計見直しはユーザー指定Astraが必要で、Astraサブエージェントへの明示委任を質問済み・未回答。変更は一時worktreeにあり、元リポジトリへの統合・コミット・pushは未実施。

[^code]: e4356d6からの作業tree。既存のゲスト時間同期設計を維持し、未来の音源合成で不足を隠さない。
[^tests]: M5スタブは出力の参照寿命・状態・呼出回数を検査するが、実I2S DMAを再現しない。
[^device]: cores3-audio-lifecycle.log/.pngはローカルbuild-x68k成果物。録音・カメラ取得はしていない。
