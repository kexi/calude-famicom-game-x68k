---
okf_version: "0.2"
---

# ナレッジ索引

* [ゲーム中のブロック終端は89.8%が分岐で、その46.5%は不成立で切れている](jit-block-length.md) - BlockEnd分布を実行回数で初測定。中間Bccを許すと平均2.655→4.33命令(+63%)。長さ不明終端は100%がシフト($E群)。

* [描画27.5→4.2msでfps 7.73→15.84](cores3-30fps-blocker.md) - GVRAM書込がdamage_.all()を呼んで差分描画を殺していた。座標単位のdirtyでタイルが300→30に。CPU 4,601→7,959kHz、音声不足54.0→20.3%。30fpsには残り1.26倍。

* [GPIP既定ONと簡略背景で実効4,601→6,324kHz](cores3-speed-gpip-and-background.md) - 作ってあったGPIP同期省略が既定OFFで埋もれていた(+27.5%)。遠景を行1色へ簡略化して+7.8%。音声不足54.0%→36.9%。

* [JITのarena容量・reset閾値・スロット配分を実機で振ったが全部悪化](jit-arena-capacity.md) - 満杯48.4%を律速と読んで容量系を3通り試したが、いずれも基準より遅い。arenaは律速ではない。

* [ゲーム実行中にJITが弾く命令を実行回数で測る](jit-hot-rejects.md) - 実行回数で測るとJIT不可15.0%。2件実装し被覆90.6%へ。当初「遅くなった」と記録したが誤りで、A/Bの結果JIT拡張はONの方が速い。真因はOPMレート4倍化(15625→62500Hz)で11.7%のCPU費用。

* [前景16bit版CoreS3の実機計測](cores3-foreground-measurement.md) - 6条件とも不足率はCPU速度の10MHz比と恒等的に一致。遠景合成が最も重く、背景OFFで描画-20%・CPU+15%。JITと主旋律ブーストを既定ONへ。

* [CoreS3への書き込みと実機での前景16bit動作確認](cores3-device-verified.md) - hash検証つきで書き込み、実機でタイトル・ROUND・ゲーム本編まで到達。65536色前景の実機描画を確認。速度と音質は未計測。

* [e2eの固定サイクル入力が起動時間の変化で破綻](e2e-input-timing.md) - --keysの固定刻みがGAME.X肥大化で崩れ全項目FAILしていた。--input-scriptの明示サイクルへ移し、判定器が読める4bitを選ぶよう変更して全項目成功。

* [スプライト属性ワードの反転bitが実機と不一致](sprite-attribute-bits.md) - ゲームとエミュレータの双方が反転にbit8/9を使い、実機のパレットblockを汚していた。両方をbit14/15へ是正し、4bit経路の実機で反転と色の両方を確認した。

* [標準GVRAMリング試作の初回計測と残る超過](foreground-ring-bench.md) - リング方式は全4面スクロールを約8倍高速化。逐次overlay復元と画素毎の再計算を除いてさらに約2.1倍縮め、通常場面は予算の約1.6倍まで到達した。

* [全前景16bit描画のFable相談と採用条件](foreground-fable-review.md) - Fableは標準スクロールとリング描画を提案。Astraとソース照合し、互換性の先行是正・全16bit維持・未測定の復元方式と性能値を区別した。

* [全前景の16bit化試作と性能上の未達](foreground-highcolor.md) - 全前景の直接色描画は試作で成功したが性能は未達。基準周期を訂正し、現ゲームとエミュ間で隠れていた標準互換性の不一致を追記。

* [4bit・16bit表示モードの選択と継承](graphics-mode-selection.md) - 4bit/16bit選択と高色ROUNDを実装し、Wキー対応を修正。全画素回帰・実ゲスト起動・配布物一致・CoreS3書込みと撮影を検証。

* [NESビルドツールのNix管理](nes-nix-toolchain.md) - cc65とGNU Makeを既存devShellへ追加し、Homebrewなしで原作NESの再ビルドを検証。

* [ゲーム単体ディスクの配布](game-distribution.md) - GAME.Xのみの非起動XDFを自作し構造を検証。現行エミュレータのFD読込みは媒体エラーで未達。

* [ゲーム遠景の65536色化](stage-highcolor.md) - 全4面の山と空を固定の直接色背景へ変更。ホスト描画・CoreS3への書込み・実機ゲーム画面を検証し、性能比較は未確認。

