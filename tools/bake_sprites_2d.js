#!/usr/bin/env node
/**
 * bake_sprites_2d.js — 手绘 2D 像素精灵 → C 数组(与 sprites.h 同一格式)。
 *
 * 每个字符 = 2x2 像素(SCALE=2),字符到调色板索引见 CHAR_MAP。
 * 输出 main/sprites.h + main/sprites.c 和 /tmp/sprites-preview.ppm。
 */
'use strict';
const fs = require('fs');
const path = require('path');

const OUT_H = path.join(__dirname, '..', 'main', 'sprites.h');
const OUT_C = path.join(__dirname, '..', 'main', 'sprites.c');
const SCALE = 2;

/* ---------------- 调色板 ---------------- */
// idx: [r,g,b]  (0 固定为透明)
const PALETTE = [
    null,
    [134, 194, 50],   // 1 G 恐龙主绿
    [94, 140, 35],    // 2 g 恐龙暗绿(描边/阴影)
    [168, 216, 90],   // 3 l 恐龙亮绿
    [255, 255, 255],  // 4 W 眼白
    [32, 32, 32],     // 5 B 黑(瞳孔/嘴/死眼)
    [78, 154, 47],    // 6 C 仙人掌绿
    [58, 115, 35],    // 7 c 仙人掌暗绿
    [233, 30, 156],   // 8 P 花粉红
    [139, 90, 43],    // 9 T 翼龙棕
    [107, 68, 35],    // 10 t 翼龙暗棕
    [156, 139, 112],  // 11 R 石头灰
    [122, 108, 85],   // 12 r 石头暗灰
    [255, 220, 100],  // 13 Y 花蕊黄
    [218, 177, 84],   // 14 L 枯草亮面
    [174, 127, 52],   // 15 A 枯草主体
    [116, 78, 34],    // 16 D 枯草暗面
    [139, 164, 108],  // 17 H 龙舌兰亮面
    [82, 119, 79],    // 18 V 龙舌兰主体
    [48, 76, 54],     // 19 d 龙舌兰暗面
];
const CHAR_MAP = {
    G: 1, g: 2, l: 3, W: 4, B: 5, C: 6, c: 7, P: 8, T: 9, t: 10,
    R: 11, r: 12, Y: 13, L: 14, A: 15, D: 16, H: 17, V: 18, d: 19,
};

