import { beforeAll, describe, expect, it } from "vitest";
import { coerceGrid } from "../src/api/coerce.ts";
import { HighmapError, HArray, NoiseType, initHighmap } from "../src/index.ts";
import { init } from "../src/loader/init.ts";
import type { HighmapModule } from "../src/loader/module.ts";

let hmap: Awaited<ReturnType<typeof initHighmap>>;
let mod: HighmapModule;

beforeAll(async () => {
  mod = await init();
  hmap = await initHighmap();
});

describe("HArray lifecycle", () => {
  it("exposes shape and a live view of the computed values", () => {
    const g = hmap.noise(NoiseType.PERLIN, [16, 8], [4, 2], 0);
    expect(g.shape).toEqual([16, 8]);
    expect(g.data).toBeInstanceOf(Float32Array);
    expect(g.data.length).toBe(16 * 8);
  });

  it("copyData() survives free(); data access after free throws", () => {
    const g = hmap.makeGrid(4, 4, 0.5);
    const copy = g.copyData();
    g.free();
    expect(copy.length).toBe(16);
    expect(copy[0]).toBeCloseTo(0.5);
    expect(() => g.data).toThrow(HighmapError);
    expect(() => g.copyData()).toThrow(HighmapError);
  });

  it("free() is idempotent", () => {
    const g = hmap.makeGrid(2, 2);
    g.free();
    expect(() => g.free()).not.toThrow();
  });

  it("reclaims wrappers that are garbage collected without free()", () => {
    // Allocate many wrappers, drop all references, force GC. The observable
    // contract: the process does not leak into a wasm OOM abort and the
    // module stays usable afterwards.
    for (let i = 0; i < 500; i++) {
      hmap.makeGrid(64, 64, i / 500).shape;
    }
    globalThis.gc?.();
    globalThis.gc?.();
    expect(hmap.makeGrid(2, 2, 1).copyData()[0]).toBe(1);
  });
});

describe("input coercion (unit level)", () => {
  const values = new Float32Array([0, 1, 2, 3, 4, 5]);

  it("copies plain { data, shape } inputs and preserves values and order", () => {
    const { raw, source } = coerceGrid(
      { data: values, shape: [2, 3] },
      "grid",
      mod,
    );
    expect(source).toBeNull();
    expect(raw.ni()).toBe(2);
    expect(raw.nj()).toBe(3);
    const view = new Float32Array(mod.wasmMemory.buffer, raw.dataPtr(), 6);
    expect(Array.from(view)).toEqual([0, 1, 2, 3, 4, 5]);
  });

  it("passes HArray inputs through zero-copy (aliased)", () => {
    const g = hmap.makeGrid(2, 3, 0);
    const { raw, source } = coerceGrid(g, "grid", mod);
    expect(source).toBe(g);
    expect(raw).toBe(g.getRaw());
  });

  it("rejects a data/shape length mismatch, naming the argument", () => {
    expect(() =>
      coerceGrid({ data: new Float32Array(10), shape: [4, 4] }, "myArg", mod),
    ).toThrow(HighmapError);
    expect(() =>
      coerceGrid({ data: new Float32Array(10), shape: [4, 4] }, "myArg", mod),
    ).toThrow(/myArg: data length 10 does not match shape \[4, 4\]/);
  });

  it("rejects non-float32 data before any wasm call", () => {
    expect(() =>
      coerceGrid(
        { data: new Uint8Array(16) as unknown as Float32Array, shape: [4, 4] },
        "myArg",
        mod,
      ),
    ).toThrow(/myArg\.data: expected a Float32Array, got Uint8Array/);
  });

  it("rejects malformed shapes", () => {
    const data = new Float32Array(4);
    expect(() =>
      coerceGrid(
        { data, shape: [0, 4] as unknown as [number, number] },
        "g",
        mod,
      ),
    ).toThrow(/g\.shape: expected two positive integers/);
    expect(() =>
      coerceGrid({ data, shape: "nope" as unknown as [number, number] }, "g", mod),
    ).toThrow(/g\.shape: expected two positive integers/);
  });

  it("rejects freed HArray inputs", () => {
    const g = hmap.makeGrid(2, 2);
    g.free();
    expect(() => coerceGrid(g, "g", mod)).toThrow(/g: the HArray was freed/);
  });
});

describe("shape convention (wrapper level)", () => {
  it("uses [ni, nj] with data length ni*nj", () => {
    const g = hmap.makeGrid(3, 7, 2);
    expect(g.shape).toEqual([3, 7]);
    expect(g.data.length).toBe(21);
    expect(HArray.isWrapped(g)).toBe(true);
  });
});
