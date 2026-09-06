---
type: Report
title: CoreS3のFM旋律の再アタックと倍音修正
description: 追加3dB版でも主旋律が小さすぎるとの報告。TL調整の余地がほぼなくなり、依頼されたトランペット風音色と専用ゲインの検証へ継続。
status: draft
generated: { by: codex, at: 2026-09-06T02:05:55Z }
verified:
  - { by: process:audio-retrigger-regression, at: 2026-09-05T20:44:00Z }
  - { by: process:audio-balance-and-spectrum, at: 2026-09-05T20:44:00Z }
  - { by: process:cores3-flash-hash, at: 2026-09-05T20:44:00Z }
  - { by: process:cores3-title-frame-and-audio-log, at: 2026-09-05T20:48:00Z }
  - { by: process:opm-output-observation-host-tests, at: 2026-09-05T21:04:05Z }
  - { by: process:cores3-opm-channel-log, at: 2026-09-05T21:04:05Z }
  - { by: process:title-balance-host-and-flash, at: 2026-09-05T21:04:05Z }
  - { by: process:cores3-final-title-and-log, at: 2026-09-05T21:06:39Z }
  - { by: process:title-volume-plus-host-and-flash, at: 2026-09-06T01:19:11Z }
  - { by: process:title-volume-plus-device-frame-and-log, at: 2026-09-06T01:21:00Z }
  - { by: process:title-volume3-host-and-flash, at: 2026-09-06T01:29:09Z }
  - { by: process:title-volume3-device-frame-and-log, at: 2026-09-06T01:30:27Z }
sources:
  - id: original
    resource: ../src/sound.s
    title: 原作の新音時音量リセットとゲーム中旋律停止
  - id: platform
    resource: ../x68k/platform/audio.c
    title: OPM音色と発音要求の適用
  - id: test
    resource: ../x68k/test/test_audio_balance.cpp
    title: 実音源の分離測定・倍音比・クリップ試験
  - id: baseline
    resource: ../build-x68k/audio-balance/title-baseline-separated.jsonl
    title: 修正前20秒・15625Hz・5545/100fpsのタイトル曲
  - id: candidate
    resource: ../build-x68k/audio-balance/title-validated.jsonl
    title: 再アタックと倍音修正後・同条件のタイトル曲
  - id: design
    resource: conversation/astra_design_explicit
    title: Astraの段階的修正方針
  - id: device-channels
    resource: ../build-x68k/cores3-fm-channels.log
    title: 診断アプリでタイトルの各FMチャンネルを実機観測
  - id: final-balance
    resource: ../build-x68k/audio-balance/title-final-balance.jsonl
    title: 最終のタイトル主旋律音量を実コードで検証
  - id: volume-plus
    resource: ../build-x68k/audio-balance/title-volume-plus.jsonl
    title: 追加1.5dB後のタイトル20秒と36設定のクリップ検証
  - id: volume-feedback
    resource: conversation/2026-09-06-title-volume
    title: 聞こえません・少しメロディー音量を上げて・ドラムはよく聞こえるとのユーザー報告
  - id: volume-plus-device
    resource: ../build-x68k/cores3-fm-volume-plus.log
    title: 追加1.5dB版の実機タイトル起動・通常再生ログ
  - id: volume3-feedback
    resource: conversation/2026-09-06-title-too-quiet
    title: 前候補は小さい音で鳴っているが小さすぎるとの報告
    author: human:kexi
  - id: volume3
    resource: ../build-x68k/audio-balance/title-volume3.jsonl
    title: 追加3dB版の実コード20秒・15625Hz・55.45fpsと36設定の検証
  - id: volume3-device
    resource: ../build-x68k/cores3-fm-volume3.log
    title: 追加3dB版の通常タイトル起動・再生ログ
---

# 確定した問題と修正