* [タイトル右64pxの背景描き足し](title-right-extension.md) - 既存256pxを完全保持し、CoreS3の右64pxへ森と岩場を追加する。CRTC・音声・ゲーム中の倍率は変更しない。

* [65536色モードのタイトル](title-highcolor.md) - 多色タイトルのホスト描画・CoreS3書き込み・実機タイトルからゲームへの遷移を検証。音声供給不足と聴感確認は残る。

* [タイトルのX68000表記とCoreS3右余白](title-x68000-label.md) - X68000表記をタイトル下部へ追加しホスト起動・描画を検証。CoreS3の右64pxは等倍左寄せによるゲーム領域外表示で、表示変更・実機再書き込みは未実施。

* [CoreS3のトランペット風FM主旋律と出力ゲイン](cores3-fm-trumpet.md) - トランペット風主旋律を基本2倍へ変更しピークだけ圧縮。合成RMSは前候補比1.96倍。別ファーム誤書き込みから16MB構成も復元しタイトル表示を確認、聴感は確認待ち。

* [CoreS3のFM旋律の再アタックと倍音修正](cores3-fm-melody.md) - 追加3dB版でも主旋律が小さすぎるとの報告。TL調整の余地がほぼなくなり、依頼されたトランペット風音色と専用ゲインの検証へ継続。

* [CoreS3の画像再利用とHUD行単位更新](cores3-bitmap-hud-byte.md) - 同画像の全面再転写と文字の画素単位RMWを削減。診断版の単発実機計測は約9.65MHz・PCM不足約2.90%で、全不足解消は未達。

* [CoreS3の実行開始位置別計測](cores3-run-profile.md) - 任意ONの1秒計測で画像転写・HUDの命令列を実機とELF間で照合。音声不足は約9%残り、最適化の評価は継続中。

* [CoreS3音声不足のFable独立調査](cores3-fable-review.md) - Fableの原因候補をソースとログで照合。描画・JIT脱出・無音投入の追加計測が必要で、修正完了ではない。

* [CoreS3のデバッグHUD差分描画](cores3-hud-cache.md) - 同値文字と状態遷移時の全消去を削減。範囲消去版は実機約9.05MHz、音声不足約8.25%が残る。

* [CoreS3の厳密GPIP待機ループ最適化](cores3-gpip-macro.md) - 完全周回一括実行と追いつき時のPCMキュー破棄を修正。実機約6.38MHz、供給不足約37%は残る。

* [CoreS3音声の停止・再開とストリーム寿命](cores3-audio-lifecycle.md) - 停止中の時計、古いPCM、スピーカー空成功を修正。ホスト検証と実機起動は成功したが実時間供給は未達。

* [CoreS3のGPIPポーリング同期省略](cores3-gpip-poll.md) - イベント期限前のGPIP読取で不要な同期を省略。実機約5.09MHzで音声不足51.1%は残る。

* [CoreS3の単項命令時間とシフト計算修正](cores3-unary-shift.md) - 単項命令・シフトの時間を修正しビット反復を一括計算化。ホストと実機を検証したが音声不足は残る。

* [CoreS3の通常ALU命令時間修正](cores3-standard-timing.md) - 通常ALUの時間を幅・EA・方向別に修正し351ケースとUBSanを検証。音声供給不足の解消は未達。

* [CoreS3の即値・quick演算時間修正](cores3-immediate-timing.md) - 即値6演算とADDQ/SUBQを幅・EA別の仕様時間へ修正。196ケースとUBSanを検証したが音声供給不足の解消は未達。

* [CoreS3のJIT実行部IRAM配置比較](cores3-jit-iram.md) - JIT実行部960byteをIRAMへ移して単発比較したが、明確な改善を確認できず配置変更を撤回。音声不足は残る。

* [CoreS3音声時計のScc命令時間修正](cores3-scc-timing.md) - Sccの4サイクル固定を条件・EA別時間へ修正し32ケースと実機起動を検証。音声不足解消は未完了。

* [CoreS3の制御EA命令時間の修正](cores3-control-timing.md) - LEA・PEA・JMP・JSRのEA別時間を共通化し28ケースとUBSanを検証。音声供給不足の解消は未達。

