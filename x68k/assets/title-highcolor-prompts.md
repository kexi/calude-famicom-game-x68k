# 多色タイトル素材の生成記録

2026-09-06、built-in image_genで既存タイトルから編集。元画像・既存NES資産は保全。

- `title-highcolor.png`: 通常背景。実装は256×240へnearest縮小してGRB16化。
- `title-highcolor-closed.png`: 瞼を閉じた差分候補。実装は目の2小領域だけを採用。
- メニュー・著作権・X68000はゲームの既存字形を生成時に合成。
- 原画はAI生成素材であり、以下のprompt通りの完全な画素同一性を保証するものではない。実際の各相は描画テスト画像で確認する。

## 通常背景の最終プロンプト

```text
Use case: identity-preserve
Asset type: editable background artwork for an actual X68000 game title screen, final game resolution 256x240 (aspect ratio 16:15).
Image 1 is the edit target. Recolor and enrich this exact title artwork from its restricted old 16-color palette into a vivid richly shaded 16-bit high-color pixel-art title. Preserve the exact composition, object silhouettes, character identity, face position, pose, costume, bow, tree, snowy mountains and landscape, and the Japanese gold title logo. Do not replace this with a new scene. Keep disciplined crisp pixel-art forms, not photographic or smooth vector art. Use many nuanced colors and subtle pixel shading: luminous gold/copper logo and hair, rich brown leather, violet scarf, warm natural skin, cool snowy peaks, lush varied green forest, azure lake. Maintain the black negative space of the original sky and menu area. Keep the face in the exact original location (original 256x240 coordinates x184..216, y56..80) with the same open-eyed expression. Do not reposition or resize any features.
Preserve the Japanese logo text exactly "狩人行動" and its small "KARYUDO" subtitle in their original upper-left positions. For runtime UI, REMOVE ONLY the small "START", "CONTINUE", "OPTION" text and triangular cursor from the mid-left area, and remove the bottom-left copyright text and "X68000"; leave those specific text areas clean black. These are drawn precisely by the game after loading the artwork. Preserve surrounding original scenery. No added wording, no frame, no border, no watermark, no timestamp. Match the original 16:15 image bounds with no padding. Output a full-color PNG, preferably 1024x960.
```

## 閉眼差分の最終プロンプト

```text
Use case: precise-object-edit
Image 1 is the edit target: a completed pixel-art title background. Create its blinking animation frame.
Change ONLY the two eyelids of the hunter in the original face position, so both eyes are gently fully closed. Draw natural slim dark eyelash curves in place of the eyes, with matching warm skin color where the open eyes were. Preserve the nose, mouth, eyebrows, hair, face contour, lighting, all colors, and every other part of the image as exactly as possible. The image will be registered pixel-for-pixel with the original: do NOT shift, scale, reframe or redesign anything. Keep image bounds and aspect ratio identical. The entire eye edit must fit within the area corresponding to x184..216, y56..80 on a 256x240 screen. Do not alter any text, logo, scenery, costume, or pose. This is a single surgical eyelid edit, not a new illustration. Return one full-color PNG of the same complete image, with no border or extra text.
```
