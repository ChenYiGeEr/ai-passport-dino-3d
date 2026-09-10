#!/usr/bin/env node
/**
 * bake_sprites.js — 把 dino3d 的 .vox 体素模型烘焙成 C 精灵表。
 *
 * 输入: /Users/lim/code/GitHub/dino3d/objects/... 下的 .vox 文件
 * 输出: main/sprites.h + main/sprites.c (8bpp 调色板索引像素, RGB565 日/夜双调色板)
 * 另外输出 /tmp/sprites-preview.ppm 供人工检查烘焙效果。
 *
 * 用法: node tools/bake_sprites.js
 */
'use strict';

const fs = require('fs');
const path = require('path');

const DINO3D = '/Users/lim/code/GitHub/dino3d/objects';
const OUT_H = path.join(__dirname, '..', 'main', 'sprites.h');
const OUT_C = path.join(__dirname, '..', 'main', 'sprites.c');

/* ---------------- .vox 解析 ---------------- */

function parseVox(file) {
    const buf = fs.readFileSync(file);
    if (buf.toString('ascii', 0, 4) !== 'VOX ') throw new Error('not vox: ' + file);
    let off = 8;
    let size = null, voxels = null, palette = null;

    function readChunk() {
        const id = buf.toString('ascii', off, off + 4);
        const content = buf.readUInt32LE(off + 4);
        const children = buf.readUInt32LE(off + 8);
        const start = off + 12;
        off = start + content + children;
        return { id, start, content };
    }

    // MAIN
    readChunk(); // skip MAIN header itself (content=0), children parsed below via loop
    off = 20;    // MAIN: id(4)+content(4)+children(4) = 12, children start at 20
    while (off < buf.length) {
        const id = buf.toString('ascii', off, off + 4);
        const content = buf.readUInt32LE(off + 4);
        const children = buf.readUInt32LE(off + 8);
        const start = off + 12;
        if (id === 'SIZE') {
            size = { x: buf.readUInt32LE(start), y: buf.readUInt32LE(start + 4), z: buf.readUInt32LE(start + 8) };
        } else if (id === 'XYZI') {
            const n = buf.readUInt32LE(start);
            voxels = [];
            for (let i = 0; i < n; i++) {
                voxels.push({
                    x: buf[start + 4 + i * 4],
                    y: buf[start + 4 + i * 4 + 1],
                    z: buf[start + 4 + i * 4 + 2],
                    c: buf[start + 4 + i * 4 + 3],
                });
            }
        } else if (id === 'RGBA') {
            palette = [];
            for (let i = 0; i < 256; i++) {
                palette.push({
                    r: buf[start + i * 4], g: buf[start + i * 4 + 1], b: buf[start + i * 4 + 2],
                });
            }
        }
        off = start + content + children;
    }
    if (!voxels) throw new Error('no XYZI in ' + file);
    return { size, voxels, palette };
}

/* ---------------- 斜投影渲染 + 方向性明暗 ----------------
 * MagicaVoxel 坐标: x=右, y=纵深, z=上。
 * 投影: sx = x*K + y*(K/2), sy = -z*K + y*(K-1)(接近原版的 45° 俯视)。
 * 观察者位于 y 负方向, 遮挡顺序: y 降序(远->近), 同 y 时 z 升序。
 *
 * 明暗(伪 3D 立体感的关键, 对齐原版的光照观感):
 *   顶面(z+1 无邻居) ×1.30 亮
 *   右面(x+1 无邻居) ×0.70 暗
 *   其余               ×1.00
 */
const K = 2; // 主精灵: 每个体素渲染成 K x K 像素块

function shade(c, f) {
    return {
        r: Math.min(255, Math.round(c.r * f)),
        g: Math.min(255, Math.round(c.g * f)),
        b: Math.min(255, Math.round(c.b * f)),
    };
}

