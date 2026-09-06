# タイトル右64pxの描き足し

2026-09-06、内蔵imagegenで `build-x68k/cores3-title-65536.png` を編集対象として生成。
保存素材は `title-highcolor-outpaint.png`。生成結果の右端20%だけをゲーム用へ変換する。
左256pxはこの画像から採用せず、既存素材・フォント・目・ロゴ配列を完全に保持する。
横拡大、キャラクターや文字の移動、CRTCタイミング変更は行わない。

## 最終プロンプト

```text
Use case: precise-object-edit
Asset type: X68000 pixel-art game title screen background extension.
Input images: Image 1 is the EDIT TARGET, an exact 320x240 LCD capture. The complete existing title artwork occupies x=0..255. The rightmost 64 columns, x=256..319 (the rightmost 20 percent), are unused black padding.
Primary request: PAINT ONLY INTO THE UNUSED RIGHTMOST 20 PERCENT so the forest, distant landscape and rocky grassy foreground naturally continue to the right edge. This is an outpainting of background, NOT horizontal stretching and NOT a new title design.
Preserve the entire original left 80 percent at its exact location and scale. Do not move the hunter, title logo, menu, copyright, X68000 label or any existing artwork. The hunter must remain identical in face, body, costume and pose. Do not recenter anything.
Extend the conifer tree cut off at x255, the mountain/lake background where visible and the mossy rock/grass in the bottom-right into the 64-column strip. Match every visible seam at x255/256 and the original crisp richly colored 16-bit pixel-art style and illumination. Keep the existing black sky treatment where appropriate, but remove the obvious full-height black sidebar by extending the scenery across it. Add no new characters, props, structures or text.
Output: one complete image in exactly the same 4:3 framing, preferably 1280x960 (4x the logical 320x240), with no crop, no border, no watermark. Keep the logical original left256x240 area unchanged; all newly drawn content must stay in the logical right64x240 area.
```
