// Times every function of the wasm binding surface on the shipped artifact.
//
//   node scripts/bench.mjs [--only <substr[,substr2...]>] [--sizes 256,512]
//                          [--out <path>]
//
// Coverage is the binding manifest (AD-008), parsed with the same rules as
// tests/surface.test.ts, so new surface growth automatically grows the suite.
// Per-function fixed-seed input recipes live in one table below; functions
// that need extra grids (`warp`, the advection pair, `rifts`, the thermal
// talus) get recipe-defined companions. Requires `npm run build` first.
//
// Methodology (design: openspec/changes/add-performance-benchmarks): build the
// inputs once per (function, grid) cell, run one untimed warmup, then repeated
// timed runs within a per-cell budget (>= 3, <= 10 reps, ~2 s) and report the
// median/min/max. In-place mutators get a pristine copy of their input before
// every timed rep so state never compounds. No timing gates anything.
//
// Results go to bench/results/bench-<ISO-date>-<hostname>.json (gitignored)
// with a Google-Benchmark-compatible top level: {context, benchmarks}.

import { execFileSync } from "node:child_process";
import {
  existsSync,
  mkdirSync,
  readFileSync,
  readdirSync,
  statSync,
  writeFileSync,
} from "node:fs";
import { arch, hostname, platform } from "node:os";
import { dirname, join, relative, resolve } from "node:path";
import { performance } from "node:perf_hooks";
import { fileURLToPath, pathToFileURL } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const distDir = join(root, "dist");
const distEntry = join(distDir, "index.js");
const manifestPath = join(root, "HighMap", "wasm", "manifest.md");

const DEFAULT_SIZES = [256, 512, 1024];
const MIN_REPS = 3;
const MAX_REPS = 10;
const CELL_BUDGET_MS = 2000;

function fail(message) {
  console.error(`bench: ${message}`);
  process.exit(1);
}

/**
 * JS names of the high-level API, parsed from the manifest's Functions table.
 * Same rules as `manifestApiFunctionNames()` in tests/surface.test.ts: alias
 * rows (`clampValues` (JS: `clamp`)) map to the alias, and rows the API
 * reaches through options or overloads (scalar algebra variants, the moisture
 * overload, the raw `makeArray`) are excluded.
 */
function manifestApiFunctionNames() {
  const skip = new Set([
    "makeArray", // exposed as makeGrid
    "addScalar",
    "subtractScalar",
    "multiplyScalar",
    "divideScalar",
    "hydraulicMusgraveMoisture", // reachable via hydraulicMusgrave options
  ]);
  const names = new Set();

  for (const line of readFileSync(manifestPath, "utf8").split(/\r?\n/)) {
    const m = line.match(/^\|([^|]*)\|/);
    if (!m) continue;
    const cell = m[1] ?? "";

    // alias form first: `bindingName` (JS: `apiName`)
    const alias = cell.match(/`[A-Za-z]+`\s*\(JS:\s*`([A-Za-z]+)`\)/);
    if (alias) {
      names.add(alias[1]);
      continue;
    }

    for (const t of cell.matchAll(/`([A-Za-z]+)`/g)) {
      const name = t[1];
      if (!skip.has(name)) names.add(name);
    }
  }
  return [...names].sort();
}

function parseArgs(argv) {
  const opts = { only: null, sizes: DEFAULT_SIZES.slice(), out: null };

  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === "--only" || arg === "--sizes" || arg === "--out") {
      const value = argv[i + 1];
      if (value === undefined || value.startsWith("--")) {
        fail(`${arg} needs a value`);
      }
      i++;

      if (arg === "--only") {
        const filters = value
          .split(",")
          .map((s) => s.trim().toLowerCase())
          .filter(Boolean);
        if (filters.length === 0) fail("--only needs at least one name substring");
        opts.only = filters;
        continue;
      }

      if (arg === "--sizes") {
        const sizes = value.split(",").map((s) => Number(s.trim()));
        for (const size of sizes) {
          if (!Number.isInteger(size) || size <= 0) {
            fail(`--sizes expects positive integers, got "${value}"`);
          }
        }
        opts.sizes = [...new Set(sizes)];
        continue;
      }

      opts.out = value;
      continue;
    }

    fail(`unknown argument "${arg}" (usage: [--only <names>] [--sizes <list>] [--out <path>])`);
  }

  return opts;
}