/* ---------------- 精灵 ASCII 稿 ---------------- */
// 恐龙(面向右, 经典 Chrome 造型, 1x 约 20x22, 2x 后 40x44)
const DINO_HEAD = [
    '..........GGGGGGG',
    '..........GGGGGGGGG',
    '..........GGWGGGGGG',
    '..........GGGGGGGGG',
    '..........GGGGGGGGG',
    '..........GGGGBBBBB',
    '..........GGGGGG',
    '..........GGGGGGGG',
];
const DINO_BODY_TAIL = [
    'G.........GGGGGG',
    'GG........GGGGGGG',
    'GGG......GGGGGGGG',
    'GGGG....GGGGGGGGGG',
    'GGGGG..GGGGGGGGGG',
    'GGGGGGGGGGGGGGGG',
    'GGGGGGGGGGGGGGG',
    '.GGGGGGGGGGGGGG',
    '..GGGGGGGGGGG',
    '...GGGGGGGGG',
    '....GGGGGGGG',
];
const DINO_LEGS_0 = [
    '....GG..GG',
    '....GG...GG',
    '....GG....GG',
    '....GGG...GGG',
];
const DINO_LEGS_1 = [
    '....GG..GG',
    '....GG..GG',
    '.....GG..GG',
    '.....GGG..GGG',
];
const DINO_LEGS_JUMP = [
    '....GG..GG',
    '....GG..GG',
    '....GGGGGG',
];
// 下蹲(长条低身, 1x 约 24x12)
const DINO_DOWN_BODY = [
    '..................GGGGGGGGG',
    '...................GGWGGGGG',
    '...................GGGGGGBB',
    'GGGGG............GGGGGGGG',
    'GGGGGGGGGGGGGGGGGGGGGGGG',
    '.GGGGGGGGGGGGGGGGGGGGG',
    '..GGGGGGGGGGGGGGGGGG',
];
const DINO_DOWN_LEGS_0 = [
    '....GG......GG',
    '....GGG.....GGG',
];
const DINO_DOWN_LEGS_1 = [
    '.....GG......GG',
    '.....GGG.....GGG',
];
// 仙人掌(2x 后: 小 14x30, 高 14x44, 细 10x36)
const CACTUS_SMALL = [
    '...C...',
    '...CC..',
    '...C...',
    'CC.C..C',
    'CC.C..C',
    'CC.CC.C',
    'CC.CC.C',
    'CC.CC.C',
    'CC.CC.C',
    '.C.CC..',
    '..CCC..',
    '..CCC..',
    '..CCC..',
    '..CCC..',
    '..CCC..',
];
const CACTUS_TALL = [
    '...C...',
    '...CC..',
    '...C...',
    '...C...',
    'CC.C..C',
    'CC.C..C',
    'CC.C..C',
    'CC.CC.C',
    'CC.CC.C',
    'CC.CC.C',
    'CC.CC.C',
    'CC.CC.C',
    '.C.CC..',
    '..CCC..',
    '..CCC..',
    '..CCC..',
    '..CCC..',
    '..CCC..',
    '..CCC..',
    '..CCC..',
    '..CCC..',
    '..CCC..',
];
const CACTUS_THIN = [
    '..C..',
    '..C..',
    'C.C.C',
    'C.C.C',
    'C.CCC',
    'C.CC.',
    '.CC..',
    '.CC..',
    '.CC..',
    '.CC..',
    '.CC..',
    '.CC..',
];
// 翼龙(2x 后 28x14)
const PTERO_0 = [
    '......TT......',
    '.....TTTT.....',
    '....TTTTTTT...',
    '..TTTTTTTTTTT.',
    'TTTTTTTTTTTTTT',
    '..ttttttttt...',
    '....tttt......',
];
const PTERO_1 = [
    'TTTTTTTTTTTTTT',
    '..TTTTTTTTTT..',
    '....TTTTTT....',
    '.....TTTT.....',
    '.....tttt.....',
    '......tt......',
];
// 地面装饰
const ROCK_0 = ['.RR.', 'RRRR', 'RRrR'];
const ROCK_1 = ['.RRR..', 'RRRRRR', 'RRRrrR', '.rrrr.'];
const ROCK_2 = ['..RR...', '.RRRR..', 'RRRRRRR', 'RRRRrRR', '.rrrrr.'];
const FLOWER_0 = ['..P..', '.PYP.', '..P..', '..g..', '..g..'];
const FLOWER_1 = ['.P.', 'PYP', '.P.', '.g.', '.g.'];
const DRY_GRASS = [
    '.....L.....', '.L...L...L.', '..L..L..L..', 'D.L..A..L.D',
    '.D.A.A.A.D.', '..DAAAAAD..', '...DAAAD...', '....DAD....',
    '....DDD....', '....DDD....', '...DDDDD...', '..DDDDDDD..',
];
const DRY_GRASS_FAR = [
    '...L...', 'L..L..L', '.L.A.L.', '.DAAAD.', '..DAD..', '..DDD..', '.DDDDD.',
];
const AGAVE_FAR = [
    '....H....', '.H..V..H.', '..H.V.H..', 'H..VVV..H', '.VVVVVVV.',
    '..VVVVV..', '...ddd...', '..ddddd..',
];

/* ---------------- 组装帧 ---------------- */
function joinRows(...parts) {
    const w = Math.max(...parts.flat().map(r => r.length));
    return parts.flat().map(r => r.padEnd(w, '.'));
}

const dino0 = joinRows(DINO_HEAD, DINO_BODY_TAIL, DINO_LEGS_0);
const dino1 = joinRows(DINO_HEAD, DINO_BODY_TAIL, DINO_LEGS_1);
const dinoJump = joinRows(DINO_HEAD, DINO_BODY_TAIL, DINO_LEGS_JUMP);
const dinoDead = dino0.map(r => r.replace('W', 'B'));
const dinoDown0 = joinRows(DINO_DOWN_BODY, DINO_DOWN_LEGS_0);
const dinoDown1 = joinRows(DINO_DOWN_BODY, DINO_DOWN_LEGS_1);
const dinoDeadDown = dinoDown0.map(r => r.replace('W', 'B'));