* [Human68k HDD へのファイル注入](human68k-hdd-injection.md) - 既存の起動可能HDDへGAME.Xを追加する方法と検証根拠
* [X68000版の移植状況](x68k-port-status.md) - 原作との機能差と検証済み範囲
* [CoreS3の処理速度・音声出力の改善設計](cores3-runtime-design.md) - 計測、音声バッファ所有権、描画負荷、ゲスト時間同期の段階的な改善方針と合格条件
* [CoreS3音声バッファの所有権修正](cores3-audio-implementation.md) - 音声所有権と計測を修正しCoreS3へ適用。追加PCM領域による内部RAM不足をPSRAM配置で解消したが、ゲーム中の速度・音声供給不足は残る。
* [CoreS3のJIT比較と描画時間予算](cores3-render-budget.md) - OPMの毎オペレータ初期化ガードを除去し、タイトル曲の合成最大時間が約32msから約3.7msへ改善。供給は約30.5ブロック/秒を維持したが全場面と長時間は未検証。
* [CoreS3再生投入状態とゲーム開始の検証](cores3-playback-observations.md) - flashマップとSASI一括DMAで読み込み遅延を約219msから49msへ短縮し、STAGE 1-1開始表示まで投入前空状態0を確認。DMAアンダーラン・全場面・長時間は未検証。
* [CoreS3のBG行キャッシュとJIT有効の延長観察](cores3-bg-row-cache.md) - BGの8byte行キャッシュで後半の平均描画31.6ms→30.5ms、CPU実効2.71→2.76MHz。変更前後の約171秒で投入前空状態0だが、全編操作・実DMAアンダーラン・長時間は未検証。
* [CoreS3の押下・解放入力と音声供給試験](cores3-input-scenario.md) - 入力試験の受信待ちを修正し、8イベントの送信遅れが最大979ms→10ms。約110秒で音声空状態0を再確認したが、各操作・実DMA・音質の検証は未完了。
* [CoreS3の描画前受け渡し確認とJIT内訳](cores3-frame-preflight.md) - 描画前チェックを追加したが約110秒で受け渡し待ちは0、速度は約2.69MHzで変わらず。JIT内訳の計測を追加し、CPU側の切り分けとDMA計測のESP依存除去を残す。
* [CoreS3のDMA計測分離とJIT容量比較](cores3-jit-capacity-probe.md) - DMA計測のESP依存を除去。JITコード20KiB化はスロット2048→512へ縮退し約2.72→2.09MHzに悪化したため16KiBへ復元。音声キュー指標は0を維持したが最終性能・音質は未達。
* [CoreS3のタイル世代管理準備と表示数更新](cores3-tile-generations.md) - 二重バッファの300タイル世代管理を単体実装し96フレームの疑似画素モデルと一致。表示数更新を定数時間化したが実機速度向上は未確認で、局所再合成への接続は残る。
* [CoreS3の等倍局所合成接続](cores3-tiled-compositor.md) - 書き込み通知と等倍の二重バッファ局所合成を実機へ接続。約110秒の単発比較で後半CPU 2.59→3.20MHz、描画占有29→10%。音声キュー指標0だが10MHz・音質・ゲスト時間同期は未達。
* [CoreS3音源のゲスト時刻通知とPCM蓄積](cores3-guest-audio-clock.md) - 音源MMIO前のゲスト時刻通知と640cycles/sampleのPCM蓄積を実装し、実行分割・短いFM音・満杯時の状態進行をホスト検証。M5接続・pacing・供給不足補完は未実装。
* [CoreS3ゲスト同期再生と供給不足の顕在化](cores3-synchronized-playback.md) - ゲスト同期PCMをM5へ接続。約110秒で投入切れ0だが、後半の供給不足補完は67.5%、CPU約3.32MHz。ミュート・フェード・先行制限を実装したが実時間供給は未達。
* [CoreS3のJIT鍵不一致とメモリ配分比較](cores3-jit-memory-split.md) - ゲーム中のJIT鍵不一致は未登録が主因。1024スロット・コード32KiBの単発比較で後半3.33→3.39MHz、音声不足67.35→66.71%。改善の再現性と実時間供給は未達。
* [CoreS3音声時計に使うMOVE命令時間の修正](cores3-move-timing.md) - MOVE/MOVEAの4サイクル固定をMC68000のEA・幅別時間へ修正し通常実行とJITを統一。ホスト・UBSan・実機起動を検証したが、後半の音声不足56.8%と他命令の時間監査が残る。
