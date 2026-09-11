#!/usr/bin/env node
/**
 * bake_sprites.js — 把 dino3d 的 .vox 体素模型烘焙成 C 精灵表。
 *
 * 输入: /Users/lim/code/GitHub/dino3d/objects/... 下的 .vox 文件
 * 输出: main/sprites.h + main/sprites.c (16bpp RGB565, 运行时统一叠加夜色)
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

// T-Rex 源模型的三档绿色材质在所有动作帧中共用这些索引。
// 只替换材质色，不动近黑眼睛和死亡帧的白色细节。
function recolorDino(model) {
    const palette = model.palette.map(c => ({ ...c }));
    const set = (index, hex) => {
        palette[index - 1] = {
            r: (hex >> 16) & 0xff,
            g: (hex >> 8) & 0xff,
            b: hex & 0xff,
        };
    };
    set(249, 0xB98236); // 主体赭黄
    set(251, 0xA66F30); // 次级焦糖棕
    set(217, 0x6F4825); // 暗面深土棕
    set(241, 0x6F4825); // 同一暗面在部分动作帧使用另一索引
    return { ...model, palette };
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

// 运行时不做缩放：默认 K=2 主体先离线缩到 3/4，得到约 1.5 像素/体素的等效尺寸。
function scaleSpriteNearest(sprite, num, den) {
    const w = Math.max(1, Math.round(sprite.w * num / den));
    const h = Math.max(1, Math.round(sprite.h * num / den));
    const px = new Uint16Array(w * h);
    for (let y = 0; y < h; y++) {
        const sy = Math.min(sprite.h - 1, Math.floor(y * sprite.h / h));
        for (let x = 0; x < w; x++) {
            const sx = Math.min(sprite.w - 1, Math.floor(x * sprite.w / w));
            px[y * w + x] = sprite.px[sy * sprite.w + sx];
        }
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
// 翼龙: 使用全部六个原始扑翼帧，运行时固定 420ms 周期。
for (let i = 0; i < 6; i++) MODELS.push({ name: `ptero_${i}`, file: `ptero/${i}.vox` });
// 地面装饰
for (let i = 0; i <= 4; i++) MODELS.push({ name: `rock_${i}`, file: `rocks/${i}.vox` });
for (let i = 0; i <= 2; i++) MODELS.push({ name: `flower_${i}`, file: `flowers/${i}.vox` });
MODELS.push({ name: 'scorpion', file: 'misc/scorpion.vox' });
// 跑道纵深层：远层 K=1，近层只选少量石块/花草 K=3，控制 flash 和合成成本。
for (const [n, f] of [['rock_far_0', 'rocks/0.vox'], ['rock_far_2', 'rocks/2.vox'],
                      ['flower_far_0', 'flowers/0.vox'], ['flower_far_2', 'flowers/2.vox']])
    MODELS.push({ name: n, file: f, k: 1 });
for (const [n, f] of [['rock_near_0', 'rocks/0.vox'], ['rock_near_2', 'rocks/2.vox'],
                      ['flower_near_0', 'flowers/0.vox'], ['flower_near_2', 'flowers/2.vox']])
    MODELS.push({ name: n, file: f, k: 3 });
// tumbleweed 在下方单独扩成 8 个离线旋转帧。
// 远景元素: 沙漠只使用高仙人掌，避免树木与场景设定冲突。
// K=3 让仙人掌高度约 70–90px，与原树的视觉比例相当。
for (const [n, f] of [['cactus_far_tall_0', 'misc/cactus/0.vox'],
                      ['cactus_far_tall_1', 'misc/cactus/2.vox'],
                      ['cactus_far_tall_2', 'misc/cactus/4.vox']])
    MODELS.push({ name: n, file: f, k: 3 });
// 保留小号仙人掌给其它离线素材/调试使用。
MODELS.push({ name: 'cactus_far_0', file: 'misc/cactus/0.vox', k: 1 });
MODELS.push({ name: 'cactus_far_1', file: 'misc/cactus/2.vox', k: 1 });
MODELS.push({ name: 'cactus_far_2', file: 'misc/cactus/4.vox', k: 1 });

/* ---------------- 烘焙(16bpp RGB565 直出, 已含方向性明暗) ---------------- */

