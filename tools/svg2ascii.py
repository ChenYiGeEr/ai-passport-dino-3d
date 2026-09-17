#!/usr/bin/env python3
# svg2ascii.py — 把 SVG 渲染成小尺寸位图，再转 ASCII 稿
# 依赖: pip install cairosvg pillow numpy
import io

import sys
import numpy as np
from PIL import Image
import cairosvg

SVG_PATH = "shield.svg"
TARGET_W, TARGET_H = 22, 20   # 跑道用 22×20；HUD 用 11×10 改这里即可
# TARGET_W, TARGET_H = 11, 10

# 1. SVG → PNG (内存)
png_bytes = cairosvg.svg2png(url=SVG_PATH, output_width=TARGET_W, output_height=TARGET_H)
img = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
arr = np.array(img)  # (H, W, 4)

# 2. 透明度阈值分层
alpha = arr[:, :, 3]
# 三档：不透明 / 半透明 / 透明
opaque = alpha > 200
semi   = (alpha > 50) & (alpha <= 200)

# 3. 生成 ASCII：@=不透明实心, +=半透明边缘, .=空
char_map = np.full((TARGET_H, TARGET_W), '.', dtype='U1')
char_map[opaque] = '@'
char_map[semi]   = '+'

# 4. 输出 bake_sprites.js 可直接复制的数组格式
rows = ['"' + ''.join(row) + '"' for row in char_map]
print("const SHIELD = [")
print(",\n".join(f"    {r}" for r in rows))
print("];")
print(f"// {TARGET_W}x{TARGET_H}")