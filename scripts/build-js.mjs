// Bundles the package entry into dist/index.js (esbuild) and installs the
// seed-surface type declaration. The Emscripten ES6 glue (dist/highmap.js)
// is bundled in; its `new URL('highmap.wasm', import.meta.url)` reference
// makes bundlers emit dist/highmap.wasm as an asset.
//
// Requires dist/highmap.js + dist/highmap.wasm from scripts/build-wasm.sh.

import { build } from "esbuild";
import { copyFileSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");

await build({
  entryPoints: [join(root, "src/index.ts")],
  bundle: true,
  format: "esm",
  platform: "neutral",
  target: "es2020",
  outfile: join(root, "dist/index.js"),
  // keep the wasm reference intact so bundlers emit it as an asset
  loader: { ".wasm": "file" },
  // the Emscripten glue dynamically imports node: modules in its Node branch
  // only; keeping them external leaves one browser+node bundle
  external: ["node:*"],
  minify: true,
  logLevel: "info",
});

// Emscripten emits `new URL("highmap.wasm", import.meta.url)` (bare path);
// bundlers only auto-emit URL assets for "./"-relative paths. Rewrite it in
// the bundled entry so Vite/Rollup/esbuild/webpack5 pick the wasm up with no
// plugin. (dist/highmap.js keeps the original form.)
{
  const outFile = join(root, "dist/index.js");
  const bundled = readFileSync(outFile, "utf8");
  const fixed = bundled.replaceAll(
    'new URL("highmap.wasm",import.meta.url)',
    'new URL("./highmap.wasm",import.meta.url)',
  );
  if (fixed === bundled && !bundled.includes('new URL("./highmap.wasm"')) {
    throw new Error("wasm URL pattern not found in bundle - update the rewrite");
  }
  writeFileSync(outFile, fixed);
}

copyFileSync(
  join(root, "scripts/index.d.ts"),
  join(root, "dist/index.d.ts"),
);

console.log("dist/index.js + dist/index.d.ts written");