OPM実装はキーのOFF→ON立上りでのみAttackへ戻る。ゲームは異なる次音でもONだけを書いていたため、休符を挟まない新音が減衰済みのまま続いていた。audio_commitの離鍵条件をkey_off || key_onへ変更し、OFF→KC/KF/TL→ONを送る。同音タイはsound_updateがkey_onを出さないため維持する。原作の新音時mel_vol=10/mel_age=0への復帰と整合する。[^original] [^platform]

修正前にtest_videoの再アタック試験がAttack復帰assertで失敗し、修正後は成功。無更新frameと再OFFで波形が変わらないこと、別音ONで振幅が戻ることも検査した。

以前のリード音色はalg7の4スロットが同じMUL1/TL/位相で、矩形波ではなく同相の正弦波4本だった。lead/harmonyだけMULを[1,1,1,3]とし、基本波3本と第3倍音1本を加算する。原作50%矩形波の最初の2成分の近似であり、完全なNES波形再現ではない。基本波の本数が減る分をTL3段の減算で補償し、0〜127へclampする。bass/SFXの音色、ADPCM、曲データ、テンポ、マスター音量は変更しない。[^design] [^platform]

# 音源別検査

実際のsound.c/audio.cを55.45fpsで動かし、OPM/ADPCMを各sample1回だけ進める。各voiceへOPMレジスタ書込みを複製し、他chのkey-onだけを抑制した独立OPMの和が、全FMと全312500sampleで一致することを確認。音声WAVは合成データであり、マイク録音ではない。[^test]

| 20秒全体 | 修正前 | 再アタックのみ | 再アタック＋倍音 |
| --- | ---: | ---: | ---: |
| lead RMS | 494.740 | 498.655 | 511.385 |
| harmony RMS | 146.056 | 147.221 | 151.123 |
| 全FM RMS | 1422.134 | 1424.617 | 1432.227 |
| ADPCM RMS | 2242.193 | 2242.193 | 2242.193 |
| mix peak | 15864 | 16100 | 15856 |
| mix clip数 | 0 | 0 | 0 |

導入部の旋律無音を含む全20秒の値。主旋律の初回key-onは全候補3.011776秒で、発音件数は44回。再アタックだけの平均音量改善は小さく、それだけで可聴性が解消したとは扱わない。FM合計にはbassが大きく含まれるため、非ゼロFMやFM合計RMSは主旋律が聞こえる証拠にならない。[^baseline] [^candidate]

最終候補の定常C5音をHann窓で検査し、第3倍音/基本波=0.332521、第2倍音/基本波=0.000003。タイトル音列および4曲状態×9SFXの36設定を合成し、旋律第3倍音のNyquist超過なし、mix clipなし。開始和音の設定はplaying=0で検査するため、36設定がすべて異なる曲の組合せではない。ADPCMの20秒WAVは修正前とcmp完全一致、bassは再アタック候補と倍音候補間で完全一致。[^test]

just test-video、just test（C217件＋Python17件）、just fmt-check、git diff --check成功。全編実機プレイ・聴感・音声供給不足ゼロは未検証。

# ホスト検証環境の注意

最初のch別計測はGCCで作ったlibx68k_core.aへClangのテストをリンクし、OPM配列のコンストラクタでSIGSEGVになった。macOSの該当ipsと逆アセンブルで、Clang側がconstructorの返すthisを次要素のアドレスに使い、GCC側がそれを保持しないコードを確認した。この失敗から音源の無音を推論しない。

測定試験はOPM/ADPCMの実ソースも同じclang++でコンパイルする形に修正した。MMIOは実音源メソッドへ写し、Machine/bus経路は既存test-videoで別検査する。変更前の全FM/ADPCM/mix統計は、従来Machine経由の20秒基準値と一致した。実機のツールチェーンや音源実装は変更していない。

# 実機候補と復元

GAME.Xを別ディレクトリ/private/tmp/x68k-fm-melody.oKLJBuへクロスビルドし、通常debug0ディスクのGAME.Xだけを更新。IPLとHDD全81920セクタのpack一致確認後、接続済みCoreS3の0x410000へ741952bytesを書き、esptoolのhash照合成功。エミュレータappは既存5c3e5efb…のまま。volume40/255、servo OFFを維持する。

