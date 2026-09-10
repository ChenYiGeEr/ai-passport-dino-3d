// 逐帧放大预览 t-rex 0..7 的腿部细节
'use strict';
const fs = require('fs');

function parseVox(file) {
    const buf = fs.readFileSync(file);
    let off = 20, voxels = null, palette = null;
    while (off < buf.length) {
        const id = buf.toString('ascii', off, off + 4);
        const content = buf.readUInt32LE(off + 4);
        const children = buf.readUInt32LE(off + 8);
        const start = off + 12;
        if (id === 'XYZI') {
            const n = buf.readUInt32LE(start);
            voxels = [];
            for (let i = 0; i < n; i++)
                voxels.push({ x: buf[start+4+i*4], y: buf[start+5+i*4], z: buf[start+6+i*4], c: buf[start+7+i*4] });
        } else if (id === 'RGBA') {
            palette = [];
            for (let i = 0; i < 256; i++)
                palette.push({ r: buf[start+i*4], g: buf[start+i*4+1], b: buf[start+i*4+2] });
        }
        off = start + content + children;
    }
    return { voxels, palette };
}

const k = 3;
function render(model) {
    const occ = new Set(model.voxels.map(v => v.x+','+v.y+','+v.z));
    const pts = model.voxels.map(v => ({
        sx: v.x*k + v.y*(k>>1),
        sy: -v.z*k + v.y*(k-1),
        c: v.c,
        top: !occ.has(v.x+','+v.y+','+(v.z+1)),
        right: !occ.has((v.x+1)+','+v.y+','+v.z),
    }));
    const minX = Math.min(...pts.map(p=>p.sx)), minY = Math.min(...pts.map(p=>p.sy));
    const w = Math.max(...pts.map(p=>p.sx))-minX+k, h = Math.max(...pts.map(p=>p.sy))-minY+k;
    const px = new Uint16Array(w*h);
    for (const p of pts) {
        const base = model.palette[p.c-1];
        const f = p.top ? 1.3 : p.right ? 0.7 : 1.0;
        const r = Math.min(255, base.r*f)|0, g = Math.min(255, base.g*f)|0, b = Math.min(255, base.b*f)|0;
        const v16 = ((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3);
        for (let dy=0;dy<k;dy++) for(let dx=0;dx<k;dx++)
            px[(p.sy-minY+dy)*w + p.sx-minX+dx] = v16 || 0x0841;
    }
    return { w, h, px };
}

const W = 700, H = 150;
const img = Buffer.alloc(W*H*3, 240);
for (let i = 0; i <= 7; i++) {
    const m = render(parseVox(`/Users/lim/code/GitHub/dino3d/objects/t-rex/${i}.vox`));
    const ox = 10 + i*85, oy = H - 12 - m.h;
    for (let y=0;y<m.h;y++) for (let x=0;x<m.w;x++) {
        const v = m.px[y*m.w+x];
        if (!v) continue;
        const o = ((oy+y)*W+ox+x)*3;
        img[o] = ((v>>11)&31)*255/31; img[o+1] = ((v>>5)&63)*255/63; img[o+2] = (v&31)*255/31;
    }
}
for (let x=0;x<W;x++){ const o=((H-10)*W+x)*3; img[o]=200;img[o+1]=60;img[o+2]=60; }
fs.writeFileSync('/tmp/frames.ppm', Buffer.concat([Buffer.from(`P6\n${W} ${H}\n255\n`), img]));
console.log('ok');
