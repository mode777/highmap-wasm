import { beforeAll, describe, expect, it } from "vitest";
import {
  DistanceFunction,
  HArray,
  NoiseType,
  initHighmap,
} from "../src/index.ts";

let hmap: Awaited<ReturnType<typeof initHighmap>>;

beforeAll(async () => {
  hmap = await initHighmap();
});

describe("noise family (pyHighMap parity)", () => {
  it.each(["noise", "noiseFbm", "noisePingpong", "noiseRidged"] as const)(
    "%s returns the requested shape and is deterministic",
    (fn) => {
      const g1 = hmap[fn](NoiseType.SIMPLEX2S, [32, 16], [4, 2], 42);
      const g2 = hmap[fn](NoiseType.SIMPLEX2S, [32, 16], [4, 2], 42);
      expect(g1.shape).toEqual([32, 16]);
      expect(Array.from(g1.data)).toEqual(Array.from(g2.data));
    },
  );

  it("noiseFbm: omitted options equal explicit pyHighMap defaults", () => {
    const a = hmap.noiseFbm(NoiseType.PERLIN, [16, 16], [4, 2], 7);
    const b = hmap.noiseFbm(NoiseType.PERLIN, [16, 16], [4, 2], 7, {
      octaves: 8,
      weight: 0.7,
      persistence: 0.5,
      lacunarity: 2,
    });
    expect(Array.from(a.data)).toEqual(Array.from(b.data));
  });

  it("noiseRidged: kSmoothing default is 0.1", () => {
    const a = hmap.noiseRidged(NoiseType.PERLIN, [16, 16], [4, 2], 7);
    const b = hmap.noiseRidged(NoiseType.PERLIN, [16, 16], [4, 2], 7, {
      kSmoothing: 0.1,
    });
    expect(Array.from(a.data)).toEqual(Array.from(b.data));
  });

  it("different NoiseType values select different generators", () => {
    const perlin = hmap.noise(NoiseType.PERLIN, [32, 32], [4, 2], 0);
    const simplex = hmap.noise(NoiseType.SIMPLEX2S, [32, 32], [4, 2], 0);
    expect(Array.from(perlin.data)).not.toEqual(Array.from(simplex.data));
  });
});