- GAME.X 226118bytes：SHA256 2fe5e091afaffaed6f1fa69c7c3c6196f8e50f0352c748da54c26c4620a4c911
- disk.hdf：bce9a755127b0e1b7d328d7bccb22a3e9a659abfc409f681907e9122b2237de4
- x68kdata.bin：51713f853c0b8388dce83761e8272d87e864dfc8feecfc1334fb5fb67e130ee2
- 元GAME候補/private/tmp/x68k-release-runtime.Tx9TPJと、全storage退避/private/tmp/x68k-firmware-recovery.ZPwRSY/storage-before-hud-fast.binを保持。

原作もゲーム中(song0)はlead/harmonyを停止し、タイトルのfade>=11で主旋律が始まる。タイトルを実機で確認する必要がある。ゲーム中に新しいBGMを追加する変更はしていない。[^original]

更新前後ともUSB接続後20秒でGPIP/JITをONにしてgameを入力し、40秒間タイトルで待機。最終LCDがタイトルであることを親が確認した（build-x68k/cores3-fm-title-{before,after}.png）。直接保存ログの30〜60秒内の最初/最後のPCM報告差分は、修正前30999〜59474msでsource430592/missing14336（不足3.222%）、修正後30999〜59540msでsource439296/missing6656（1.493%）。runtimeの別の5秒境界集計は約9.67→9.84MHz。同じ壁時計台本でも曲の到達位置と窓は一致しない単発参考値であり、音色変更の速度改善とは断定しない。

両ログでdropped/rejected_total/empty_before_submit_total/failed_frames/restart_failuresの非ゼロ報告なし。更新後もPCM不足は残る。FM単独の実機波形やDAC出力を取得したわけではなく、主旋律が人に聞こえることはまだ未確認。実機は修正版タイトルで動作したままにしている。

[^original]: src/sound.sの@melody、@mel_rest、@echo、@envelopesと、移植sound.c。初回の再アタック/倍音候補時点ではsound.cのSHA256は02784a650243c8380c45f7e9b83217c48efa2f1018fc5b8065f7ed0fa7b16efeのままだった。続くタイトル音量変更は以下の追記に記録。
[^platform]: audio.cのset_voice/audio_init/audio_commit。Astraの限定案に基づき親が実装。
[^test]: just emu=/private/tmp/x68k-device-capture test-audio-balance 20 build-x68k/audio-balance/title-validated、終了0。
[^baseline]: title-baseline-separated.jsonlとtitle-baseline-separated-{bass,lead,harmony,adpcm,fm,mix}.wav。
[^candidate]: title-retrigger.jsonl、title-validated.jsonlと対応WAV。実機スピーカーの聴感とは別のホスト計測。
[^design]: Astraは再アタック→音源分離→限定倍音の順を提案。音量一括増幅は行わなかった。

# 初回候補は未解決との報告（2026-09-06 JST）

上記適用後、ユーザーから「まだFM音源なってない」と報告された。再アタックの不具合と音色不整合は検査できたが、主旋律の可聴性は解消していなかった。上記の非ゼロPCMやクリップなしを、解決済みの根拠にしてはならない。

既存の3秒FM単独テストをmaster40のまま実機で実行。ゲームを起動しない状態でOPM ch0を鳴らし、出力用PCMのpeakが0→8192→0へ変化した。ログはbuild-x68k/cores3-fm-only-probe.log。event-driven/JITはOFFで、ゲスト供給不足を含むため連続した実時間の純音が保証された試験ではない。テスト音の聴感回答は未受領。

## 各FMチャンネルの実機計測

OPMの実際のrenderChannel戻り値から、ch別peak/nonzero/squaresを任意のOutputStatsへ記録する。再合成はしない。保存領域はPSRAMで確保し、Core1だけで接続/初期化/採取する。既存run-profileのON時だけ有効で、通常はOFF。Astraは出力不変と寿命を静的レビュー。just test-hostは2/2成功（34.12秒）、release途中と解除後の波形一致も追加後2/2成功（33.89秒）。

