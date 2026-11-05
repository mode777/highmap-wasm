import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { init } from "../src/loader/init.ts";

const require = createRequire(import.meta.url);
const wasmPath = require.resolve("../dist/highmap.wasm");

describe("loader: init()", () => {
  it("resolves with default artifact location", async () => {
    const mod = await init();
    expect(mod.makeArray(2, 2, 0).ni()).toBe(2);
  });

  it("accepts wasmBinary bytes and bypasses location entirely", async () => {
    const bytes = readFileSync(wasmPath);
    const mod = await init({ wasmBinary: bytes });
    expect(mod.makeArray(3, 5, 1).nj()).toBe(5);
  });

  it("accepts a locateWasm mapping to a file URL", async () => {
    const mod = await init({
      locateWasm: (filename) =>
        new URL(`../dist/${filename}`, import.meta.url).href,
    });
    expect(mod.makeArray(1, 1, 0).ni()).toBe(1);
  });

  it("creates independent instances with separate heaps", async () => {
    const a = await init({ wasmBinary: readFileSync(wasmPath) });
    const b = await init({ wasmBinary: readFileSync(wasmPath) });
    expect(a).not.toBe(b);
    expect(a.wasmMemory).not.toBe(b.wasmMemory);
  });

  it("rejects with a descriptive error when the artifact is unreachable", async () => {
    const broken = "file://" + fileURLToPath(new URL("../dist/", import.meta.url)) + "does-not-exist.wasm";
    await expect(init({ locateWasm: () => broken })).rejects.toThrow(/highmap\.wasm|fetch|wasm/i);
  });
});
