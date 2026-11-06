import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { beforeAll, describe, expect, it } from "vitest";
import { DistanceFunction, NoiseType, initHighmap } from "../src/index.ts";

const require = createRequire(import.meta.url);
const golden = JSON.parse(
  readFileSync(require.resolve("./golden/scenes.json"), "utf8"),
) as Record<string, { shape: [number, number]; sha256: string; first16: number[] }>;

/**
 * Golden-value determinism snapshots (AD-011), captured from the artifact
 * via `node scripts/golden-capture.mjs`. These pin exact bytes: same build +
 * same arguments MUST give identical grids across calls. When a scene or
 * toolchain pin changes intentionally, recapture and review the JSON diff.
 */

let hmap: Awaited<ReturnType<typeof initHighmap>>;

beforeAll(async () => {
  hmap = await initHighmap();
});

const sha = (g: { data: Float32Array; shape: [number, number] }) =>
  createHash("sha256")
    .update(Buffer.from(g.data.buffer, g.data.byteOffset, g.data.byteLength))
    .digest("hex");

describe("golden scenes", () => {
  it("noise-perlin-seed0", () => {
    const g = hmap.noise(NoiseType.PERLIN, [64, 64], [4, 2], 0);
    expect(g.shape).toEqual(golden["noise-perlin-seed0"]!.shape);
    expect(sha(g)).toBe(golden["noise-perlin-seed0"]!.sha256);
    expect(Array.from(g.data.slice(0, 16))).toEqual(golden["noise-perlin-seed0"]!.first16);
  });

  it("noise-simplex2s-seed1", () => {
    const g = hmap.noise(NoiseType.SIMPLEX2S, [64, 64], [4, 2], 1);
    expect(sha(g)).toBe(golden["noise-simplex2s-seed1"]!.sha256);
  });

  it("noise-worley-seed2", () => {
    const g = hmap.noise(NoiseType.WORLEY, [64, 64], [4, 2], 2);
    expect(sha(g)).toBe(golden["noise-worley-seed2"]!.sha256);
  });

  it("noise-fbm-perlin-seed42", () => {
    const g = hmap.noiseFbm(NoiseType.PERLIN, [64, 64], [4, 2], 42);
    expect(sha(g)).toBe(golden["noise-fbm-perlin-seed42"]!.sha256);
  });

  it("noise-ridged-perlin-seed42", () => {
    const g = hmap.noiseRidged(NoiseType.PERLIN, [64, 64], [4, 2], 42);
    expect(sha(g)).toBe(golden["noise-ridged-perlin-seed42"]!.sha256);
  });

  it("warp (composed pipeline)", () => {
    const base = hmap.noise(NoiseType.PERLIN, [64, 64], [4, 2], 0);
    const fbm = hmap.noiseFbm(NoiseType.SIMPLEX2S, [64, 64], [4, 2], 0);
    hmap.warp(base, hmap.multiply(fbm, 0.1), hmap.multiply(fbm, 0.1));
    expect(sha(base)).toBe(golden.warp!.sha256);
  });

  it("hydraulic-musgrave-seed11 (finite + byte-exact)", () => {
    const t = hmap.noiseFbm(NoiseType.SIMPLEX2S, [64, 64], [4, 2], 11);
    hmap.remap(t);
    hmap.hydraulicMusgrave(t, { iterations: 50 });
    for (const v of t.data) expect(Number.isFinite(v)).toBe(true);
    expect(sha(t)).toBe(golden["hydraulic-musgrave-seed11"]!.sha256);
  });

  it("hydraulic-stream-seed11 (finite + byte-exact)", () => {
    const t = hmap.noiseFbm(NoiseType.SIMPLEX2S, [64, 64], [4, 2], 11);
    hmap.remap(t);
    hmap.hydraulicStream(t, 0.1, 0.01);
    for (const v of t.data) expect(Number.isFinite(v)).toBe(true);
    expect(sha(t)).toBe(golden["hydraulic-stream-seed11"]!.sha256);
  });

  it("thermal-seed11 (finite + byte-exact)", () => {
    const t = hmap.noiseFbm(NoiseType.SIMPLEX2S, [64, 64], [4, 2], 11);
    hmap.remap(t);
    hmap.thermal(t, 0.1 / 64, { iterations: 10 });
    for (const v of t.data) expect(Number.isFinite(v)).toBe(true);
    expect(sha(t)).toBe(golden["thermal-seed11"]!.sha256);
  });

  it("thermal-conserve-seed11 (finite + byte-exact)", () => {
    const t = hmap.noiseFbm(NoiseType.SIMPLEX2S, [64, 64], [4, 2], 11);
    hmap.remap(t);
    hmap.thermalConserve(t, 0.1 / 64, { iterations: 10, rate: 0.5 });
    for (const v of t.data) expect(Number.isFinite(v)).toBe(true);
    expect(sha(t)).toBe(golden["thermal-conserve-seed11"]!.sha256);
  });

  it("falloff golden stability with each DistanceFunction", () => {
    // every distance function must stay finite and deterministic
    for (const df of Object.keys(DistanceFunction)) {
      const g = hmap.noiseFbm(NoiseType.PERLIN, [64, 64], [4, 2], 3);
      hmap.remap(g);
      hmap.falloff(g, { distFct: df as DistanceFunction });
      const h = hmap.noiseFbm(NoiseType.PERLIN, [64, 64], [4, 2], 3);
      hmap.remap(h);
      hmap.falloff(h, { distFct: df as DistanceFunction });
      expect(sha(g)).toBe(sha(h));
    }
  });
});
