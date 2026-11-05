// Captures golden-value scenes from the built wasm artifact (AD-011).
//
//   node scripts/golden-capture.mjs   -> writes tests/golden/scenes.json
//
// Golden values are captured from the artifact itself (self-consistent
// determinism snapshots, not desktop parity — the desktop OpenCL oracle is
// reserved for phase-2 CPU ports of GPU-only algorithms). Recapture when a
// scene definition, the binding surface, or the Emscripten/toolchain pins
// change intentionally; review the JSON diff before committing.

import { createHash } from "node:crypto";
import { mkdirSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const outDir = join(dirname(fileURLToPath(import.meta.url)), "..", "tests", "golden");
const glue = require.resolve("../dist/highmap.js");

const { default: HighmapFactory } = await import(
  new URL(`file:///${glue.replace(/\\/g, "/")}`).href
);
const m = await HighmapFactory();

const view = (raw) => new Float32Array(m.wasmMemory.buffer, raw.dataPtr(), raw.ni() * raw.nj());

const scenes = {
  "noise-perlin-seed0": () =>
    m.noise(m.NoiseType.PERLIN, 64, 64, 4, 2, 0),
  "noise-simplex2s-seed1": () =>
    m.noise(m.NoiseType.SIMPLEX2S, 64, 64, 4, 2, 1),
  "noise-worley-seed2": () =>
    m.noise(m.NoiseType.WORLEY, 64, 64, 4, 2, 2),
  "noise-fbm-perlin-seed42": () =>
    m.noiseFbm(m.NoiseType.PERLIN, 64, 64, 4, 2, 42, 8, 0.7, 0.5, 2),
  "noise-ridged-perlin-seed42": () =>
    m.noiseRidged(m.NoiseType.PERLIN, 64, 64, 4, 2, 42, 8, 0.7, 0.5, 2, 0.1),
  warp: () => {
    const base = m.noise(m.NoiseType.PERLIN, 64, 64, 4, 2, 0);
    const fbm = m.noiseFbm(m.NoiseType.SIMPLEX2S, 64, 64, 4, 2, 0, 8, 0.7, 0.5, 2);
    const dx = m.multiplyScalar(fbm, 0.1);
    const dy = m.multiplyScalar(fbm, 0.1);
    m.warp(base, dx, dy);
    return base;
  },
  "hydraulic-musgrave-seed11": () => {
    const t = m.noiseFbm(m.NoiseType.SIMPLEX2S, 64, 64, 4, 2, 11, 8, 0.7, 0.5, 2);
    m.remapValues(t, 0, 1);
    m.hydraulicMusgrave(t, 50, 1, 0.1, 0.1, 0.01, 0.01);
    return t;
  },
  "hydraulic-stream-seed11": () => {
    const t = m.noiseFbm(m.NoiseType.SIMPLEX2S, 64, 64, 4, 2, 11, 8, 0.7, 0.5, 2);
    m.remapValues(t, 0, 1);
    m.hydraulicStream(t, 0.1, 0.01, 1, 10);
    return t;
  },
};

const out = {};
for (const [name, run] of Object.entries(scenes)) {
  const raw = run();
  const data = view(raw);
  out[name] = {
    shape: [raw.ni(), raw.nj()],
    sha256: createHash("sha256").update(Buffer.from(data.buffer, data.byteOffset, data.byteLength)).digest("hex"),
    first16: Array.from(data.slice(0, 16)),
  };
}

mkdirSync(outDir, { recursive: true });
writeFileSync(join(outDir, "scenes.json"), JSON.stringify(out, null, 2) + "\n");
console.log(`captured ${Object.keys(out).length} scenes -> tests/golden/scenes.json`);
