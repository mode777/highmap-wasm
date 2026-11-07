import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { beforeAll, describe, expect, it } from "vitest";
import { HArray, NoiseType, initHighmap } from "../src/index.ts";
import type {
  AdvectionParticleOptions,
  AdvectionWarpOptions,
  RiftsOptions,
} from "../src/index.ts";

/**
 * Rifts / advection JS surface (add-rifts-advection-cpu-ports, design D6 /
 * AD-027). The algorithms are CPU ports of upstream OpenCL kernels; their
 * numerical agreement with the GPU family is verified on the desktop build
 * (tests/src/test_rifts_advection_cpu.cpp). This file covers what the JS user
 * can observe: in-place versus new-grid semantics, tuple conversion, option
 * defaults, validation, deferred-overload absence and determinism.
 */

const require = createRequire(import.meta.url);
const manifest = readFileSync(
  require.resolve("../HighMap/wasm/manifest.md"),
  "utf8",
);

let hmap: Awaited<ReturnType<typeof initHighmap>>;

beforeAll(async () => {
  hmap = await initHighmap();
});

/** A remapped fBm terrain grid in [0, 1]; the rifts mask expects bounded,
 * non-negative elevation. */
const terrain = (seed = 3, shape: [number, number] = [16, 16]): HArray => {
  const g = hmap.noiseFbm(NoiseType.PERLIN, shape, [4, 2], seed);
  hmap.remap(g, { vmin: 0, vmax: 1 });
  return g;
};

/** A remapped fBm advected field in [0, 1] (same shape as `terrain`). */
const field = (seed = 7, shape: [number, number] = [16, 16]): HArray => {
  const g = hmap.noiseFbm(NoiseType.SIMPLEX2S, shape, [4, 2], seed);
  hmap.remap(g, { vmin: 0, vmax: 1 });
  return g;
};

const values = (g: HArray) => Array.from(g.data);

