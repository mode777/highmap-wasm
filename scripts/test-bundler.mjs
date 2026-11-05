// Consumer bundler tests (spec: wasm-loader - bundler emits the wasm
// artifact as an asset, one import everywhere).
//
// Leg 1 - esbuild: bundle a scratch consumer against dist/index.js, copy the
//   artifact next to the bundle (esbuild does not auto-handle `new URL`
//   assets), execute it in Node and assert init() + generation work.
// Leg 2 - Vite: build a scratch library consumer; Vite natively understands
//   the `new URL('./highmap.wasm', import.meta.url)` pattern, so the build
//   MUST emit the wasm as an asset without plugins or configuration.

import { build as esbuild } from "esbuild";
import { execFileSync, execSync } from "node:child_process";
import {
  copyFileSync,
  mkdirSync,
  mkdtempSync,
  readdirSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("..", import.meta.url));
const distEntry = join(root, "dist", "index.js");
const distWasm = join(root, "dist", "highmap.wasm");
const scratch = mkdtempSync(join(tmpdir(), "highmap-consumer-"));

const consumerSource = `
import { initHighmap } from "highmap-wasm";
const hmap = await initHighmap();
const g = hmap.noise(hmap.NoiseType.PERLIN, [16, 16], [4, 2], 0);
if (g.shape[0] !== 16) throw new Error("bad shape");
g.free();
console.log("consumer OK");
`;

function runNode(file, cwd) {
  return execFileSync(process.execPath, [file], { cwd, encoding: "utf8" });
}

try {
  // --- leg 1: esbuild -------------------------------------------------------
  {
    const dir = join(scratch, "esbuild");
    mkdirSync(dir, { recursive: true });
    writeFileSync(join(dir, "entry.mjs"), consumerSource);

    await esbuild({
      entryPoints: [join(dir, "entry.mjs")],
      bundle: true,
      format: "esm",
      platform: "node",
      outdir: join(dir, "out"),
      alias: { "highmap-wasm": distEntry },
      external: ["node:*"],
      logLevel: "silent",
    });

    // esbuild leaves `new URL(...)` untouched; place the artifact alongside
    // the bundle as a consumer deployment would.
    copyFileSync(distWasm, join(dir, "out", "highmap.wasm"));

    const out = runNode(join(dir, "out", "entry.js"), dir);
    if (!out.includes("consumer OK")) throw new Error(`esbuild leg: ${out}`);
    console.log("  esbuild leg OK (bundle executes, init + generation work)");
  }

  // --- leg 2: vite (real package install) ----------------------------------
  {
    const dir = join(scratch, "vite");
    mkdirSync(dir, { recursive: true });

    // pack the real tarball and install it like a consumer would
    execSync(`npm pack --pack-destination "${dir}" --silent`, {
      cwd: root,
      encoding: "utf8",
    });
    const tarball = readdirSync(dir).find((f) => f.endsWith(".tgz"));
    if (!tarball) throw new Error("vite leg: npm pack produced no tarball");

    writeFileSync(
      join(dir, "package.json"),
      JSON.stringify({ name: "consumer", private: true, type: "module" }, null, 2),
    );
    execSync(`npm install "${join(dir, tarball)}" --no-save --silent`, {
      cwd: dir,
      encoding: "utf8",
    });

    writeFileSync(join(dir, "main.js"), consumerSource);
    writeFileSync(
      join(dir, "index.html"),
      `<!doctype html><html><body><script type="module" src="/main.js"></script></body></html>`,
    );
    writeFileSync(
      join(dir, "vite.config.mjs"),
      `
      export default {
        build: { target: "esnext", minify: false },
        logLevel: "silent",
      };
      `,
    );

    execFileSync(
      process.execPath,
      [join(root, "node_modules", "vite", "bin", "vite.js"), "build"],
      { cwd: dir, encoding: "utf8" },
    );

    const findWasm = (d) =>
      readdirSync(d, { withFileTypes: true }).flatMap((e) =>
        e.isDirectory()
          ? findWasm(join(d, e.name))
          : /\.wasm$/.test(e.name)
            ? [join(d, e.name)]
            : [],
      );
    const wasm = findWasm(join(dir, "dist"));
    if (wasm.length === 0) {
      throw new Error("vite leg: build did not emit the wasm asset");
    }
    console.log(
      `  vite leg OK (wasm emitted as asset: ${wasm.map((w) => w.replace(dir, "")).join(", ")})`,
    );
  }

  console.log("consumer bundler tests OK");
} finally {
  rmSync(scratch, { recursive: true, force: true });
}