/** Noise terrain as plain data (the per-rep pristine copy). */
function terrainData(hmap, n, seed, kw = [4, 4], options = {}) {
  const grid = hmap.noiseFbm(hmap.NoiseType.PERLIN, [n, n], kw, seed);
  try {
    if (options.remap) hmap.remap(grid);
    return grid.copyData();
  } finally {
    grid.free();
  }
}

/** Noise terrain as a reusable wasm-side grid (read-only companion inputs). */
function terrainGrid(hmap, n, seed, kw = [4, 4]) {
  return hmap.noiseFbm(hmap.NoiseType.PERLIN, [n, n], kw, seed);
}

/** Constant talus limit, matching the showcase recipes' `0.1 / 256` style. */
const TALUS = 1 / 256;

/**
 * Fixed-seed recipes, one per manifest API name (checked at startup).
 *
 * - `mutates`: the primary grid is modified in place, so the harness passes a
 *   fresh plain `{data, shape}` copy per rep and frees the returned wrapper.
 * - `setup(hmap, n)`: builds every input grid once per (function, grid) cell.
 * - `call(hmap, ctx, primary)`: one rep; the harness frees its return value.
 *
 * Parameters mirror site/src/demos/*.ts where a demo exists (showcase scale
 * and web-sized iteration budgets) and use the API defaults elsewhere.
 */
