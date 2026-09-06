# 16BIT前景素材の生成記録

内蔵imagegenを使用。主人公の参照は既存 `title-highcolor.png`。
髪・紫のマフラー・革装備・弓をタイトルと統一する依頼に基づく。
初版の透過指定はalphaなしの市松模様となり、背景除去の再試行も背景が混入した。
透過成功とは記録せず、原画を保持したまま単色magenta版を生成して採用した。
ゲームの素材コンパイラはこのkeyを透明word0とし、不透明な黒はword1へ分離する。

採用原画は `hero-highcolor-keyed.png` と `objects-highcolor-atlas.png`。
`hero-highcolor-atlas.png` は初版参照として保持。再試行の不採用画像は配布しない。
原作4BIT素材は変更しない。各pose/slotとcrop座標は `../tools/mkforeground.py`。

## 主人公12ポーズ

Use case: stylized-concept
Asset type: production sprite animation atlas for a 320x240 X68000 game.
Primary request: A sprite sheet of the SAME orange-ponytailed female archer from the reference title. The reference is a character/design reference only, not an edit target. Preserve her orange high ponytail with green ribbon, violet-purple scarf, brown leather armor/skirt/gauntlets, pale thighs, fur-cuffed brown boots, and wooden bow. Do not substitute the original NES orange monochrome character.
Composition: EXACTLY 6 equal-width columns and 2 equal-height rows, 12 isolated full-body poses, on a genuinely TRANSPARENT background with alpha. Each upright cell fits a 1:2 character canvas, all feet/heads same scale, generous transparent gap between cells. Each cell will be reduced to 16x32 pixels; use crisp chunky silhouettes and rich deliberate highlight/shadow pixel clusters, not blurry painting or tiny decorative detail.
Reading order, row 1: standing facing RIGHT; run contact right foot; run passing; run contact left foot; run passing opposite; jumping rising.
Row 2: jumping apex; falling; aiming bow straight RIGHT; releasing bow straight RIGHT; recovering bow shot; defeated lying on her side horizontally (last cell only, will be 32x16).
Style: high-color 1990s Japanese computer pixel art, shaded brown leather, vivid purple scarf, copper orange hair, warm skin, dark outlines and bright highlights. Consistent side view appropriate for a platform game. All normal frames face RIGHT; no left facing frames. Real coherent animation poses, not duplicate standing figures.
Constraints: exactly twelve figures, no labels, no lettering, no logo, no grid lines, no checkerboard painted into background, no scenery, no shadow outside figure. Keep the hero identity and outfit of the provided title image.

## 透過再試行（不採用）

Use case: background-extraction. Edit target: the provided twelve-pose archer sprite atlas. Remove ONLY the baked-in white/light-gray checkerboard background. Return all twelve sprites in EXACTLY the same 1536x1024 pixel positions and with EXACTLY the same poses, scale, colors, details and layout, on a genuinely TRANSPARENT PNG background with actual alpha=0 outside the characters. Do not paint a checkerboard, white, black, green or any replacement background. Preserve copper orange ponytail, green ribbon, purple scarf, leather outfit, bow, pixel clusters and every figure unchanged. No added text or grid.

## ゲーム取り込み用キー色

Use case: precise-object-edit. The provided sprite atlas is the edit target. Preserve ALL twelve figures exactly: identical position, scale, pose, color, hair, scarf, armor, bow, pixel art detail and 1536x1024 layout. Change ONLY the checkerboard background into ONE perfectly uniform flat chroma-key color RGB(255,0,255), hexadecimal #FF00FF. Pure opaque bright magenta everywhere outside sprites, including holes between arms and bows. NO gradients, scenery, shadows or texture in the background. No checkerboard. No glow. No text. This flat magenta will be interpreted as transparent by the game's sprite compiler. Do not use magenta inside the character; her scarf stays violet blue.

## 敵・アイテム・地形

Use case: stylized-concept
Asset type: production game enemy, item, effect and terrain sprite atlas.
Primary request: high-color 1990s Japanese computer pixel-art versions of the flame creatures, bats and terrain from the supplied old game screenshot. The screenshot is a subject reference, not an edit target. Match the richly shaded pixel art style of the supplied title illustration, but DO NOT include the heroine or title text.
Composition: EXACTLY 6 equal columns and 4 equal rows (24 cells), on perfectly uniform opaque chroma-key magenta #FF00FF. Each sprite isolated and centered fully INSIDE its cell with large magenta margins. No grid lines, labels or lettering. Each asset will be sampled down to 16x16 pixels (boss32x32, little icons8x8): chunky readable silhouettes with many purposeful colored highlight/shadow clusters.
Cell reading order, left-to-right top-to-bottom:
Row 1: small golden orange flame monster with white face and blue eyes; same flame monster hurt expression; green bat wings down; green bat wings spread up; gray petrified flame monster; gray petrified bat.
Row 2: violet purple bat wings down; violet purple bat wings spread up; LARGE golden flame boss with white face and blue eyes matching screenshot; golden five-point invincibility star; red power-up jewel; tiny orange-ponytailed heroine head icon with violet scarf matching title.
Row 3: horizontal wooden arrow pointing RIGHT; vertical wooden arrow pointing UP; small golden sparkle burst; larger orange-and-gold defeat burst; round golden coin with bright blue jewel center; a solid square terrain tile of green grass top over brown rock soil (full square).
Row 4: a seamless square brown soil-rock texture tile; a square reddish stone platform block with beveled gold rim and masonry detail; red pennant flag on the LEFT of a golden vertical pole at the RIGHT; plain thin golden vertical flag pole; golden flagpole mounted on grass-covered earth base; blue checkpoint pennant on the LEFT of a golden vertical pole at the RIGHT.
Style: crisp pixel art with dark outline, strong shading, detailed copper/gold highlights, faceted rocks, vivid emerald grass, real material depth, not flat NES 3-color recoloring. Keep every sprite separate. Background exactly #FF00FF even inside holes between wings and pole. Terrain texture squares may be opaque within their square; magenta outside. No cast shadows or glow on backdrop. No scenery, no UI, no text.