無音fast pathはrenderOneSampleを呼ばないため、stats.samplesは全PCMフレーム数ではない。0は「観測なし」で、sqrt(squares/samples)は観測された合成sampleのRMS。ログのTL/EG/phaseはoperator0だけ。混合前のch別出力なので、スピーカー到達とは区別する。計測中のPSRAM更新/毎秒8行ログは性能へ影響しうる。

診断app SHA256 954bd7eb787e6575d1eaedb281292a28df8bc8c3eb0c90a8dcff6ee99568dc5e、632336bytesを0x10000へ書きhash検証成功。元appは/private/tmp/x68k-firmware-recovery.ZPwRSY/run-profile-code.bin、診断版は同dirのfm-channel-profile.bin/.elfに保持。タイトルでch0 peak2408、ch1 peak最大1579・TL15・KC96/94/89などの遷移・EG64 Sustain、ch2 peak468・TL29を観測。ch3〜7は出力0。実機にも主旋律の合成出力が存在し、ホストと同程度に小さいことを確認した。[^device-channels]

## タイトル主旋律だけのバランス調整

M5UnifiedのローカルSpeaker_Class.cppはmaster_volumeを二乗して用いる。40は255に対して係数約0.0246になるが、これは音圧の測定ではない。マスターを増やさず、タイトルのleadだけを追加調整した。ファンファーレやSFXの音量へ適用しないため、sound.cのタイトル分岐のlead volumeを18→10へ変更し、platform側の3段音色補償は維持する。最終TLは7。harmony/bass/ADPCMは変更しない。[^design]

候補比較はleadがkey_onの200067sampleに限定し、同じsample位置のdrumsを比較した。追加TL減算0/6/8段でlead RMSは639.068/1075.817/1279.766、drums RMSは2371.136のまま。比率26.95%/45.37%/53.97%となり、目安50〜67%に達した最小の8段を採用した。これは可聴性の保証ではなく、実機へ無制限に増幅候補を出さないための事前基準。

最終実コードで20秒を再実行して候補8とmix WAVがcmp完全一致。drums/bass/harmony WAVも前の倍音候補と完全一致。全20秒lead RMS1024.075、mix peak17241、clip0。タイトル＋36SFX重畳条件もclip0、倍音/音程/上限検査も成功。core試験は607件＋Python17件成功（タイトル初回frame167、44発音、harmony音量32、ゲーム中の旋律停止を追加検査）。just fmt-check、git diff --checkも成功。[^final-balance]

GAME.Xを/private/tmp/x68k-fm-balance.8OpCmpへクロスビルドし、前候補を保持してGAME.Xだけ更新。IPL/HDD全81920セクタのpack一致と実機書込みhash検証成功。診断アプリの毎秒採取をOFFにした通常タイトルで最終確認を進める。聴感の解消はまだ断定しない。

- GAME.X 226118bytes：b73125de90617662707d40ebf7c453c1085fc95a2341b335af49a76122db7c5b
- disk.hdf：c39fcde505626452b00f9f253a8ebbfb677adb9c5229b512103ceeaab13ff50b
- x68kdata.bin 741952bytes：c50b853c2060bc4b1449e293bb30dedf4dd07f31a0b8f7db898b86565113adcd
- sound.c：9ae339cdcdfdc0e70adf5f05a113f49ea0585842f43421c8dec8161b1f06bff0
- エミュレータ差分の保管：build-x68k/cores3-runtime-source-20260906-fm.tgz。e4356d6基点へのsource overlayであり、ROM/HDD/ビルド全体は含まない。

[^device-channels]: cores3-fm-channels.logは直接ファイルへ保存。最終LCDがタイトルであることも親が確認。
[^final-balance]: title-extra-gain{0,6,8}.jsonlとtitle-final-balance.jsonl。追加GAINはホスト試験だけの設定で、通常は0。