describe("filters and value ranges", () => {
  it("clamp constrains values to [vmin, vmax]", () => {
    const g = hmap.makeGrid(8, 8, 5);
    hmap.clamp(g, { vmin: 0, vmax: 1 });
    for (const v of g.data) expect(v).toBeLessThanOrEqual(1);
  });

  it("clamp with plain input leaves the caller's data untouched", () => {
    const data = new Float32Array(64).fill(5);
    const out = hmap.clamp({ data, shape: [8, 8] }, { vmin: 0, vmax: 1 });
    expect(data[0]).toBe(5); // plain inputs are copied in, never mutated
    expect(out.data[0]).toBe(1);
    expect(out).not.toBe(data);
  });

  it("clamp: both input forms produce identical results", () => {
    const values = new Float32Array(64).map((_, i) => i / 8);
    const viaPlain = hmap.clamp(
      { data: new Float32Array(values), shape: [8, 8] },
      { vmin: 0.2, vmax: 0.8 },
    );
    const harr = hmap.makeGrid(8, 8, 0);
    harr.data.set(values);
    const viaHArray = hmap.clamp(harr, { vmin: 0.2, vmax: 0.8 });
    expect(Array.from(viaPlain.data)).toEqual(Array.from(viaHArray.data));
  });

  it("remap rescales to [0, 1]", () => {
    const g = hmap.noiseFbm(NoiseType.SIMPLEX2, [32, 32], [4, 2], 3);
    hmap.remap(g);
    const values = Array.from(g.data);
    expect(Math.min(...values)).toBeCloseTo(0, 5);
    expect(Math.max(...values)).toBeCloseTo(1, 5);
  });

  it("falloff darkens the borders (defaults + distFct option)", () => {
    const g = hmap.noiseFbm(NoiseType.PERLIN, [64, 64], [4, 2], 3);
    hmap.remap(g);
    const centerBefore = g.data[32 * 64 + 32]!;
    hmap.falloff(g);
    expect(g.data[0]!).toBeLessThan(g.data[32 * 64 + 32]!);
    expect(g.data[32 * 64 + 32]!).toBeLessThanOrEqual(centerBefore + 1e-6);
    // explicit defaults equal omitted options
    const g2 = hmap.noiseFbm(NoiseType.PERLIN, [64, 64], [4, 2], 3);
    hmap.remap(g2);
    hmap.falloff(g2, {
      strength: 1,
      distFct: DistanceFunction.EUCLIDIAN,
    });
    expect(g.data[0]!).toBeCloseTo(g2.data[0]!, 5);
  });

  it("plateau and smoothFill preserve shape and stay finite", () => {
    const g1 = hmap.noiseFbm(NoiseType.PERLIN, [32, 32], [4, 2], 3);
    hmap.plateau(g1, 4, 0.5);
    expect(g1.shape).toEqual([32, 32]);
    for (const v of g1.data) expect(Number.isFinite(v)).toBe(true);

    const g2 = hmap.noiseFbm(NoiseType.PERLIN, [32, 32], [4, 2], 3);
    hmap.smoothFill(g2, 4, { k: 0.1 });
    expect(g2.shape).toEqual([32, 32]);
    for (const v of g2.data) expect(Number.isFinite(v)).toBe(true);
  });

  it("smoothFill: k default is 0.1", () => {
    const a = hmap.noiseFbm(NoiseType.PERLIN, [32, 32], [4, 2], 3);
    const b = hmap.noiseFbm(NoiseType.PERLIN, [32, 32], [4, 2], 3);
    hmap.smoothFill(a, 4);
    hmap.smoothFill(b, 4, { k: 0.1 });
    expect(Array.from(a.data)).toEqual(Array.from(b.data));
  });
});

describe("hillshade and warp", () => {
  it("hillshade returns a new grid in [0, 1]", () => {
    const g = hmap.noiseFbm(NoiseType.PERLIN, [64, 64], [4, 2], 3);
    hmap.remap(g);
    const shaded = hmap.hillshade(g, Math.PI / 4, Math.PI / 4);
    expect(shaded).not.toBe(g);
    expect(shaded.shape).toEqual([64, 64]);
    const values = Array.from(shaded.data);
    expect(Math.min(...values)).toBeGreaterThanOrEqual(0);
    expect(Math.max(...values)).toBeLessThanOrEqual(1);
  });

  it("warp displaces the grid (pyHighMap example shape)", () => {
    const base = hmap.noise(NoiseType.PERLIN, [64, 64], [4, 2], 0);
    const fbm = hmap.noiseFbm(NoiseType.SIMPLEX2S, [64, 64], [4, 2], 0);
    const dx = hmap.scale(hmap.multiply(fbm, 0.1), 1);
    const dy = hmap.multiply(fbm, 0.1);
    const before = Array.from(base.data);
    hmap.warp(base, dx, dy);
    expect(Array.from(base.data)).not.toEqual(before);
  });
});