describe("rifts", () => {
  it("erodes an HArray in place and returns the same wrapper", () => {
    const g = terrain();
    const before = values(g);

    const out = hmap.rifts(g, [4, 1.2], 30, 0.1, 11);

    expect(out).toBe(g);
    expect(out.shape).toEqual([16, 16]);
    expect(values(out)).not.toEqual(before);
  });

  it("plain grid input leaves the caller's data untouched", () => {
    const source = terrain();
    const data = Float32Array.from(source.data);
    const original = Float32Array.from(data);

    const out = hmap.rifts(
      { data, shape: [16, 16] },
      [4, 1.2],
      30,
      0.1,
      11,
    );

    expect(Array.from(data)).toEqual(Array.from(original));
    expect(out.shape).toEqual([16, 16]);
    expect(values(out)).not.toEqual(Array.from(original));
  });

  it("requires kw, angle, amplitude and seed", () => {
    const g = terrain();

    expect(() =>
      hmap.rifts(g, undefined as unknown as [number, number], 30, 0.1, 11),
    ).toThrow(/kw/);
    expect(() =>
      hmap.rifts(g, [4] as unknown as [number, number], 30, 0.1, 11),
    ).toThrow(/kw/);
    expect(() =>
      hmap.rifts(g, [4, 1.2], undefined as unknown as number, 0.1, 11),
    ).toThrow(/angle/);
    expect(() =>
      hmap.rifts(g, [4, 1.2], 30, undefined as unknown as number, 11),
    ).toThrow(/amplitude/);
    expect(() =>
      hmap.rifts(g, [4, 1.2], 30, 0.1, undefined as unknown as number),
    ).toThrow(/seed/);
  });

  it("omitted options equal every explicit default", () => {
    const a = terrain(5);
    const b = terrain(5);

    hmap.rifts(a, [4, 1.2], 30, 0.1, 11);
    hmap.rifts(b, [4, 1.2], 30, 0.1, 11, {
      elevationNoiseShift: 0,
      kSmoothBottom: 0.05,
      kSmoothTop: 0.05,
      radialSpreadAmp: 0.2,
      elevationNoiseAmp: 0.1,
      clampVmin: 0,
      remapVmin: 0,
      applyMask: true,
      reverseMask: false,
      maskGamma: 1,
      center: [0.5, 0.5],
      bbox: [0, 1, 0, 1],
    });

    expect(values(a)).toEqual(values(b));
  });

  it("honours an asymmetric bbox in [xmin, xmax, ymin, ymax] order", () => {
    const a = terrain(11);
    const b = terrain(11);
    const c = terrain(11);

    hmap.rifts(a, [4, 1.2], 30, 0.1, 11, { bbox: [0, 1, 0.25, 0.75] });
    hmap.rifts(b, [4, 1.2], 30, 0.1, 11, { bbox: [0, 0.25, 1, 0.75] });
    hmap.rifts(c, [4, 1.2], 30, 0.1, 11);

    expect(values(a)).not.toEqual(values(b));
    expect(values(a)).not.toEqual(values(c));
  });

  it("honours a non-default center", () => {
    const a = terrain(13);
    const b = terrain(13);
    const c = terrain(13);

    hmap.rifts(a, [4, 1.2], 30, 0.1, 11, { center: [0.25, 0.75] });
    hmap.rifts(b, [4, 1.2], 30, 0.1, 11, { center: [0.25, 0.75] });
    hmap.rifts(c, [4, 1.2], 30, 0.1, 11);

    // the call is deterministic, so the difference from the default center
    // is a real effect of the option and not run-to-run noise
    expect(values(a)).toEqual(values(b));
    expect(values(a)).not.toEqual(values(c));
  });

  it("preserves the applyMask/reverseMask quirk", () => {
    const a = terrain(7);
    const b = terrain(7);
    const before = values(a);

    hmap.rifts(a, [4, 1.2], 30, 0.1, 11, {
      applyMask: false,
      reverseMask: false,
    });
    expect(values(a)).not.toEqual(before);

    // the reverser flips t even when the mask is off: t = 1 - 1 = 0
    hmap.rifts(b, [4, 1.2], 30, 0.1, 11, {
      applyMask: false,
      reverseMask: true,
    });
    expect(values(b)).toEqual(before);
  });

  it("reverseMask inverts the elevation blend per region", () => {
    // elevation zero on the left half, one on the right half
    const makeMasked = (): HArray => {
      const g = hmap.makeGrid(16, 16, 1);
      const d = g.data;
      for (let j = 0; j < 16; j++)
        for (let i = 0; i < 8; i++) d[j * 16 + i] = 0;
      return g;
    };

    const a = makeMasked();
    const b = makeMasked();

    hmap.rifts(a, [4, 1.2], 30, 0.1, 11, {
      applyMask: true,
      reverseMask: false,
    });
    hmap.rifts(b, [4, 1.2], 30, 0.1, 11, {
      applyMask: true,
      reverseMask: true,
    });

    const da = a.data;
    const db = b.data;

    // zero region: untouched by the plain call, deformed by the reversed one
    for (let j = 0; j < 16; j++)
      for (let i = 0; i < 8; i++) {
        expect(da[j * 16 + i]).toBe(0);
        expect(db[j * 16 + i]).not.toBe(0);
      }

    // one region: deformed by the plain call, untouched by the reversed one
    for (let j = 0; j < 16; j++)
      for (let i = 8; i < 16; i++) {
        expect(da[j * 16 + i]).not.toBe(1);
        expect(db[j * 16 + i]).toBe(1);
      }
  });

  it("rejects freed HArray inputs", () => {
    const g = terrain();
    g.free();

    expect(() => hmap.rifts(g, [4, 1.2], 30, 0.1, 11)).toThrow(/freed/);
  });
});