最終確認では、毎秒OPM/PC採取をOFFにした診断appで、USB接続20秒後にGPIP/JITをONにしてgameを入力し35秒待機。build-x68k/cores3-fm-balanced.pngでタイトルを親が確認し、現在そのまま再生中。約55秒のログに再生失敗/投入拒否/投入前空/キュー破棄の非ゼロ報告なし。PCM不足は30999〜54494msのsource356864/missing10240（2.789%）で残る。異なるapp配置・異なる計測窓の単発値なので前の1.493%との厳密な因果比較はしない。最終聴感は引き続きユーザー確認待ちであり、目標全体は未完了。エミュレータsource overlayのSHA256は971daef22c33fbe46775b414694bbfe4564ce8e105791048f3b511ccaf78cf64。

## ユーザー依頼による追加約1.5dB（2026-09-06 JST）

前候補についてユーザーから「聞こえません」、続けて「いや、ちょっとメロディーのボリューム上げられる？」「ドラムはよく聞こえる」と報告された。前節の候補選択基準に達しても可聴性の解決には至らなかった。スピーカー全体が無音とは扱わず、依頼どおりタイトル主旋律だけを少し増幅する。マスターやドラムを増幅せず、音源経路の追加診断はこの変更の対象外とする。[^volume-feedback]

sound.cのタイトル専用lead音量を10→8（実TL7→5、約+1.5dB）へ変更。harmony32、bass/ADPCM、ファンファーレ、SFX、曲とテンポ、platformの3段補償は据え置き。test_main.cのタイトル期待値のみ追従。独立read-onlyレビューでもタイトル分岐限定であることを確認した。

just testはC607件＋Python17件成功。実コードを20秒、15625Hz、55.45fpsで合成し、主旋律RMSは1024.075→1218.055、key-on窓RMSは1279.766→1522.180。同じ窓のdrumsは2371.136のまま。mix peak17764、clip0、36設定もclip0。前候補のWAVとADPCM/bass/harmony/SFXがcmp完全一致し、主旋律初回3.011776秒と44発音も維持。just fmt-checkとgit diff --check成功。これらはホストPCM検証であり、実機の聴感を保証しない。[^volume-plus]

前候補/private/tmp/x68k-fm-balance.8OpCmpを保持し、別ディレクトリ/private/tmp/x68k-fm-volume.OdOM4qへクロスビルド。既存ディスクのGAME.Xだけ更新し、IPLとHDD全81920セクタのpack一致を検証した。CoreS3のstorage（0x410000）だけへ741952bytesを書き込み、esptoolのhash照合成功。エミュレータappは954bd7eb…のまま、master40/255・NullServo OFFを維持する。

- GAME.X 226118bytes：2c62ffd6ccee452df1f57b17a35703644f1c1868ea91ae048d4d1d78444594d5
- disk.hdf：fd6ddcec8567689e812307621a494151d45e38eda26911d78f0eef5df70227f6
- x68kdata.bin：b9831bd44b0e24ce71b219dbaf47d1cb8ee75ef9125e3fb071ba3eee9f6a629c
- sound.c：498648a7cdb2d05ebfea89168ea2aaf87116df0697e32cddfa87c45cc8f2d2d9

[^volume-feedback]: 2026-09-06のユーザー発言。音量調整の依頼であり、新候補の聴感承認ではない。
[^volume-plus]: just emu=/private/tmp/x68k-device-capture test-audio-balance 20（通常GAIN=0）、終了0。WAV生成は同条件のtitle-volume-plus指定で別実行し成功。

実機はUSB接続20秒後にGPIP/JITをON、game入力後35秒待機し、cores3-fm-volume-plus.pngでタイトル画面を親が確認した。毎秒OPM採取はOFF、サーボOFF。再生失敗・投入拒否・投入前空・キュー破棄の非ゼロ報告はなかった。30999〜54461ms間のPCM差分はsource358912/missing7680（不足2.095%）であり、供給不足ゼロは未達。通常タイトルで動かしたまま引き渡し、新候補の聴感はユーザー確認待ちとする。[^volume-plus-device]

