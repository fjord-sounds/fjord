#!/usr/bin/env node
// Composes the product images of the README from the raw page shots (build/screenshots/raw, made by shoot.sh):
//   docs/screenshots/hero-<light|dark>.webp   two Fjord windows in perspective over the fjord, logo and key facts
//   docs/screenshots/themes.webp             the synth page, light and dark, split diagonally
//
//   tools/screenshots/shoot.sh        # makes the raw page shots first, then calls this script
//   node hero.mjs                     # only the compositions
//
// Requires Playwright (npm install && npx playwright install chromium in this folder) and cwebp.

import { chromium } from "playwright";
import { execFileSync } from "node:child_process";
import { mkdirSync, readdirSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const COMPOSITIONS = [
  { id: "hero",   layout: "hero",  front: "synth", back: "fx" },
  { id: "themes", layout: "split", shot: "synth", themed: false },
];

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const RAW_DIR = join(ROOT, "build/screenshots/raw");
const OUT_DIR = join(ROOT, "docs/screenshots");
const PNG_DIR = join(ROOT, "build/screenshots/png");
const WEBP_QUALITY = "88";
const FONTS = join(ROOT, "resources/fonts");
const THEMES = ["light", "dark"];
const SCALE = 1.5;   // device pixel ratio of the composed images

// Colours taken from Source/ui/Theme.h (backdrop tones derived from them)
const PALETTE = {
  light: { bg1: "#eef5f6", bg2: "#d6e4ea", glow: "rgba(31,154,124,0.20)", ridge1: "#d3e1e7", ridge2: "#c1d3db",
           ridge3: "#aec4ce", text: "#15253a", dim: "#6f8291", accent: "#0e7566", edge: "rgba(0,0,0,0.08)",
           shadow: "rgba(15,40,60,0.28)" },
  dark:  { bg1: "#12203a", bg2: "#070c14", glow: "rgba(88,209,164,0.24)", ridge1: "#0e1828", ridge2: "#132036",
           ridge3: "#192a44", text: "#e2edf0", dim: "#7d8fa3", accent: "#58d1a4", edge: "rgba(255,255,255,0.08)",
           shadow: "rgba(0,0,0,0.60)" },
};

function presetCount() {
  let n = 0;
  const walk = (dir) => {
    for (const f of readdirSync(dir)) {
      const p = join(dir, f);
      if (statSync(p).isDirectory()) walk(p);
      else if (f.endsWith(".fjord")) n++;
    }
  };
  walk(join(ROOT, "presets/Factory"));
  return n;
}

const img = (id, theme) => pathToFileURL(join(RAW_DIR, `${id}-${theme}.png`)).href;

// Layered mountain ridges – the "fjord" at the bottom of every backdrop
function ridges(c, height) {
  const layer = (points, fill, opacity = 1) =>
    `<polygon points="${points}" fill="${fill}" opacity="${opacity}"/>`;
  return `
  <svg class="ridges" viewBox="0 0 1600 ${height}" preserveAspectRatio="none" style="height:${height}px">
    ${layer(`0,${height * 0.35} 180,${height * 0.12} 330,${height * 0.42} 520,${height * 0.05} 760,${height * 0.38} 980,${height * 0.15} 1210,${height * 0.45} 1420,${height * 0.1} 1600,${height * 0.3} 1600,${height} 0,${height}`, c.ridge1)}
    ${layer(`0,${height * 0.6} 240,${height * 0.38} 470,${height * 0.66} 690,${height * 0.4} 930,${height * 0.7} 1150,${height * 0.42} 1390,${height * 0.68} 1600,${height * 0.5} 1600,${height} 0,${height}`, c.ridge2)}
    ${layer(`0,${height * 0.85} 300,${height * 0.68} 620,${height * 0.88} 900,${height * 0.7} 1250,${height * 0.9} 1600,${height * 0.74} 1600,${height} 0,${height}`, c.ridge3)}
  </svg>`;
}

function baseCss(c) {
  return `
  @font-face { font-family: Manrope; font-weight: 400; src: url(${pathToFileURL(join(FONTS, "Manrope-Regular.ttf")).href}); }
  @font-face { font-family: Manrope; font-weight: 500; src: url(${pathToFileURL(join(FONTS, "Manrope-Medium.ttf")).href}); }
  @font-face { font-family: Manrope; font-weight: 600; src: url(${pathToFileURL(join(FONTS, "Manrope-SemiBold.ttf")).href}); }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  html, body { width: 100%; height: 100%; }
  body {
    font-family: Manrope, sans-serif; color: ${c.text}; overflow: hidden; position: relative;
    /* northern lights: green, teal and violet veils over the sky */
    background: radial-gradient(1200px 520px at 80% 4%, ${c.glow}, transparent 70%),
                radial-gradient(900px 380px at 45% 0%, rgba(47,183,200,0.14), transparent 70%),
                radial-gradient(800px 360px at 15% 8%, rgba(122,111,216,0.12), transparent 70%),
                linear-gradient(170deg, ${c.bg1}, ${c.bg2});
  }
  .ridges { position: absolute; left: 0; bottom: 0; width: 100%; }
  .window {
    display: block; border-radius: 14px; position: relative;
    box-shadow: 0 0 0 1px ${c.edge}, 0 50px 90px -30px ${c.shadow}, 0 18px 36px -18px ${c.shadow};
  }`;
}

function heroLayout(comp, theme) {
  const c = PALETTE[theme];
  const other = theme === "light" ? "dark" : "light";
  return {
    viewport: { width: 1600, height: 900 },
    html: `<style>${baseCss(c)}
      .copy { position: absolute; left: 96px; top: 250px; width: 490px; z-index: 2; }
      .logo { font-size: 104px; font-weight: 500; letter-spacing: -3px; line-height: 1; }
      .logo span { color: ${c.accent}; }
      .tag { margin-top: 26px; font-size: 27px; line-height: 1.35; font-weight: 500; }
      .facts { margin-top: 30px; font-size: 15px; letter-spacing: 2.2px; text-transform: uppercase; color: ${c.dim};
               line-height: 2.1; font-weight: 600; }
      .facts b { color: ${c.accent}; font-weight: 600; }
      .scene { position: absolute; left: 590px; top: 110px; perspective: 2600px; }
      .scene .window { position: absolute; width: 1100px; transform-origin: left center; }
      .back  { top: -150px; left: 190px; transform: rotateY(-18deg) rotateX(4deg) scale(0.9); opacity: 0.96; }
      .front { top: 70px; left: 0; transform: rotateY(-18deg) rotateX(4deg); }</style>
      ${ridges(c, 260)}
      <div class="copy">
        <div class="logo">fjord<span>.</span></div>
        <div class="tag">Warm, soft synthesis for every DAW.<br>Nothing sampled.</div>
        <div class="facts">
          <b>4</b> oscillators · <b>24</b> wavetables<br>
          <b>15</b> effects · arp, gate, stutter, pump<br>
          <b>${presetCount().toLocaleString("en-US")}</b> factory presets<br>
          <b>VST3</b> · <b>AU</b> · <b>LV2</b> · standalone
        </div>
      </div>
      <div class="scene">
        <img class="window back" src="${img(comp.back, other)}">
        <img class="window front" src="${img(comp.front, theme)}">
      </div>`,
  };
}

// Light and dark theme of the same page, split diagonally
function splitLayout(comp) {
  const c = PALETTE.light, d = PALETTE.dark;
  return {
    viewport: { width: 1400, height: 1000 },
    html: `<style>${baseCss(c)}
      /* the dark half of the backdrop continues the cut through the window (window: 100,96 – 1300,903) */
      .dark-bg { position: absolute; inset: 0; background: linear-gradient(170deg, ${d.bg1}, ${d.bg2});
                 clip-path: polygon(878px 0, 100% 0, 100% 100%, 521px 100%); }
      .stage { position: absolute; left: 100px; top: 96px; width: 1200px; height: 807px; }
      .stage .window { position: absolute; inset: 0; width: 1200px; }
      .stage .dark { clip-path: polygon(62% 0, 100% 0, 100% 100%, 38% 100%); box-shadow: none; }
      .label { position: absolute; top: 34px; font-size: 14px; letter-spacing: 3px; font-weight: 600; text-transform: uppercase; }</style>
      <div class="dark-bg"></div>
      <div class="label" style="left:100px; color:${c.dim}">Light</div>
      <div class="label" style="right:100px; color:${d.dim}">Dark</div>
      <div class="stage">
        <img class="window" src="${img(comp.shot, "light")}">
        <img class="window dark" src="${img(comp.shot, "dark")}">
      </div>`,
  };
}

const LAYOUTS = { hero: heroLayout, split: splitLayout };
const compositions = COMPOSITIONS;

mkdirSync(OUT_DIR, { recursive: true });
mkdirSync(PNG_DIR, { recursive: true });
const browser = await chromium.launch();
const page = await browser.newPage({ deviceScaleFactor: SCALE });
try {
  for (const comp of compositions) {
    const themes = comp.themed === false ? [null] : THEMES;
    for (const theme of themes) {
      const { viewport, html } = LAYOUTS[comp.layout](comp, theme);
      // A file next to the raw shots, so the page may load them and the fonts via file://
      const htmlFile = join(RAW_DIR, `_${comp.id}${theme ? "-" + theme : ""}.html`);
      writeFileSync(htmlFile, `<!doctype html><meta charset="utf-8">${html}`);
      await page.setViewportSize(viewport);
      await page.goto(pathToFileURL(htmlFile).href);
      await page.evaluate(() => document.fonts.ready);
      await page.waitForFunction(() => [...document.images].every((i) => i.complete && i.naturalWidth > 0));
      const name = `${comp.id}${theme ? "-" + theme : ""}`;
      const png = join(PNG_DIR, `${name}.png`);
      const out = join(OUT_DIR, `${name}.webp`);
      await page.screenshot({ path: png });
      execFileSync("cwebp", ["-quiet", "-q", WEBP_QUALITY, "-m", "6", png, "-o", out]);
      const kb = Math.round(statSync(out).size / 1024);
      console.log(`image ${out.replace(ROOT + "/", "")} (${kb} KB)`);
    }
  }
} finally {
  await browser.close();
}
