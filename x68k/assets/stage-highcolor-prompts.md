# ゲーム遠景の高色素材

内蔵 imagegen で生成。`stage-highcolor-atlas.png` の四象限を左上・右上・
左下・右下の順に各 320×240 へ変換して使う。素材は遠景だけで、
ゲームの地形・当たり判定・キャラクター・HUD は含まない。

## 最終プロンプト

```text
Use case: stylized-concept
Asset type: production background sprite atlas for a side-scrolling X68000 action game, four backgrounds in a clean 2 by 2 grid.
Primary request: vivid high-color mountain-and-sky scenery, replacing flat single-color mountains. Output a landscape 4:3 image, preferably 1448 by 1086 pixels. Four equally sized edge-to-edge panels, with absolutely NO borders, gutters, dividing lines, labels, or text. Each quadrant is a complete 4:3 landscape.
Composition in EVERY panel: level side-on view for a 2D platformer; upper 55 percent is quiet sky with subtle wispy clouds; layered mountain peaks rise between 55 and 72 percent of panel height, mid-distance wooded ridges fade toward 80 percent; lowest 20 percent becomes a dark deep shadow/haze so dangerous gaps below the game ground remain readable. No foreground ground platforms, ledges, bricks, roads, buildings, animals, characters, game objects, HUD, or lettering. Keep the uppermost 14 percent dark and low-detail for a cream-colored game HUD.
Style: finely shaded 1990s Japanese computer-game painted pixel-art landscape, crisp little clusters and many rich shades, not a photo and not smooth 3D. Atmospheric depth, jagged rocky ridgelines with plausible directional shading, distant bluish haze, small dark conifer silhouettes lower down. Beautiful color nuance that survives reduction to 320 by 240 pixels. Readable background subordinate to small bright pixel-art actors.
Quadrant upper left: blue mountain ranges beneath a deep indigo-to-blue dawn sky, soft cool light on stone.
Quadrant upper right: violet mountain ranges beneath a dark violet dusk sky with a restrained warm cloud glow.
Quadrant lower left: teal and blue-green forested mountain ranges, cool mist, dark cyan sky.
Quadrant lower right: plum and burgundy mountains at late twilight, subdued copper rim light and dark wine-colored sky.
Constraints: all four panels have the same horizon scale and structure, but distinct mountain shapes and the stated color identity. Full bleed, no frames or decorative dividers. No text, no watermark. Do not draw the game foreground; these are distant backdrops only.
```