const models = MODELS.map(m => {
    let model = parseVox(path.join(DINO3D, m.file));
    if (m.name.startsWith('dino_')) model = recolorDino(model);
    let sp = renderSprite(model, m.k);
    if (m.k === undefined) sp = scaleSpriteNearest(sp, 3, 4);
    console.log(`${m.name}: ${sp.w}x${sp.h} (${m.file})`);
    return { name: m.name, w: sp.w, h: sp.h, px: sp.px };
});

// 现有模型提供八个稳定姿势；补四个轻微位移的中间姿势，保持离线烘焙且不改变碰撞锚点。
for (let i = 0; i < 4; i++) {
    const base = models.find(m => m.name === `dino_${i}`);
    const px = new Uint16Array(base.px.length);
    for (let y = 0; y < base.h; y++) for (let x = 0; x < base.w; x++) {
        const src = x > 0 ? base.px[y * base.w + x - 1] : 0;
        px[y * base.w + x] = src;
    }
    models.push({ name: `dino_${i + 8}`, w: base.w, h: base.h, px });
}

// 沙漠植物使用小幅 ASCII 稿，避免为几百像素引入新的外部 .vox 依赖。
function addAsciiSprite(name, rows, scale, colors) {
    const sourceW = Math.max(...rows.map(row => row.length));
    const w = sourceW * scale;
    const h = rows.length * scale;
    const px = new Uint16Array(w * h);
    rows.forEach((row, y) => [...row.padEnd(sourceW, '.')].forEach((ch, x) => {
        const color = colors[ch];
        if (!color) return;
        for (let dy = 0; dy < scale; dy++)
            for (let dx = 0; dx < scale; dx++)
                px[(y * scale + dy) * w + x * scale + dx] = color;
    }));
    models.push({ name, w, h, px });
    console.log(`${name}: ${w}x${h} (hand-drawn)`);
}

const CANYON_ROCK_COLORS = {
    L: rgb565num(185, 105, 74),
    G: rgb565num(133, 70, 60),
    D: rgb565num(73, 45, 52),
};
const OASIS_THORN_COLORS = {
    L: rgb565num(205, 170, 82),
    G: rgb565num(117, 145, 66),
    D: rgb565num(52, 76, 48),
};
const VOLCANO_VENT_COLORS = {
    L: rgb565num(245, 205, 70),
    G: rgb565num(183, 67, 42),
    D: rgb565num(57, 43, 43),
};
function obstacleRows(left, body, right) {
    const rows = [];
    rows.push('.......L.......');
    rows.push('......LLL......');
    rows.push('.....LGGL......');
    for (let i = 0; i < 7; i++) rows.push('.....GGG.......');
    for (let i = 0; i < 9; i++) rows.push(`...${left}..${body}${body}${body}..${right}...`);
    rows.push('..DDDDDDDDDD...');
    rows.push('..DDDDDDDDDD...');
    rows.push('.DDDDDDDDDDDD..');
    rows.push('.DDDDDDDDDDDD..');
    return rows;
}
addAsciiSprite('canyon_spire_0', obstacleRows('G', 'G', 'D'), 2, CANYON_ROCK_COLORS);
addAsciiSprite('canyon_spire_1', obstacleRows('L', 'G', 'D'), 2, CANYON_ROCK_COLORS);
addAsciiSprite('canyon_spire_2', obstacleRows('D', 'G', 'L'), 2, CANYON_ROCK_COLORS);
addAsciiSprite('oasis_thorn_0', obstacleRows('G', 'G', 'D'), 2, OASIS_THORN_COLORS);
addAsciiSprite('oasis_thorn_1', obstacleRows('L', 'G', 'D'), 2, OASIS_THORN_COLORS);
addAsciiSprite('oasis_thorn_2', obstacleRows('D', 'G', 'L'), 2, OASIS_THORN_COLORS);
addAsciiSprite('volcano_vent_0', obstacleRows('G', 'G', 'L'), 2, VOLCANO_VENT_COLORS);
addAsciiSprite('volcano_vent_1', obstacleRows('D', 'G', 'L'), 2, VOLCANO_VENT_COLORS);
addAsciiSprite('volcano_vent_2', obstacleRows('L', 'G', 'D'), 2, VOLCANO_VENT_COLORS);