const recipes = {
  add: {
    mutates: false,
    setup: (hmap, n) => {
      const a = terrainGrid(hmap, n, 1);
      const b = terrainGrid(hmap, n, 2, [2, 2]);
      return {
        a,
        b,
        dispose: () => {
          a.free();
          b.free();
        },
      };
    },
    call: (hmap, ctx) => hmap.add(ctx.a, ctx.b),
  },

  advectionParticle: {
    mutates: false,
    setup: (hmap, n) => {
      const z = terrainGrid(hmap, n, 1, [2, 2]);
      hmap.remap(z);
      const n0 = terrainGrid(hmap, n, 1, [32, 32]);
      return {
        z,
        n0,
        dispose: () => {
          z.free();
          n0.free();
        },
      };
    },
    call: (hmap, ctx) => hmap.advectionParticle(ctx.z, ctx.n0, 10000, 1, { iterations: 10 }),
  },

  advectionWarp: {
    mutates: false,
    setup: (hmap, n) => {
      const z = terrainGrid(hmap, n, 1, [2, 2]);
      hmap.remap(z);
      return { z, dispose: () => z.free() };
    },
    // the demo warps the terrain along its own gradient
    call: (hmap, ctx) =>
      hmap.advectionWarp(ctx.z, ctx.z, {
        advectionLength: 0.05,
        valuePersistence: 0.96,
      }),
  },

  clamp: {
    mutates: true,
    setup: (hmap, n) => ({ shape: [n, n], pristine: terrainData(hmap, n, 1) }),
    call: (hmap, ctx, z) => hmap.clamp(z),
  },

  divide: {
    mutates: false,
    setup: (hmap, n) => {
      const a = terrainGrid(hmap, n, 1);
      const b = terrainGrid(hmap, n, 2, [2, 2]);
      return {
        a,
        b,
        dispose: () => {
          a.free();
          b.free();
        },
      };
    },
    call: (hmap, ctx) => hmap.divide(ctx.a, ctx.b),
  },

  falloff: {
    mutates: true,
    setup: (hmap, n) => ({ shape: [n, n], pristine: terrainData(hmap, n, 1) }),
    call: (hmap, ctx, z) =>
      hmap.falloff(z, { strength: 1, distFct: hmap.DistanceFunction.EUCLIDIAN }),
  },

  hillshade: {
    mutates: false,
    setup: (hmap, n) => {
      const grid = terrainGrid(hmap, n, 1);
      return { grid, dispose: () => grid.free() };
    },
    call: (hmap, ctx, grid) => hmap.hillshade(grid, Math.PI / 4, Math.PI / 4),
  },

  hydraulicMcdonald: {
    mutates: true,
    setup: (hmap, n) => ({
      shape: [n, n],
      pristine: terrainData(hmap, n, 1, [2, 2], { remap: true }),
    }),
    call: (hmap, ctx, z) =>
      hmap.hydraulicMcdonald(z, 64, 1, { samples: 1024, maxage: 64 }),
  },

  hydraulicMcdonaldMultiscale: {
    mutates: true,
    setup: (hmap, n) => ({
      shape: [n, n],
      pristine: terrainData(hmap, n, 1, [2, 2], { remap: true }),
    }),
    call: (hmap, ctx, z) =>
      hmap.hydraulicMcdonaldMultiscale(z, 1, {
        stepsPerLevel: [256, 128, 64],
        samples: 512,
        maxage: 32,
      }),
  },

  hydraulicMusgrave: {
    mutates: true,
    // no showcase demo (numerically unstable in the GUI); API defaults
    setup: (hmap, n) => ({
      shape: [n, n],
      pristine: terrainData(hmap, n, 1, [2, 2], { remap: true }),
    }),
    call: (hmap, ctx, z) => hmap.hydraulicMusgrave(z),
  },

  hydraulicSchott: {
    mutates: true,
    setup: (hmap, n) => ({
      shape: [n, n],
      pristine: terrainData(hmap, n, 2, [2, 2], { remap: true }),
    }),
    call: (hmap, ctx, z) => hmap.hydraulicSchott(z, 2 / 256, { iterations: 60 }),
  },

  hydraulicSchottErosion: {
    mutates: true,
    setup: (hmap, n) => ({
      shape: [n, n],
      pristine: terrainData(hmap, n, 2, [2, 2], { remap: true }),
    }),
    call: (hmap, ctx, z) => hmap.hydraulicSchottErosion(z, { iterations: 20 }),
  },

  hydraulicStream: {
    mutates: true,
    setup: (hmap, n) => ({
      shape: [n, n],
      pristine: terrainData(hmap, n, 1, [2, 2], { remap: true }),
    }),
    call: (hmap, ctx, z) =>
      hmap.hydraulicStream(z, 0.1, 5 * TALUS, { ir: 1, clippingRatio: 10 }),
  },

  hydraulicVpipes: {
    mutates: true,
    setup: (hmap, n) => ({
      shape: [n, n],
      pristine: terrainData(hmap, n, 2, [4, 4], {
        remap: true,
        options: { octaves: 8, weight: 0.7 },
      }),
    }),
    call: (hmap, ctx, z) => hmap.hydraulicVpipes(z, { iterations: 30 }),
  },

  multiply: {
    mutates: false,
    setup: (hmap, n) => {
      const a = terrainGrid(hmap, n, 1);
      const b = terrainGrid(hmap, n, 2, [2, 2]);
      return {
        a,
        b,
        dispose: () => {
          a.free();
          b.free();
        },
      };
    },
    call: (hmap, ctx) => hmap.multiply(ctx.a, ctx.b),
  },

  noise: {
    mutates: false,
    setup: (hmap, n) => ({ shape: [n, n] }),
    call: (hmap, ctx) =>
      hmap.noise(hmap.NoiseType.PERLIN, ctx.shape, [4, 4], 1),
  },

  noiseFbm: {
    mutates: false,
    setup: (hmap, n) => ({ shape: [n, n] }),
    call: (hmap, ctx) =>
      hmap.noiseFbm(hmap.NoiseType.PERLIN, ctx.shape, [4, 4], 1),
  },

  noisePingpong: {
    mutates: false,
    setup: (hmap, n) => ({ shape: [n, n] }),
    call: (hmap, ctx) =>
      hmap.noisePingpong(hmap.NoiseType.PERLIN_BILLOW, ctx.shape, [4, 4], 1),
  },

  noiseRidged: {
    mutates: false,
    setup: (hmap, n) => ({ shape: [n, n] }),
    call: (hmap, ctx) =>
      hmap.noiseRidged(hmap.NoiseType.SIMPLEX2S, ctx.shape, [4, 4], 1),
  },

  plateau: {
    mutates: true,
    setup: (hmap, n) => ({ shape: [n, n], pristine: terrainData(hmap, n, 1) }),
    call: (hmap, ctx, z) => hmap.plateau(z, 64, 4),
  },

  remap: {
    mutates: true,
    setup: (hmap, n) => ({ shape: [n, n], pristine: terrainData(hmap, n, 1) }),
    call: (hmap, ctx, z) => hmap.remap(z),
  },

  rifts: {
    mutates: true,
    setup: (hmap, n) => ({
      shape: [n, n],
      pristine: terrainData(hmap, n, 0, [4, 4], { remap: true }),
    }),
    call: (hmap, ctx, z) => hmap.rifts(z, [8, 1.5], 30, 0.1, 1),
  },

  scale: {
    mutates: true,
    setup: (hmap, n) => ({ shape: [n, n], pristine: terrainData(hmap, n, 1) }),
    call: (hmap, ctx, z) => hmap.scale(z, 1),
  },

  smoothFill: {
    mutates: true,
    setup: (hmap, n) => ({
      shape: [n, n],
      pristine: terrainData(hmap, n, 1, [2, 2]),
    }),
    call: (hmap, ctx, z) => hmap.smoothFill(z, 32, { k: 0.1 }),
  },

  subtract: {
    mutates: false,
    setup: (hmap, n) => {
      const a = terrainGrid(hmap, n, 1);
      const b = terrainGrid(hmap, n, 2, [2, 2]);
      return {
        a,
        b,
        dispose: () => {
          a.free();
          b.free();
        },
      };
    },
    call: (hmap, ctx) => hmap.subtract(ctx.a, ctx.b),
  },

  thermal: {
    mutates: true,
    setup: (hmap, n) => ({ shape: [n, n], pristine: terrainData(hmap, n, 1) }),
    call: (hmap, ctx, z) => hmap.thermal(z, 0.1 * TALUS, { iterations: 500 }),
  },

  thermalAutoBedrock: {
    mutates: true,
    setup: (hmap, n) => ({
      shape: [n, n],
      pristine: terrainData(hmap, n, 1, [2, 2]),
    }),
    call: (hmap, ctx, z) =>
      hmap.thermalAutoBedrock(z, 0.5 * TALUS, { iterations: 500 }),
  },

  thermalConserve: {
    mutates: true,
    setup: (hmap, n) => ({ shape: [n, n], pristine: terrainData(hmap, n, 1) }),
    call: (hmap, ctx, z) =>
      hmap.thermalConserve(z, 0.1 * TALUS, { iterations: 100 }),
  },

  thermalFlatten: {
    mutates: true,
    setup: (hmap, n) => ({
      shape: [n, n],
      pristine: terrainData(hmap, n, 1, [2, 2]),
    }),
    call: (hmap, ctx, z) =>
      hmap.thermalFlatten(z, 1.5 * TALUS, {
        iterations: 100,
        sigmaInf: 0.5,
        sigmaSup: 0.05,
      }),
  },

  thermalInflate: {
    mutates: true,
    setup: (hmap, n) => ({
      shape: [n, n],
      pristine: terrainData(hmap, n, 1, [4, 4], { remap: true }),
    }),
    call: (hmap, ctx, z) =>
      hmap.thermalInflate(z, 2 * TALUS, { iterations: 500 }),
  },

  thermalOlsen: {
    mutates: true,
    setup: (hmap, n) => ({
      shape: [n, n],
      pristine: terrainData(hmap, n, 0, [4, 4], { remap: true }),
    }),
    call: (hmap, ctx, z) => hmap.thermalOlsen(z, 4 * TALUS, { iterations: 100 }),
  },

  thermalRib: {
    mutates: true,
    setup: (hmap, n) => ({
      shape: [n, n],
      pristine: terrainData(hmap, n, 1, [2, 2]),
    }),
    call: (hmap, ctx, z) => hmap.thermalRib(z, { iterations: 5 }),
  },

  thermalRidge: {
    mutates: true,
    setup: (hmap, n) => ({
      shape: [n, n],
      pristine: terrainData(hmap, n, 1, [4, 4], { remap: true }),
    }),
    call: (hmap, ctx, z) =>
      hmap.thermalRidge(z, 2 * TALUS, { iterations: 500 }),
  },

  thermalScree: {
    mutates: true,
    setup: (hmap, n) => ({
      shape: [n, n],
      pristine: terrainData(hmap, n, 1, [4, 4], { remap: true }),
    }),
    call: (hmap, ctx, z) =>
      hmap.thermalScree(z, 2 * TALUS, 0.5, { iterations: 500 }),
  },

  thermalSchott: {
    mutates: true,
    setup: (hmap, n) => {
      // elevation-aligned talus map, as in the thermal-schott demo
      const talus = hmap.scale(terrainGrid(hmap, n, 1, [2, 2]), 1);
      hmap.remap(talus, { vmin: TALUS, vmax: 2 * TALUS });
      return {
        shape: [n, n],
        pristine: terrainData(hmap, n, 1, [2, 2], { remap: true }),
        talus,
        dispose: () => talus.free(),
      };
    },
    call: (hmap, ctx, z) => hmap.thermalSchott(z, ctx.talus, { iterations: 500 }),
  },

  warp: {
    mutates: true,
    setup: (hmap, n) => {
      const dx = terrainGrid(hmap, n, 2);
      const dy = terrainGrid(hmap, n, 3);
      hmap.remap(dx, { vmin: -0.25, vmax: 0.25 });
      hmap.remap(dy, { vmin: -0.25, vmax: 0.25 });
      return {
        shape: [n, n],
        pristine: terrainData(hmap, n, 1, [2, 2]),
        dx,
        dy,
        dispose: () => {
          dx.free();
          dy.free();
        },
      };
    },
    call: (hmap, ctx, z) => hmap.warp(z, ctx.dx, ctx.dy),
  },
};