// 开花仙人掌: 顶部两像素换成花粉红
function flowered(rows) {
    const out = rows.slice();
    out[0] = out[0].replace('C', 'P');
    if (out[1]) out[1] = out[1].replace('C', 'P');
    return out;
}

const SPRITES = {
    dino_0: dino0,
    dino_1: dino1,
    dino_jump: dinoJump,
    dino_dead: dinoDead,
    dino_down_0: dinoDown0,
    dino_down_1: dinoDown1,
    dino_dead_down: dinoDeadDown,
    cactus: CACTUS_SMALL,
    cactus_tall: CACTUS_TALL,
    cactus_thin: CACTUS_THIN,
    fcactus: flowered(CACTUS_SMALL),
    fcactus_tall: flowered(CACTUS_TALL),
    fcactus_thin: flowered(CACTUS_THIN),
    ptero_0: PTERO_0,
    ptero_1: PTERO_1,
    rock_0: ROCK_0,
    rock_1: ROCK_1,
    rock_2: ROCK_2,
    flower_0: FLOWER_0,
    flower_1: FLOWER_1,
    dry_grass: DRY_GRASS,
    dry_grass_far: DRY_GRASS_FAR,
    agave_far: AGAVE_FAR,
};

/* ---------------- 编码输出 ---------------- */
function encode(rows) {
    const w = Math.max(...rows.map(r => r.length)) * SCALE;
    const h = rows.length * SCALE;
    const px = new Uint8Array(w * h);
    rows.forEach((row, ry) => {
        [...row.padEnd(w / SCALE, '.')].forEach((ch, rx) => {
            const idx = CHAR_MAP[ch] || 0;
            if (!idx) return;
            for (let dy = 0; dy < SCALE; dy++)
                for (let dx = 0; dx < SCALE; dx++)
                    px[(ry * SCALE + dy) * w + rx * SCALE + dx] = idx;
        });
    });
    return { w, h, px };
}

