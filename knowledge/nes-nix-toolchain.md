---
type: Attested Computation
title: NESビルドツールのNix管理
description: cc65とGNU Makeを既存devShellへ追加し、Homebrewなしで原作NESの再ビルドを検証。
status: stable
generated: { by: codex, at: 2026-09-06T05:23:00Z }
verified:
  - { by: process:nix-develop-nes-build, at: 2026-09-06T05:19:00Z }
sources:
  - id: environment
    resource: ../flake.nix
  - id: lock
    resource: ../flake.lock
  - id: build
    resource: ../Makefile
---

# 設定

gameリポジトリのdevShellへ `pkgs.cc65` と `pkgs.gnumake` を追加した。
README日英のHomebrew手順を `nix develop` に置き換えた。
direnvも既存 `.envrc` の同じ環境を使う。グローバルなdotfiles設定や
Homebrewのインストール状態は変更していない。[^environment]

既存flake.lockのnixpkgs revision
`56c02bc00adcf003215cc4bd996d6efaf4cff188` を維持し、入力更新は行わない。
lockのlastModifiedは今回より1日以上古い。[^lock]

# 検証

`nix develop --command sh -c 'command -v ca65; ca65 --version; command -v ld65; ld65 --version; command -v make; make --version | head -1; make -B BUILD=build-x68k/nes-check ROM=build-x68k/nes-check.nes'`
が終了コード0で成功した。ca65/ld65/MakeはいずれもNix storeの実行ファイル。
パッケージ名はcc65-2.19だがCLI自己表示は `V2.18 - N/A` だったため、
自己表示を2.19と読み替えない。GNU Makeは4.4.1。

元のMakefile・6502ソースを変更せず、専用の無視対象ディレクトリへ49168bytesの
NES ROMを再生成した。既存のgame.nesと通常ビルドディレクトリを上書きしない。[^build]

[^environment]: `sources.environment`
[^lock]: `sources.lock`
[^build]: `sources.build`