describe("algebra layer", () => {
  const a = () => {
    const g = hmap.makeGrid(4, 4, 0);
    g.data.set(new Float32Array([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]));
    return g;
  };
  const b = () => hmap.makeGrid(4, 4, 2);

  it("grid-grid add/subtract/multiply/divide elementwise", () => {
    expect(Array.from(hmap.add(a(), b()).data)).toEqual([
      2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
    ]);
    expect(Array.from(hmap.subtract(b(), a()).data)[0]).toBe(2);
    expect(Array.from(hmap.multiply(a(), b()).data)[3]).toBe(6);
    expect(Array.from(hmap.divide(b(), a()).data)[1]).toBeCloseTo(2);
  });

  it("grid-scalar forms", () => {
    expect(Array.from(hmap.add(a(), 10).data)[0]).toBe(10);
    expect(Array.from(hmap.subtract(a(), 1).data)[0]).toBe(-1);
    expect(Array.from(hmap.multiply(a(), 0.5).data)[2]).toBeCloseTo(1);
    expect(Array.from(hmap.divide(a(), 2).data)[4]).toBeCloseTo(2);
  });

  it("scale multiplies in place for HArray inputs", () => {
    const g = hmap.makeGrid(2, 2, 3);
    const out = hmap.scale(g, 2);
    expect(out).toBe(g); // zero-copy aliasing by design (AD-001)
    expect(g.data[0]!).toBe(6);
    expect(out.data[0]).toBe(6);
  });
});

describe("erosion (CPU twins)", () => {
  it("hydraulicMusgrave: deterministic, shape preserved, values move", () => {
    const terrain1 = hmap.noiseFbm(NoiseType.SIMPLEX2S, [64, 64], [4, 2], 11);
    const terrain2 = hmap.noiseFbm(NoiseType.SIMPLEX2S, [64, 64], [4, 2], 11);
    hmap.remap(terrain1);
    hmap.remap(terrain2);
    const before = Array.from(terrain1.data);

    hmap.hydraulicMusgrave(terrain1, { iterations: 50 });
    hmap.hydraulicMusgrave(terrain2, { iterations: 50 });

    expect(terrain1.shape).toEqual([64, 64]);
    expect(Array.from(terrain1.data)).toEqual(Array.from(terrain2.data));
    expect(Array.from(terrain1.data)).not.toEqual(before);
  });

  it("hydraulicMusgrave defaults equal explicit pyHighMap defaults", () => {
    const t1 = hmap.noiseFbm(NoiseType.SIMPLEX2S, [64, 64], [4, 2], 11);
    const t2 = hmap.noiseFbm(NoiseType.SIMPLEX2S, [64, 64], [4, 2], 11);
    hmap.remap(t1);
    hmap.remap(t2);
    hmap.hydraulicMusgrave(t1, { iterations: 20 });
    hmap.hydraulicMusgrave(t2, {
      iterations: 20,
      cCapacity: 1,
      cErosion: 0.1,
      cDeposition: 0.1,
      waterLevel: 0.01,
      evapRate: 0.01,
    });
    expect(Array.from(t1.data)).toEqual(Array.from(t2.data));
    for (const v of t1.data) expect(Number.isFinite(v)).toBe(true);
  });

  it("hydraulicStream: deterministic and shape preserved", () => {
    const t1 = hmap.noiseFbm(NoiseType.SIMPLEX2S, [64, 64], [4, 2], 11);
    const t2 = hmap.noiseFbm(NoiseType.SIMPLEX2S, [64, 64], [4, 2], 11);
    hmap.remap(t1);
    hmap.remap(t2);
    hmap.hydraulicStream(t1, 0.1, 0.01);
    hmap.hydraulicStream(t2, 0.1, 0.01, { ir: 1, clippingRatio: 10 });
    expect(t1.shape).toEqual([64, 64]);
    expect(Array.from(t1.data)).toEqual(Array.from(t2.data));
  });
});

describe("pyHighMap README example translates end to end", () => {
  it("composes noise + fbm + warp + falloff", () => {
    const noiseBase = hmap.noise(NoiseType.PERLIN, [128, 64], [4, 2], 0);
    const noiseFbm = hmap.noiseFbm(NoiseType.SIMPLEX2S, [128, 64], [4, 2], 0);
    hmap.warp(
      noiseBase,
      hmap.multiply(noiseFbm, 0.1),
      hmap.multiply(noiseFbm, 0.1),
    );
    hmap.falloff(noiseFbm);
    expect(noiseBase.shape).toEqual([128, 64]);
    expect(HArray.isWrapped(noiseBase)).toBe(true);
  });
});
