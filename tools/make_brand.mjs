// Renders Themezer's logo from the monorepo's design files into romfs/brand at the sizes the app draws it, so it's
// never scaled. Uses Themezer-3DS's loader for the monorepo's sharp:
//
//     node tools/make_brand.mjs [path/to/themezer-monorepo]

import path from 'node:path';
import fs from 'node:fs';

const root = path.join(path.dirname(new URL(import.meta.url).pathname), '..');
const threeDs = process.env.THEMEZER_3DS ?? path.join(root, '..', 'themezer-3ds');
const { defaultMonorepo, loadSharp } = await import(path.join(threeDs, 'tools', 'sharp.mjs'));
const monorepo = process.argv[2] ?? defaultMonorepo;
const sharp = loadSharp(monorepo);
const out = path.join(root, 'themezer-nx', 'romfs', 'brand');
fs.mkdirSync(out, { recursive: true });

// [design file, output name, height in pixels]
const renders = [
    ['logo.svg', 'logo', 40],    // the top bar
    ['icon.svg', 'icon_128', 128], // empty pages
];

for (const [file, name, height] of renders) {
    await sharp(path.join(monorepo, 'design', file), { density: 1200, limitInputPixels: false })
        .resize({ height })
        .png()
        .toFile(path.join(out, `${name}.png`));
    console.log(name, height);
}