const DRY_GRASS_COLORS = {
    L: rgb565num(218, 177, 84),
    G: rgb565num(174, 127, 52),
    D: rgb565num(116, 78, 34),
};
const AGAVE_COLORS = {
    L: rgb565num(139, 164, 108),
    G: rgb565num(82, 119, 79),
    D: rgb565num(48, 76, 54),
};
addAsciiSprite('dry_grass', [
    '.....L.....', '.L...L...L.', '..L..L..L..', 'D.L..G..L.D',
    '.D.G.G.G.D.', '..DGGGGGD..', '...DGGGD...', '....DGD....',
    '....DDD....', '....DDD....', '...DDDDD...', '..DDDDDDD..',
], 2, DRY_GRASS_COLORS);
addAsciiSprite('dry_grass_far', [
    '...L...', 'L..L..L', '.L.G.L.', '.DGGGD.', '..DGD..', '..DDD..', '.DDDDD.',
], 2, DRY_GRASS_COLORS);
addAsciiSprite('agave_far', [
    '....L....', '.L..G..L.', '..L.G.L..', 'L..GGG..L', '.GGGGGGG.',
    '..GGGGG..', '...DDD...', '..DDDDD..',
], 2, AGAVE_COLORS);

/* 天体图标：16x16、4帧离线烘焙。帧间只改变 corona/暗面像素，运行时无需几何绘制。 */
const SUN_COLORS = {
    Y: rgb565num(255, 225, 92), C: rgb565num(255, 245, 170), O: rgb565num(226, 145, 50),
};
const MOON_COLORS = {
    W: rgb565num(232, 238, 220), D: rgb565num(116, 132, 156), B: rgb565num(54, 68, 96),
};
const SUN_FRAMES = [
    ['..OYYO..','.OYYYYO.','OYYYYYYO','YYYYCYYY','YYYYYYYY','YYYYYYYY','OYYYYYYO','.OYYYYO.'],
    ['.OYYYYO.','OYYYYYYO','YYYYCYYY','YYYYYYYY','YYYYYYYY','YYYYCYYY','OYYYYYYO','.OYYYYO.'],
    ['..OYYO..','.OYYYYO.','OYYYYYYO','YYYYYYYY','YYYYCYYY','YYYYYYYY','OYYYYYYO','.OYYYYO.'],
    ['.OYYYYO.','OYYYYYYO','YYYYYYYY','YYYYCYYY','YYYYYYYY','YYYYCYYY','OYYYYYYO','.OYYYYO.'],
];
for (let i = 0; i < SUN_FRAMES.length; i++) addAsciiSprite(`sun_${i}`, SUN_FRAMES[i], 2, SUN_COLORS);
const MOON_FRAMES = [
    ['...WW...','..WWWW..','.WWWWW..','WWWWWW..','WWWWWW..','.WWWWW..','..WWWW..','...WW...'],
    ['..WWW...','.WWWWW..','WWWWWW..','WWWWWW..','WWWWWW..','WWWWWW..','.WWWWW..','..WWW...'],
    ['..WWWW..','.WWWWWW.','WWWWWWW.','WWWWWWW.','WWWWWWW.','WWWWWWW.','.WWWWWW.','..WWWW..'],
    ['..WWW...','.WWWWW..','WWWWWW..','WWWWWW..','WWWWWW..','WWWWWW..','.WWWWW..','..WWW...'],
];
for (let i = 0; i < MOON_FRAMES.length; i++) addAsciiSprite(`moon_${i}`, MOON_FRAMES[i], 2, MOON_COLORS);