function renderSprite(model, k) {
    k = k || K;
    const occ = new Set(model.voxels.map(v => v.x + ',' + v.y + ',' + v.z));
    const sorted = model.voxels.slice().sort((a, b) => (b.y - a.y) || (a.z - b.z) || (a.x - b.x));
    let minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    const pts = sorted.map(v => {
        const sx = v.x * k + v.y * (k >> 1);
        const sy = -v.z * k + v.y * (k - 1);
        minX = Math.min(minX, sx); maxX = Math.max(maxX, sx + k - 1);
        minY = Math.min(minY, sy); maxY = Math.max(maxY, sy + k - 1);
        const top = !occ.has(v.x + ',' + v.y + ',' + (v.z + 1));
        const right = !occ.has((v.x + 1) + ',' + v.y + ',' + v.z);
        const front = !occ.has(v.x + ',' + (v.y - 1) + ',' + v.z);
        return { sx, sy, c: v.c, top, right, front };
    });
    const w = maxX - minX + 1, h = maxY - minY + 1;
    // 输出 16bpp RGB565, 0x0000 = 透明(真正的纯黑改为 0x0841 近黑)
    const px = new Uint16Array(w * h);
    for (const p of pts) {
        const base = model.palette[p.c - 1];
        const f = p.top ? 1.30 : p.right ? 0.70 : 1.00;
        const c = shade(base, f);
        let v16 = ((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3);
        if (v16 === 0) v16 = 0x0841;
        const x0 = p.sx - minX, y0 = p.sy - minY;
        for (let dy = 0; dy < k; dy++)
            for (let dx = 0; dx < k; dx++)
                px[(y0 + dy) * w + x0 + dx] = v16;
    }
    return { w, h, px };
}

// 风滚草运行时渲染器不支持旋转；在透明方形画布上离线烘焙 8 个滚动角度。
function rotateSprite(sprite, angle) {
    const side = Math.ceil(Math.sqrt(sprite.w * sprite.w + sprite.h * sprite.h));
    const px = new Uint16Array(side * side);
    const cos = Math.cos(angle), sin = Math.sin(angle);
    const srcCx = (sprite.w - 1) / 2, srcCy = (sprite.h - 1) / 2;
    const dstC = (side - 1) / 2;
    for (let y = 0; y < side; y++) for (let x = 0; x < side; x++) {
        const dx = x - dstC, dy = y - dstC;
        const sx = Math.round(cos * dx + sin * dy + srcCx);
        const sy = Math.round(-sin * dx + cos * dy + srcCy);
        if (sx >= 0 && sy >= 0 && sx < sprite.w && sy < sprite.h)
            px[y * side + x] = sprite.px[sy * sprite.w + sx];
    }
    return { w: side, h: side, px };
}

/* ---------------- 要烘焙的模型清单 ----------------
 * name: C 符号名; file: 相对 objects/ 的路径
 */
const MODELS = [];
// 恐龙: 站立/奔跑/跳跃帧
for (let i = 0; i <= 7; i++) MODELS.push({ name: `dino_${i}`, file: `t-rex/${i}.vox` });
// 恐龙: 下蹲帧
for (let i = 0; i <= 7; i++) MODELS.push({ name: `dino_down_${i}`, file: `t-rex/band/${i}.vox` });
// 恐龙: 死亡
MODELS.push({ name: 'dino_dead', file: 't-rex/other/wow.vox' });
MODELS.push({ name: 'dino_dead_down', file: 't-rex/other/wow-down.vox' });
// 仙人掌
for (const [n, f] of [['cactus', 'cactus'], ['cactus_tall', 'cactus_tall'], ['cactus_thin', 'cactus_thin'],
                      ['fcactus', 'fcactus'], ['fcactus_tall', 'fcactus_tall'], ['fcactus_thin', 'fcactus_thin']])
    MODELS.push({ name: n, file: `cactus/${f}.vox` });
// 翼龙(取 0/3 两帧扑翼)
MODELS.push({ name: 'ptero_0', file: 'ptero/0.vox' });
MODELS.push({ name: 'ptero_1', file: 'ptero/3.vox' });
// 地面装饰
for (let i = 0; i <= 4; i++) MODELS.push({ name: `rock_${i}`, file: `rocks/${i}.vox` });
for (let i = 0; i <= 2; i++) MODELS.push({ name: `flower_${i}`, file: `flowers/${i}.vox` });
MODELS.push({ name: 'skull', file: 'misc/desert_skull.vox' });
MODELS.push({ name: 'scorpion', file: 'misc/scorpion.vox' });
// tumbleweed 在下方单独扩成 8 个离线旋转帧。
// 远景元素(K=1 小号, 放在远河岸/天边, 纯装饰不碰撞)
MODELS.push({ name: 'tree_green_far', file: 'misc/trees/green.vox', k: 2 });
MODELS.push({ name: 'tree_dead_far', file: 'misc/trees/dead.vox', k: 2 });
MODELS.push({ name: 'cactus_far_0', file: 'misc/cactus/0.vox', k: 1 });
MODELS.push({ name: 'cactus_far_1', file: 'misc/cactus/2.vox', k: 1 });
MODELS.push({ name: 'cactus_far_2', file: 'misc/cactus/4.vox', k: 1 });
MODELS.push({ name: 'skull_far', file: 'misc/desert_skull.vox', k: 1 });

/* ---------------- 烘焙(16bpp RGB565 直出, 已含方向性明暗) ---------------- */

const models = MODELS.map(m => {
    const model = parseVox(path.join(DINO3D, m.file));
    const sp = renderSprite(model, m.k);
    console.log(`${m.name}: ${sp.w}x${sp.h} (${m.file})`);
    return { name: m.name, w: sp.w, h: sp.h, px: sp.px };
});
{
    const source = renderSprite(parseVox(path.join(DINO3D, 'misc/tumbleweed.vox')));
    for (let i = 0; i < 8; i++) {
        const sp = rotateSprite(source, i * Math.PI / 4);
        models.push({ name: `tumbleweed_${i}`, w: sp.w, h: sp.h, px: sp.px });
        console.log(`tumbleweed_${i}: ${sp.w}x${sp.h} (misc/tumbleweed.vox)`);
    }
}
const allModels = models; // const 引用, 下面 push 手绘红心

/* ---------------- 手绘红心(原版无此模型, ASCII 稿) ---------------- */
function rgb565num(r, g, b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
{
    const HEART = [
        '..xx...xx..',
        '.xXXx.xXXx.',
        'xXXXXXXXXXXx',
        'xXXXXXXXXXXx',
        'xXXXXXXXXXXx',
        '.xXXXXXXXXx.',
        '..xXXXXXXx..',
        '...xXXXXx...',
        '....xXXx....',
        '.....xx.....',
    ];
    const hh = HEART.length, ww = HEART[0].length;
    const hpx = new Uint16Array(ww * hh);
    HEART.forEach((row, y) => [...row].forEach((ch, x) => {
        if (ch === 'X') hpx[y * ww + x] = rgb565num(220, 40, 60);
        else if (ch === 'x') hpx[y * ww + x] = rgb565num(150, 20, 40);
    }));
    models.push({ name: 'heart', w: ww, h: hh, px: hpx });
    console.log(`heart: ${ww}x${hh} (hand-drawn)`);
}

/* ---------------- 输出 C 代码 ---------------- */

let h = `// AUTO-GENERATED by tools/bake_sprites.js — 勿手改, 改工具后重新烘焙。
#pragma once
#include <stdint.h>

typedef struct {
    uint16_t w;
    uint16_t h;
    const uint16_t *px; // RGB565, 0x0000 = 透明
} sprite_t;

`;
let c = `// AUTO-GENERATED by tools/bake_sprites.js
#include "sprites.h"

`;

for (const m of models) {
    h += `extern const sprite_t spr_${m.name};\n`;
    c += `static const uint16_t px_${m.name}[${m.w * m.h}] = {\n`;
    for (let i = 0; i < m.px.length; i += 12) {
        c += '    ' + Array.from(m.px.slice(i, i + 12)).map(v => '0x' + v.toString(16).padStart(4, '0')).join(',') + ',\n';
    }
    c += `};\nconst sprite_t spr_${m.name} = { ${m.w}, ${m.h}, px_${m.name} };\n\n`;
}

fs.writeFileSync(OUT_H, h);
fs.writeFileSync(OUT_C, c);
console.log('wrote', OUT_H, 'and', OUT_C);

/* ---------------- 预览图 (PPM, 可用 sips 转 PNG 检查) ---------------- */
{
    const W = 640, H = 400;
    const img = Buffer.alloc(W * H * 3, 240);
    function blit(m, ox, oy) {
        for (let y = 0; y < m.h; y++) for (let x = 0; x < m.w; x++) {
            const v = m.px[y * m.w + x];
            if (!v) continue;
            const o = ((oy + y) * W + ox + x) * 3;
            img[o] = ((v >> 11) & 31) * 255 / 31;
            img[o + 1] = ((v >> 5) & 63) * 255 / 63;
            img[o + 2] = (v & 31) * 255 / 31;
        }
    }
    const pick = n => models.find(m => m.name === n);
    for (let i = 0; i <= 7; i++) blit(pick(`dino_${i}`), 10 + i * 60, 10);
    for (let i = 0; i <= 7; i++) blit(pick(`dino_down_${i}`), 10 + i * 75, 80);
    blit(pick('dino_dead'), 10, 150);
    blit(pick('cactus'), 120, 150); blit(pick('cactus_tall'), 170, 150); blit(pick('cactus_thin'), 220, 150);
    blit(pick('fcactus'), 270, 150); blit(pick('fcactus_tall'), 320, 150); blit(pick('fcactus_thin'), 370, 150);
    blit(pick('ptero_0'), 430, 150); blit(pick('ptero_1'), 500, 150);
    for (let i = 0; i <= 4; i++) blit(pick(`rock_${i}`), 10 + i * 30, 230);
    for (let i = 0; i <= 2; i++) blit(pick(`flower_${i}`), 180 + i * 20, 230);
    blit(pick('skull'), 260, 220); blit(pick('scorpion'), 340, 230);
    for (let i = 0; i < 8; i++) blit(pick(`tumbleweed_${i}`), 400 + i * 28, 225 + (i & 1) * 42);
    blit(pick('tree_green_far'), 480, 200); blit(pick('tree_dead_far'), 540, 200);
    blit(pick('cactus_far_0'), 480, 260); blit(pick('cactus_far_1'), 510, 260); blit(pick('cactus_far_2'), 540, 260);
    blit(pick('skull_far'), 570, 260);
    fs.writeFileSync('/tmp/sprites-preview.ppm', Buffer.concat([Buffer.from(`P6\n${W} ${H}\n255\n`), img]));
    console.log('preview: /tmp/sprites-preview.ppm');
}