/** The primary grid for one rep; mutators get a pristine copy outside timing. */
function preparePrimary(recipe, ctx) {
  if (recipe.mutates) return { data: ctx.pristine.slice(), shape: ctx.shape };
  return ctx.grid;
}

/** One timed rep; grids returned by new-grid functions are freed outside it. */
function timedCall(hmap, recipe, ctx) {
  const primary = preparePrimary(recipe, ctx);
  const t0 = performance.now();
  const out = recipe.call(hmap, ctx, primary);
  const t1 = performance.now();
  if (out && typeof out.free === "function") out.free();
  return t1 - t0;
}

function median(sorted) {
  const mid = sorted.length >> 1;
  return sorted.length % 2 === 1
    ? sorted[mid]
    : (sorted[mid - 1] + sorted[mid]) / 2;
}

/** Build the inputs once, warm up untimed, then time within the cell budget. */
function timeCell(hmap, recipe, size) {
  const ctx = recipe.setup(hmap, size);
  try {
    timedCall(hmap, recipe, ctx); // untimed warmup

    const times = [];
    let elapsed = 0;
    for (let rep = 0; rep < MAX_REPS; rep++) {
      const dt = timedCall(hmap, recipe, ctx);
      times.push(dt);
      elapsed += dt;
      if (times.length >= MIN_REPS && elapsed >= CELL_BUDGET_MS) break;
    }

    const sorted = [...times].sort((a, b) => a - b);
    return {
      reps: times.length,
      median_ms: median(sorted),
      min_ms: sorted[0],
      max_ms: sorted[sorted.length - 1],
    };
  } finally {
    ctx.dispose?.();
  }
}