[^volume-plus-device]: capture-lcdの直接保存ログと同名PNG。マイク音声の録音・聴取ではない。

## 旋律の発音を聴感確認、まだ小さすぎるため追加約3dB（2026-09-06 JST）

追加1.5dB版を反映後、ユーザーが「小さい音でなっている」、続けて「小さすぎる」と報告した。前候補の旋律が聞こえることはユーザー確認済みだが、必要な音量には達していない。発音そのものがないという以前の推測とは区別し、タイトルleadだけをさらに調整する。[^volume3-feedback]

sound.cのタイトルlead volumeを8→4（実TL5→1、約+3dB）へ変更し、test_main.cの期待値を追従。harmony32、bass/ADPCM、ファンファーレ、SFX、曲とテンポ、platformの3段補償を維持した。独立read-onlyレビューでもタイトル限定・下限clampに当たらないことを確認。TL方式で残る増幅余地は1段（約0.75dB）で、これ以上の大きな増幅は単純にこの値を下げ続けても実現しない。

just testはC607件＋Python17件、fmt-check、just --fmt --check、git diff --checkが成功。実コードを同条件で20秒合成し、lead全体RMS1218.055→1722.900、key-on窓RMS1522.180→2153.074（振幅約1.414倍）。同じ窓のdrums RMS2371.136は不変。mix peak19127、clip0、36設定もclip0。ADPCM/bass/harmony/SFXのWAVは前候補とcmp完全一致。初回旋律3.011776秒、44発音も維持する。ホストのclip0は実機スピーカーの歪みや音量満足度を保証しない。[^volume3]

前候補/private/tmp/x68k-fm-volume.OdOM4qを保持し、/private/tmp/x68k-fm-volume3.YrqYJCへ別ビルド。GAME.Xだけ入れ替えたHDDとIPLのpack全内容を照合後、CoreS3のstorage（0x410000）へ741952bytesを書き込み、esptoolのhash照合成功。エミュレータappは変更せず、master40/255・サーボOFFを維持。

- GAME.X 226118bytes：0f930915aa5d43fb164dd4dca23c512416b4323851f5f08b19a922d5cf25debe
- disk.hdf：b943b198a6fb3dbfc67e22cafc3462e27c7ddac8dd160ce42dd2f63070d91984
- x68kdata.bin：f270e1abe195ac39563fbb77691956ca3238c04c108164e53d2486788ece953b
- sound.c：79512cabc34e55c9023ccfbde9a395cfc1901dae4e2041fde94c4b698b479f2a

[^volume3-feedback]: 2026-09-06のユーザー発言はvolume8候補についての確認。今回のvolume4候補への承認ではない。
[^volume3]: just emu=/private/tmp/x68k-device-capture test-audio-balance 20 build-x68k/audio-balance/title-volume3（GAIN=0）、終了0。

USB接続20秒後にGPIP/JITをON、game入力後35秒待機し、cores3-fm-volume3.pngでタイトルを親が確認。毎秒OPM採取OFF、サーボOFF、再生失敗・投入拒否・投入前空・キュー破棄の非ゼロ報告なし。30967〜54429msのPCM差分はsource357888/missing8704（不足2.374%）であり、供給不足の全面解消ではない。通常タイトルで再生する状態で引き渡す。新しい音量が十分かは未確認で、ユーザーの次の聴感評価を待つ。[^volume3-device]

[^volume3-device]: capture-lcdの直接保存ログと同名PNG。画面はLCD読出しであり、カメラやマイクの録音ではない。

## 追加3dBでも不足との報告

ユーザーから「まだ全然音量小さい。メロディーの。」、続けて「トランペットの音色にできる？」と依頼された。volume4候補も十分な音量とは確認できず、TL値を下げるだけの調整を終了。[トランペット風音色と専用出力ゲイン](cores3-fm-trumpet.md)へ検証を継続した。過去のRMS増加を、可聴性の解決済み証拠にはしない。
