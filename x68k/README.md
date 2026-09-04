# X68000 / x68k-stackchan版

NES版の全4ステージ・ボス・エンディング、原作画像、タイトルとラウンド演出、HUD、BGM/SFXを移植した版です。対象はx68k-stackchanのHuman68k環境です。物理X68000では未検証で、FM音程表とADPCM転送は現行エミュレータに合わせています。

## 遊ぶ

開発ツールはリポジトリのNix環境を使用します。隣の`x68k-stackchan`にビルド済みエミュレータと既存のROM/HDDが必要です。別の場所なら`X68K_STACKCHAN`を指定してください。

```sh
nix develop
just play
```

A/Dで移動、Kでジャンプ、Jで矢、Enterで開始・ポーズ、W/Sでタイトル選択・矢の方向指定を行います。ゲームオーバー後のCONTINUEは直前の面から再開します。OPTIONは原作同様、項目表示のみです。

`just build`で通常版`build-x68k/GAME.X`を生成します。デバッグ表示は入りません。

macOSでは`just screenshot-title`で通常版を実際に起動し、タイトル画面を`build-x68k/title.png`に保存できます（256×240、無拡大）。

## 検証

```sh
just test
just solve
just test-video
just e2e
just fmt-check
```

`test-video`は本番のMMIO描画・音声転送をエミュレータへ通し、画像を`build-x68k/visual/`へ保存します。`e2e`は数値検証用HUDを有効にしたGAME.Xを作るため、その後に通常版へ戻すには`just build`または`just play`を使います。

詳細な検証根拠と原作・実機との差は[移植状況](../knowledge/x68k-port-status.md)に記録しています。NESの約60Hzに対し55.45Hzで動くため、フレーム定数は同じでも実時間は異なります。音はFM/ADPCM向けのアレンジで、APU波形の完全再現ではありません。