function formatTable(rows, sizes) {
  const headers = ["function", ...sizes.map(String)];
  const cells = rows.map((row) => [
    row.name,
    ...sizes.map((size) => {
      const cell = row.cells[size];
      return `${cell.median_ms.toFixed(3)} (${cell.reps})`;
    }),
  ]);

  const widths = headers.map((h, col) =>
    Math.max(h.length, ...cells.map((r) => r[col].length)),
  );
  const line = (values) =>
    values
      .map((value, col) =>
        col === 0 ? value.padEnd(widths[0]) : value.padStart(widths[col]),
      )
      .join("  ")
      .trimEnd();

  const bar = widths.map((w) => "-".repeat(w)).join("  ");
  return [line(headers), bar, ...cells.map(line)].join("\n");
}

function gitCommit() {
  try {
    return execFileSync("git", ["rev-parse", "--short", "HEAD"], {
      cwd: root,
      encoding: "utf8",
    }).trim();
  } catch {
    return "unknown";
  }
}

// --- main -----------------------------------------------------------------

const opts = parseArgs(process.argv.slice(2));

if (!existsSync(distEntry)) {
  fail(`missing ${relative(root, distEntry)} - run npm run build first`);
}
if (
  !existsSync(distDir) ||
  readdirSync(distDir).filter((f) => f.endsWith(".wasm")).length === 0
) {
  fail("missing dist/*.wasm - run npm run build first");
}