describe("advectionParticle", () => {
  it("returns a new grid and leaves both inputs untouched", () => {
    const z = terrain(21);
    const f = field(23);
    const zBefore = values(z);
    const fBefore = values(f);

    const out = hmap.advectionParticle(z, f, 64, 11);

    expect(out).not.toBe(z);
    expect(out).not.toBe(f);
    expect(out.shape).toEqual([16, 16]);
    expect(values(z)).toEqual(zBefore);
    expect(values(f)).toEqual(fBefore);
  });

  it("plain inputs leave the caller's data untouched", () => {
    const sourceZ = terrain(21);
    const sourceF = field(23);
    const zData = Float32Array.from(sourceZ.data);
    const fData = Float32Array.from(sourceF.data);
    const zOriginal = Float32Array.from(zData);
    const fOriginal = Float32Array.from(fData);

    const out = hmap.advectionParticle(
      { data: zData, shape: [16, 16] },
      { data: fData, shape: [16, 16] },
      64,
      11,
    );

    expect(Array.from(zData)).toEqual(Array.from(zOriginal));
    expect(Array.from(fData)).toEqual(Array.from(fOriginal));
    expect(out.shape).toEqual([16, 16]);
  });

  it("requires nparticles and seed", () => {
    const z = terrain();
    const f = field();

    expect(() =>
      hmap.advectionParticle(z, f, undefined as unknown as number, 11),
    ).toThrow(/nparticles/);
    expect(() =>
      hmap.advectionParticle(z, f, 64, undefined as unknown as number),
    ).toThrow(/seed/);
  });

  it("omitted options equal every explicit default", () => {
    const a = hmap.advectionParticle(terrain(25), field(27), 64, 11);
    const b = hmap.advectionParticle(terrain(25), field(27), 64, 11, {
      iterations: 1,
      reverse: false,
      postFilter: true,
      postFilterSigma: 0.125,
      advectionLength: 0.1,
      valuePersistence: 0.99,
      inertia: 0,
    });

    expect(values(a)).toEqual(values(b));
  });

  it("rejects a field whose shape differs from the terrain", () => {
    expect(() =>
      hmap.advectionParticle(terrain(), hmap.makeGrid(8, 8), 16, 11),
    ).toThrow(/field/);
  });

  it("rejects freed HArray inputs", () => {
    const z = terrain();
    z.free();
    expect(() => hmap.advectionParticle(z, field(), 16, 11)).toThrow(/grid/);

    const f = field();
    f.free();
    expect(() => hmap.advectionParticle(terrain(), f, 16, 11)).toThrow(/field/);
  });

  it("is deterministic and finite", () => {
    const a = hmap.advectionParticle(terrain(29), field(31), 128, 11);
    const b = hmap.advectionParticle(terrain(29), field(31), 128, 11);

    expect(values(a)).toEqual(values(b));
    for (const v of a.data) expect(Number.isFinite(v)).toBe(true);
  });

  it("iterations: 0 returns the field", () => {
    const f = field(33);
    const out = hmap.advectionParticle(terrain(35), f, 64, 11, {
      iterations: 0,
    });

    expect(values(out)).toEqual(values(f));
  });

  it("iterations: 3 equals three chained iterations: 1 calls", () => {
    const z = terrain(37);
    const f = field(39);

    const triple = hmap.advectionParticle(z, f, 64, 11, { iterations: 3 });
    let chained = hmap.advectionParticle(z, f, 64, 11);
    chained = hmap.advectionParticle(z, chained, 64, 11);
    chained = hmap.advectionParticle(z, chained, 64, 11);

    expect(values(triple)).toEqual(values(chained));
  });
});