/* 非沙漠场景的离线背景小精灵。每组 3 个，绘制在固定远/中景层，不参与碰撞。 */
const CANYON_BG = { L: rgb565num(190, 115, 82), G: rgb565num(132, 76, 66), D: rgb565num(72, 48, 54) };
const OASIS_BG = { L: rgb565num(110, 180, 126), G: rgb565num(58, 122, 87), D: rgb565num(34, 76, 68) };
const VOLCANO_BG = { L: rgb565num(186, 74, 52), G: rgb565num(104, 48, 45), D: rgb565num(46, 34, 40) };
const sceneBgRows = {
    canyon: [
        ['....L....','...LLL...','..LGGL...','.LGGGGL..','LGGGGGGL.','DDDDDDDDD'],
        ['..L.....','..LL....','.LGGL...','LGGGGL..','GGGGGG..','DDDDDD..'],
        ['.....L..','....LLL.','...LGGL.','..LGGGGL','LGGGGGGG','DDDDDDDD'],
    ],
    oasis: [
        ['....L....','...LLL...','..LGGL...','.LGGGGL..','..GGG....','..DDD....'],
        ['..L...L..','.LGG.GGL.','LGGGGGGGL','.GGGGGGG.','..GGGG...','...DDD...'],
        ['...L.L...','..LGGL...','.LGGGGL..','LGGGGGGL.','..GGGG...','..DDDD...'],
    ],
    volcano: [
        ['....L....','...LLL...','..LGGL...','.LGGGGL..','LGGGGGGL.','DDDDDDDDD'],
        ['..L.....','..LL....','.LGGL...','LGGGGL..','GGGGGG..','DDDDDD..'],
        ['.....L..','....LLL.','...LGGL.','..LGGGGL','LGGGGGGG','DDDDDDDD'],
    ],
};
for (const [scene, rows] of Object.entries(sceneBgRows)) {
    const colors = scene === 'canyon' ? CANYON_BG : scene === 'oasis' ? OASIS_BG : VOLCANO_BG;
    rows.forEach((r, i) => addAsciiSprite(`${scene}_bg_${i}`, r, 2, colors));
}
{
    const source = scaleSpriteNearest(
        renderSprite(parseVox(path.join(DINO3D, 'misc/tumbleweed.vox'))), 3, 4);
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
    blit(pick('dry_grass'), 260, 220); blit(pick('scorpion'), 340, 230);
    for (let i = 0; i < 8; i++) blit(pick(`tumbleweed_${i}`), 400 + i * 28, 225 + (i & 1) * 42);
    blit(pick('cactus_far_tall_0'), 480, 200);
    blit(pick('cactus_far_tall_1'), 540, 200);
    blit(pick('cactus_far_tall_2'), 590, 200);
    blit(pick('cactus_far_0'), 480, 260); blit(pick('cactus_far_1'), 510, 260); blit(pick('cactus_far_2'), 540, 260);
    blit(pick('dry_grass_far'), 570, 260); blit(pick('agave_far'), 600, 260);
    blit(pick('rock_far_0'), 10, 300); blit(pick('flower_far_0'), 35, 300);
    blit(pick('rock_near_0'), 70, 300); blit(pick('flower_near_0'), 120, 300);
    fs.writeFileSync('/tmp/sprites-preview.ppm', Buffer.concat([Buffer.from(`P6\n${W} ${H}\n255\n`), img]));
    console.log('preview: /tmp/sprites-preview.ppm');
}
