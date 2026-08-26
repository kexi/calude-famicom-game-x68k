# 狩人行動 X68000 版の開発環境。
#
# 【方針】ツールはすべてこの devShell が供給する。68000 のクロスコンパイラも
# Python も Nix で固定し、「手元では通るが CI で落ちる」を作らない。
#
# 使い方: `nix develop`、または direnv (.envrc の `use flake`) で自動有効化。
#   shell 内で `just <task>` (例 `just build` / `just test`)。
{
  description = "狩人行動 X68000 版 (68000 クロス開発環境)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      nixpkgs,
      flake-utils,
      ...
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };

        # 68000 のクロスツールチェーン。
        #
        # Why not vasm / vlink (X68000 界隈の定番):
        #   nixpkgs に無い。自前で derivation を書くと、この環境の再現性が
        #   「upstream の tar が生きているか」に依存する。gcc なら
        #   バイナリキャッシュから降ってくる。
        #
        # Why m68k-unknown-linux-gnu (ホスト付きターゲット):
        #   nixpkgs の pkgsCross に bare-metal の m68k-none-elf が無い。
        #   -ffreestanding -nostdlib で libc とスタートアップを一切使わなければ、
        #   出てくるのはただの 68000 コードなので実害が無い。実際に
        #   -m68000 でコンパイルして 68000 の命令だけが出ることを確認済み。
        m68kPkgs = pkgs.pkgsCross.m68k.buildPackages;
      in
      {
        devShells.default = pkgs.mkShell {
          packages = [
            # --- 68000 クロスコンパイル ---
            m68kPkgs.gcc
            m68kPkgs.binutils

            # --- ホスト側 (core/ のネイティブテスト) ---
            # core/ はプラットフォーム非依存の C なので、Mac 上で直接
            # ビルドしてテストできる。これが一番回転の速い検証ループになる。
            pkgs.clang
            pkgs.clang-tools

            # --- tools/ のスクリプト (アセット変換・elf2x) ---
            pkgs.python3
            pkgs.uv

            # --- タスクランナーと開発フロー ---
            pkgs.just
            pkgs.lefthook
            pkgs.gitleaks
            pkgs.fd
          ];

          shellHook = ''
            echo "[calude-kodo-x68k] $(m68k-unknown-linux-gnu-gcc --version | head -1)"
            echo "[calude-kodo-x68k] just --list でタスク一覧"
          '';
        };
      }
    );
}