function rgb565(r, g, b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
function night(c) { return { r: Math.round(c[0] * 0.22), g: Math.round(c[1] * 0.25), b: Math.round(Math.min(255, c[2] * 0.42 + 8)) }; }

let h = `// AUTO-GENERATED by tools/bake_sprites_2d.js — 勿手改, 改 ASCII 稿后重新烘焙。
#pragma once
#include <stdint.h>

typedef struct {
    uint16_t w;
    uint16_t h;
    const uint8_t *px; // 8bpp 调色板索引, 0 = 透明
} sprite_t;

extern const uint16_t sprite_palette_day[256];
extern const uint16_t sprite_palette_night[256];

`;
let c = `// AUTO-GENERATED by tools/bake_sprites_2d.js
#include "sprites.h"

const uint16_t sprite_palette_day[256] = {
    0,
`;
for (let i = 1; i < PALETTE.length; i++) c += `    0x${rgb565(...PALETTE[i]).toString(16).padStart(4, '0')},\n`;
c += `};

const uint16_t sprite_palette_night[256] = {
    0,
`;
for (let i = 1; i < PALETTE.length; i++) { const n = night(PALETTE[i]); c += `    0x${rgb565(n.r, n.g, n.b).toString(16).padStart(4, '0')},\n`; }
c += '};\n\n';

const models = {};
for (const [name, rows] of Object.entries(SPRITES)) {
    const m = encode(rows);
    models[name] = m;
    console.log(`${name}: ${m.w}x${m.h}`);
    h += `extern const sprite_t spr_${name};\n`;
    c += `static const uint8_t px_${name}[${m.w * m.h}] = {\n`;
    for (let i = 0; i < m.px.length; i += 20)
        c += '    ' + Array.from(m.px.slice(i, i + 20)).join(',') + ',\n';
    c += `};\nconst sprite_t spr_${name} = { ${m.w}, ${m.h}, px_${name} };\n\n`;
}
fs.writeFileSync(OUT_H, h);
fs.writeFileSync(OUT_C, c);
console.log('wrote', OUT_H, 'and', OUT_C);

/* ---------------- 预览 ---------------- */
{
    const W = 640, H = 300;
    const img = Buffer.alloc(W * H * 3, 236);
    function blit(name, ox, oy) {
        const m = models[name];
        for (let y = 0; y < m.h; y++) for (let x = 0; x < m.w; x++) {
            const i = m.px[y * m.w + x];
            if (!i) continue;
            const c = PALETTE[i];
            const o = ((oy + y) * W + ox + x) * 3;
            img[o] = c[0]; img[o + 1] = c[1]; img[o + 2] = c[2];
        }
    }
    blit('dino_0', 10, 10); blit('dino_1', 60, 10); blit('dino_jump', 110, 10); blit('dino_dead', 160, 10);
    blit('dino_down_0', 10, 70); blit('dino_down_1', 80, 70); blit('dino_dead_down', 150, 70);
    blit('cactus', 240, 10); blit('cactus_tall', 270, 10); blit('cactus_thin', 310, 10);
    blit('fcactus', 350, 10); blit('fcactus_tall', 390, 10); blit('fcactus_thin', 430, 10);
    blit('ptero_0', 480, 10); blit('ptero_1', 540, 10);
    blit('rock_0', 240, 80); blit('rock_1', 280, 80); blit('rock_2', 330, 80);
    blit('flower_0', 390, 80); blit('flower_1', 420, 80); blit('dry_grass', 460, 80);
    fs.writeFileSync('/tmp/sprites-preview.ppm', Buffer.concat([Buffer.from(`P6\n${W} ${H}\n255\n`), img]));
    console.log('preview: /tmp/sprites-preview.ppm');

    // 整机画面预览: 320x240 按游戏布局合成一帧
    const SW = 320, SH = 240;
    const scene = Buffer.alloc(SW * SH * 3);
    const sky = [238, 203, 110], far = [214, 178, 90], river = [127, 196, 214],
          wave = [160, 220, 230], ground = [232, 192, 100], horizon = [200, 165, 85];
    function rect(x, y, w, hh, c) {
        for (let yy = y; yy < y + hh; yy++) for (let xx = x; xx < x + w; xx++) {
            if (xx < 0 || yy < 0 || xx >= SW || yy >= SH) continue;
            const o = (yy * SW + xx) * 3;
            scene[o] = c[0]; scene[o + 1] = c[1]; scene[o + 2] = c[2];
        }
    }
    function blit2(name, ox, oy) {
        const m = models[name];
        for (let y = 0; y < m.h; y++) for (let x = 0; x < m.w; x++) {
            const i = m.px[y * m.w + x];
            if (!i) continue;
            rect(ox + x, oy + y, 1, 1, PALETTE[i]);
        }
    }
    rect(0, 0, SW, 140, sky);
    rect(0, 140, SW, 12, far);
    rect(0, 152, SW, 22, river);
    for (let i = 0; i < 8; i++) rect(i * 48 - 10, 152 + 4 + (i * 7) % 12, 12, 2, wave);
    rect(0, 174, SW, 66, ground);
    rect(0, 174, SW, 2, horizon);
    // 云
    rect(60, 40, 26, 6, [255, 250, 235]); rect(64, 36, 16, 6, [255, 250, 235]);
    rect(220, 60, 22, 6, [255, 250, 235]); rect(224, 56, 12, 6, [255, 250, 235]);
    blit2('dino_1', 30, 205 - 46);
    blit2('fcactus', 150, 205 - 30);
    blit2('cactus_tall', 190, 205 - 44);
    blit2('cactus', 208, 205 - 30);
    blit2('ptero_0', 260, 160);
    blit2('rock_1', 100, 218); blit2('flower_0', 250, 220); blit2('dry_grass', 290, 216);
    fs.writeFileSync('/tmp/scene-preview.ppm', Buffer.concat([Buffer.from(`P6\n${SW} ${SH}\n255\n`), scene]));
    console.log('scene preview: /tmp/scene-preview.ppm');
}