describe("advectionWarp", () => {
  it("returns a new grid and leaves both inputs untouched", () => {
    const z = terrain(41);
    const f = field(43);
    const zBefore = values(z);
    const fBefore = values(f);

    const out = hmap.advectionWarp(z, f);

    expect(out).not.toBe(z);
    expect(out).not.toBe(f);
    expect(out.shape).toEqual([16, 16]);
    expect(values(z)).toEqual(zBefore);
    expect(values(f)).toEqual(fBefore);
  });

  it("plain inputs leave the caller's data untouched", () => {
    const sourceZ = terrain(41);
    const sourceF = field(43);
    const zData = Float32Array.from(sourceZ.data);
    const fData = Float32Array.from(sourceF.data);
    const zOriginal = Float32Array.from(zData);
    const fOriginal = Float32Array.from(fData);

    const out = hmap.advectionWarp(
      { data: zData, shape: [16, 16] },
      { data: fData, shape: [16, 16] },
    );

    expect(Array.from(zData)).toEqual(Array.from(zOriginal));
    expect(Array.from(fData)).toEqual(Array.from(fOriginal));
    expect(out.shape).toEqual([16, 16]);
  });

  it("omitted options equal every explicit default", () => {
    const a = hmap.advectionWarp(terrain(45), field(47));
    const b = hmap.advectionWarp(terrain(45), field(47), {
      advectionLength: 0.1,
      valuePersistence: 0.9,
    });

    expect(values(a)).toEqual(values(b));
  });

  it("rejects a field whose shape differs from the terrain", () => {
    expect(() =>
      hmap.advectionWarp(terrain(), hmap.makeGrid(8, 8)),
    ).toThrow(/field/);
  });

  it("rejects freed HArray inputs", () => {
    const z = terrain();
    z.free();
    expect(() => hmap.advectionWarp(z, field())).toThrow(/grid/);

    const f = field();
    f.free();
    expect(() => hmap.advectionWarp(terrain(), f)).toThrow(/field/);
  });

  it("is deterministic and finite", () => {
    const a = hmap.advectionWarp(terrain(49), field(51));
    const b = hmap.advectionWarp(terrain(49), field(51));

    expect(values(a)).toEqual(values(b));
    for (const v of a.data) expect(Number.isFinite(v)).toBe(true);
  });

  it("zero advection length returns an all-zero grid", () => {
    const f = field(53);
    const out = hmap.advectionWarp(terrain(55), f, { advectionLength: 0 });

    for (const v of out.data) expect(v).toBe(0);
    expect(values(out)).not.toEqual(values(f));
  });
});

describe("deferred overloads", () => {
  it("names the unbound inputs in the manifest prose", () => {
    const section =
      manifest
        .split(/^### /m)
        .find((s) => s.startsWith("Rifts / advection family")) ?? "";

    expect(section).toContain("p_noise_x");
    expect(section).toContain("p_noise_y");
    expect(section).toContain("p_mask");
    expect(section).toContain("p_advection_mask");
    expect(section).toContain("multi-field");
    expect(section).toContain("explicit `dx`/`dy`");
  });

  it("ignores mask, noise-offset and multi-field option keys", () => {
    const mask = hmap.makeGrid(16, 16, 1);

    const riftsDefaults = terrain(57);
    const riftsExtras = terrain(57);
    hmap.rifts(riftsDefaults, [4, 1.2], 30, 0.1, 11);
    hmap.rifts(riftsExtras, [4, 1.2], 30, 0.1, 11, {
      p_mask: mask,
      p_noise_x: mask,
      p_noise_y: mask,
    } as unknown as RiftsOptions);
    expect(values(riftsExtras)).toEqual(values(riftsDefaults));

    const particleDefaults = hmap.advectionParticle(
      terrain(59),
      field(61),
      64,
      11,
    );
    const particleExtras = hmap.advectionParticle(
      terrain(59),
      field(61),
      64,
      11,
      {
        p_advection_mask: mask,
        p_mask: mask,
        fields: [mask],
        dx: mask,
        dy: mask,
      } as unknown as AdvectionParticleOptions,
    );
    expect(values(particleExtras)).toEqual(values(particleDefaults));

    const warpDefaults = hmap.advectionWarp(terrain(63), field(65));
    const warpExtras = hmap.advectionWarp(terrain(63), field(65), {
      p_mask: mask,
      dx: mask,
      dy: mask,
    } as unknown as AdvectionWarpOptions);
    expect(values(warpExtras)).toEqual(values(warpDefaults));
  });

  it("rejects an array of fields", () => {
    expect(() =>
      hmap.advectionParticle(
        terrain(),
        [field(), field()] as unknown as HArray,
        64,
        11,
      ),
    ).toThrow(/field/);
  });
});