const manifestNames = manifestApiFunctionNames();
const missing = manifestNames.filter((name) => !(name in recipes));
if (missing.length > 0) {
  fail(`no benchmark recipe for manifest functions: ${missing.join(", ")}`);
}

const selected = opts.only
  ? manifestNames.filter((name) =>
      opts.only.some((filter) => name.toLowerCase().includes(filter)),
    )
  : manifestNames;
if (selected.length === 0) {
  fail(`no manifest function matches --only ${opts.only?.join(",")}`);
}

// `init()` returns the raw embind module; `initHighmap()` wraps it in the
// high-level API object this harness (and the demos) call.
const { initHighmap } = await import(pathToFileURL(distEntry).href);
const hmap = await initHighmap();

const rows = [];
for (const name of selected) {
  const cells = {};
  for (const size of opts.sizes) {
    cells[size] = timeCell(hmap, recipes[name], size);
  }
  rows.push({ name, cells });
}

const date = new Date().toISOString();
const context = {
  date,
  node: process.version,
  platform: platform(),
  arch: arch(),
  gitCommit: gitCommit(),
  wasmBytes: readdirSync(distDir)
    .filter((f) => f.endsWith(".wasm"))
    .reduce((sum, f) => sum + statSync(join(distDir, f)).size, 0),
};

const benchmarks = [];
for (const row of rows) {
  for (const size of opts.sizes) {
    const cell = row.cells[size];
    benchmarks.push({
      name: row.name,
      grid: size,
      reps: cell.reps,
      median_ms: Number(cell.median_ms.toFixed(4)),
      min_ms: Number(cell.min_ms.toFixed(4)),
      max_ms: Number(cell.max_ms.toFixed(4)),
    });
  }
}

const outFile = opts.out
  ? resolve(process.cwd(), opts.out)
  : join(
      root,
      "bench",
      "results",
      `bench-${date.replace(/[:.]/g, "-")}-${hostname()}.json`,
    );
mkdirSync(dirname(outFile), { recursive: true });
writeFileSync(outFile, JSON.stringify({ context, benchmarks }, null, 2) + "\n");

console.log(formatTable(rows, opts.sizes));
console.log("");
console.log(
  `${benchmarks.length} cells | node ${context.node} | ${context.platform}/${context.arch} | commit ${context.gitCommit} | wasm ${context.wasmBytes} bytes`,
);
console.log(`results -> ${outFile}`);
