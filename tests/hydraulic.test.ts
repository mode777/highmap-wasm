import { beforeAll, describe, expect, it } from "vitest";
import { HArray, NoiseType, initHighmap } from "../src/index.ts";

/**
 * Hydraulic erosion surface (add-hydraulic-cpu-ports). The algorithms are CPU
 * ports of upstream OpenCL kernels; their numerical agreement with the GPU
 * family is verified on the desktop build (tests/src/test_hydraulic_cpu.cpp)
 * because this Node environment has no OpenCL device. What is covered here is
 * everything the JS user can observe: in-place semantics, auxiliary/output-grid
 * handling, option defaults, validation and determinism.
 */

let hmap: Awaited<ReturnType<typeof initHighmap>>;

beforeAll(async () => {
  hmap = await initHighmap();
});

/** A remapped fBm grid in [0, 1]; the hydraulic models expect bounded terrain. */
const grid = (seed = 3, shape: [number, number] = [32, 32]): HArray => {
  const g = hmap.noiseFbm(NoiseType.PERLIN, shape, [4, 2], seed);
  hmap.remap(g, { vmin: 0, vmax: 1 });
  return g;
};

const values = (g: HArray) => Array.from(g.data);

/** Small, fast McDonald parameters for unit tests. */
const mcdonaldFast = { samples: 64, maxage: 16 } as const;

describe("hydraulicMcdonald", () => {
  it("erodes an HArray in place and returns the same wrapper", () => {
    const g = grid();
    const before = values(g);

    const out = hmap.hydraulicMcdonald(g, 2, 7, mcdonaldFast);

    expect(out).toBe(g);
    expect(out.shape).toEqual([32, 32]);
    expect(values(out)).not.toEqual(before);
  });

  it("plain grid input leaves the caller's data untouched", () => {
    const source = grid();
    const data = Float32Array.from(source.data);
    const original = Float32Array.from(data);

    const out = hmap.hydraulicMcdonald(
      { data, shape: [32, 32] },
      2,
      7,
      mcdonaldFast,
    );

    expect(Array.from(data)).toEqual(Array.from(original));
    expect(out.shape).toEqual([32, 32]);
    expect(values(out)).not.toEqual(Array.from(original));
  });

  it("requires steps and seed", () => {
    const g = grid();

    expect(() =>
      hmap.hydraulicMcdonald(g, undefined as unknown as number, 1),
    ).toThrow(/steps/);
    expect(() =>
      hmap.hydraulicMcdonald(g, 1, undefined as unknown as number),
    ).toThrow(/seed/);
  });

  it("omitted options equal explicit defaults", () => {
    const a = grid(5);
    const b = grid(5);

    hmap.hydraulicMcdonald(a, 1, 7, { samples: 32, maxage: 8 });
    hmap.hydraulicMcdonald(b, 1, 7, {
      samples: 32,
      maxage: 8,
      worldExtentKm: 40,
      zScaleKm: 4,
      lrate: 0.2,
      timeStep: 10,
      rainfall: 1,
      evapRate: 1e-4,
      gravity: 9.81,
      viscosity: 0.025,
      bedShear: 0.01,
      critSlope: 0.57,
      settleRate: 0.1,
      thermalRate: 2.5e-3,
      depositionRate: 5e-3,
      suspensionRate: 2.5e-4,
      exitSlope: 0.01,
    });

    expect(values(a)).toEqual(values(b));
  });

  it("fills sediment and discharge output maps", () => {
    const g = grid();
    const sediment = hmap.makeGrid(32, 32);
    const discharge = hmap.makeGrid(32, 32);

    const out = hmap.hydraulicMcdonald(g, 2, 7, {
      ...mcdonaldFast,
      sediment,
      discharge,
    });

    expect(out).toBe(g);
    expect(sediment.shape).toEqual([32, 32]);
    expect(discharge.shape).toEqual([32, 32]);
    for (const v of sediment.data) expect(Number.isFinite(v)).toBe(true);
    for (const v of discharge.data) {
      expect(Number.isFinite(v)).toBe(true);
      expect(v).toBeGreaterThanOrEqual(0);
    }
  });

  it("omitted output maps produce the same terrain as requested maps", () => {
    const a = grid(9);
    const b = grid(9);

    hmap.hydraulicMcdonald(a, 1, 7, { samples: 32, maxage: 8 });
    const sediment = hmap.makeGrid(32, 32);
    const discharge = hmap.makeGrid(32, 32);
    hmap.hydraulicMcdonald(b, 1, 7, {
      samples: 32,
      maxage: 8,
      sediment,
      discharge,
    });

    expect(values(a)).toEqual(values(b));
  });

  it("rejects an output map that aliases the terrain grid", () => {
    const g = grid();

    expect(() =>
      hmap.hydraulicMcdonald(g, 1, 7, { ...mcdonaldFast, sediment: g }),
    ).toThrow(/sediment/);
  });

  it("rejects an output map with a mismatched shape", () => {
    const g = grid();

    expect(() =>
      hmap.hydraulicMcdonald(g, 1, 7, {
        ...mcdonaldFast,
        sediment: hmap.makeGrid(8, 8),
      }),
    ).toThrow(/sediment/);
  });

  it("rejects two outputs that alias each other", () => {
    const g = grid();
    const out = hmap.makeGrid(32, 32);

    expect(() =>
      hmap.hydraulicMcdonald(g, 1, 7, {
        ...mcdonaldFast,
        sediment: out,
        discharge: out,
      }),
    ).toThrow(/discharge/);
  });

  it("is deterministic and finite", () => {
    const a = grid(11);
    const b = grid(11);

    hmap.hydraulicMcdonald(a, 2, 7, mcdonaldFast);
    hmap.hydraulicMcdonald(b, 2, 7, mcdonaldFast);

    expect(values(a)).toEqual(values(b));
    for (const v of a.data) expect(Number.isFinite(v)).toBe(true);
  });
});

describe("hydraulicMcdonaldMultiscale", () => {
  it("erodes an HArray in place and is deterministic", () => {
    const a = grid(13, [64, 64]);
    const b = grid(13, [64, 64]);
    const options = {
      stepsPerLevel: [4, 2],
      samples: 32,
      maxage: 8,
    } as const;

    const out = hmap.hydraulicMcdonaldMultiscale(a, 5, options);
    hmap.hydraulicMcdonaldMultiscale(b, 5, options);

    expect(out).toBe(a);
    expect(values(a)).toEqual(values(b));
    for (const v of a.data) expect(Number.isFinite(v)).toBe(true);
  });

  it("fills sediment and discharge output maps", () => {
    const g = grid(13, [32, 32]);
    const sediment = hmap.makeGrid(32, 32);
    const discharge = hmap.makeGrid(32, 32);

    hmap.hydraulicMcdonaldMultiscale(g, 5, {
      stepsPerLevel: [2, 1],
      samples: 32,
      maxage: 8,
      sediment,
      discharge,
    });

    expect(sediment.shape).toEqual([32, 32]);
    expect(discharge.shape).toEqual([32, 32]);
    for (const v of sediment.data) expect(Number.isFinite(v)).toBe(true);
    for (const v of discharge.data) expect(Number.isFinite(v)).toBe(true);
  });

  it("requires seed", () => {
    const g = grid();

    expect(() =>
      hmap.hydraulicMcdonaldMultiscale(
        g,
        undefined as unknown as number,
        { stepsPerLevel: [1] },
      ),
    ).toThrow(/seed/);
  });

  it("rejects a non-integer stepsPerLevel", () => {
    const g = grid();

    expect(() =>
      hmap.hydraulicMcdonaldMultiscale(g, 5, {
        stepsPerLevel: [1.5],
      }),
    ).toThrow(/stepsPerLevel/);
  });
});

describe("hydraulicSchott", () => {
  it("erodes an HArray in place and returns the same wrapper", () => {
    const g = grid(17, [16, 16]);
    const before = values(g);

    const out = hmap.hydraulicSchott(g, 0.01, { iterations: 2 });

    expect(out).toBe(g);
    expect(out.shape).toEqual([16, 16]);
    expect(values(out)).not.toEqual(before);
  });

  it("plain grid input leaves the caller's data untouched", () => {
    const source = grid(17, [16, 16]);
    const data = Float32Array.from(source.data);
    const original = Float32Array.from(data);

    const out = hmap.hydraulicSchott(
      { data, shape: [16, 16] },
      0.01,
      { iterations: 2 },
    );

    expect(Array.from(data)).toEqual(Array.from(original));
    expect(out.shape).toEqual([16, 16]);
    expect(values(out)).not.toEqual(Array.from(original));
  });

  it("requires talus", () => {
    const g = grid(17, [16, 16]);

    expect(() =>
      hmap.hydraulicSchott(g, undefined as unknown as number),
    ).toThrow(/talus/);
  });

  it("scalar talus equals a constant talus grid", () => {
    const a = grid(17, [16, 16]);
    const b = grid(17, [16, 16]);

    hmap.hydraulicSchott(a, 0.01, { iterations: 2 });
    hmap.hydraulicSchott(b, hmap.makeGrid(16, 16, 0.01), { iterations: 2 });

    expect(values(a)).toEqual(values(b));
  });

  it("omitted options equal explicit defaults", () => {
    const a = grid(19, [16, 16]);
    const b = grid(19, [16, 16]);

    hmap.hydraulicSchott(a, 0.01);
    hmap.hydraulicSchott(b, 0.01, {
      iterations: 10,
      cErosion: 1,
      cThermal: 0.1,
      cDeposition: 0.2,
      flowAccExponent: 0.8,
      flowAccExponentDepo: 0.8,
      flowRoutingExponent: 1.3,
      thermalWeight: 1.5,
      depositionWeight: 2.5,
    });

    expect(values(a)).toEqual(values(b));
  });

  it("fills the flow output map", () => {
    const g = grid(17, [16, 16]);
    const flow = hmap.makeGrid(16, 16);

    hmap.hydraulicSchott(g, 0.01, { iterations: 2, flow });

    expect(flow.shape).toEqual([16, 16]);
    for (const v of flow.data) {
      expect(Number.isFinite(v)).toBe(true);
      expect(v).toBeGreaterThanOrEqual(0);
    }
  });

  it("zero iterations leaves the terrain and input flow unchanged", () => {
    const g = grid(17, [16, 16]);
    const before = values(g);
    const flow = hmap.makeGrid(16, 16, 1);

    hmap.hydraulicSchott(g, 0.01, { iterations: 0, flow });

    expect(values(g)).toEqual(before);
    for (const v of flow.data) expect(v).toBe(1);
  });

  it("rejects a talus grid whose shape differs from the grid", () => {
    const g = grid(17, [16, 16]);

    expect(() => hmap.hydraulicSchott(g, hmap.makeGrid(8, 8, 0.01))).toThrow(
      /talus/,
    );
  });

  it("rejects a flow output that aliases the talus input", () => {
    const g = grid(17, [16, 16]);
    const talus = hmap.makeGrid(16, 16, 0.01);

    expect(() =>
      hmap.hydraulicSchott(g, talus, { iterations: 1, flow: talus }),
    ).toThrow(/flow/);
  });

  it("is deterministic and finite", () => {
    const a = grid(23, [16, 16]);
    const b = grid(23, [16, 16]);

    hmap.hydraulicSchott(a, 0.01, { iterations: 3 });
    hmap.hydraulicSchott(b, 0.01, { iterations: 3 });

    expect(values(a)).toEqual(values(b));
    for (const v of a.data) expect(Number.isFinite(v)).toBe(true);
  });
});

describe("hydraulicSchottErosion", () => {
  it("erodes an HArray in place and returns the same wrapper", () => {
    const g = grid(29, [16, 16]);
    const before = values(g);

    const out = hmap.hydraulicSchottErosion(g, { iterations: 2 });

    expect(out).toBe(g);
    expect(out.shape).toEqual([16, 16]);
    expect(values(out)).not.toEqual(before);
  });

  it("plain grid input leaves the caller's data untouched", () => {
    const source = grid(29, [16, 16]);
    const data = Float32Array.from(source.data);
    const original = Float32Array.from(data);

    const out = hmap.hydraulicSchottErosion(
      { data, shape: [16, 16] },
      { iterations: 2 },
    );

    expect(Array.from(data)).toEqual(Array.from(original));
    expect(out.shape).toEqual([16, 16]);
  });

  it("omitted options equal explicit defaults", () => {
    const a = grid(31, [16, 16]);
    const b = grid(31, [16, 16]);

    hmap.hydraulicSchottErosion(a);
    hmap.hydraulicSchottErosion(b, {
      iterations: 10,
      cErosion: 1,
      flowAccExponent: 0.8,
      flowRoutingExponent: 1.3,
    });

    expect(values(a)).toEqual(values(b));
  });

  it("fills the flow output map", () => {
    const g = grid(29, [16, 16]);
    const flow = hmap.makeGrid(16, 16);

    hmap.hydraulicSchottErosion(g, { iterations: 2, flow });

    expect(flow.shape).toEqual([16, 16]);
    for (const v of flow.data) expect(Number.isFinite(v)).toBe(true);
  });

  it("zero iterations leaves the terrain unchanged", () => {
    const g = grid(29, [16, 16]);
    const before = values(g);

    hmap.hydraulicSchottErosion(g, { iterations: 0 });

    expect(values(g)).toEqual(before);
  });

  it("rejects a flow output that aliases the terrain grid", () => {
    const g = grid(29, [16, 16]);

    expect(() =>
      hmap.hydraulicSchottErosion(g, { iterations: 1, flow: g }),
    ).toThrow(/flow/);
  });

  it("is deterministic and finite", () => {
    const a = grid(37, [16, 16]);
    const b = grid(37, [16, 16]);

    hmap.hydraulicSchottErosion(a, { iterations: 3 });
    hmap.hydraulicSchottErosion(b, { iterations: 3 });

    expect(values(a)).toEqual(values(b));
    for (const v of a.data) expect(Number.isFinite(v)).toBe(true);
  });
});

describe("hydraulicVpipes", () => {
  it("erodes an HArray in place and returns the same wrapper", () => {
    const g = grid(41, [16, 16]);
    const before = values(g);

    const out = hmap.hydraulicVpipes(g, { iterations: 2 });

    expect(out).toBe(g);
    expect(out.shape).toEqual([16, 16]);
    expect(values(out)).not.toEqual(before);
  });

  it("plain grid input leaves the caller's data untouched", () => {
    const source = grid(41, [16, 16]);
    const data = Float32Array.from(source.data);
    const original = Float32Array.from(data);

    const out = hmap.hydraulicVpipes(
      { data, shape: [16, 16] },
      { iterations: 2 },
    );

    expect(Array.from(data)).toEqual(Array.from(original));
    expect(out.shape).toEqual([16, 16]);
    expect(values(out)).not.toEqual(Array.from(original));
  });

  it("omitted options equal explicit defaults", () => {
    const a = grid(43, [16, 16]);
    const b = grid(43, [16, 16]);

    hmap.hydraulicVpipes(a);
    hmap.hydraulicVpipes(b, {
      waterHeight: 1e-2,
      maintainWaterVolume: true,
      evapRate: 0.1,
      iterations: 50,
      dt: 0.5,
      kCapacity: 0.5,
      kErode: 0.001,
      kDepose: 0.01,
      kDischargeExp: 1,
      downcuttingMaxDepthRatio: 10,
      fluxDiffusion: true,
      fluxDiffusionStrength: 0.01,
    });

    expect(values(a)).toEqual(values(b));
  });

  it("fills the water depth, sediment and velocity output maps", () => {
    const g = grid(41, [16, 16]);
    const waterDepth = hmap.makeGrid(16, 16);
    const sediment = hmap.makeGrid(16, 16);
    const velU = hmap.makeGrid(16, 16);
    const velV = hmap.makeGrid(16, 16);

    hmap.hydraulicVpipes(g, {
      iterations: 2,
      waterDepth,
      sediment,
      velU,
      velV,
    });

    for (const map of [waterDepth, sediment, velU, velV]) {
      expect(map.shape).toEqual([16, 16]);
      for (const v of map.data) expect(Number.isFinite(v)).toBe(true);
    }
    for (const v of waterDepth.data) expect(v).toBeGreaterThanOrEqual(0);
  });

  it("zero iterations leaves the terrain unchanged and initializes outputs", () => {
    const g = grid(41, [16, 16]);
    const before = values(g);
    const waterDepth = hmap.makeGrid(16, 16);
    const sediment = hmap.makeGrid(16, 16);
    const velU = hmap.makeGrid(16, 16);
    const velV = hmap.makeGrid(16, 16);

    hmap.hydraulicVpipes(g, {
      iterations: 0,
      waterDepth,
      sediment,
      velU,
      velV,
    });

    expect(values(g)).toEqual(before);
    for (const v of waterDepth.data) expect(v).toBeCloseTo(1e-2, 6);
    for (const v of sediment.data) expect(v).toBe(0);
    for (const v of velU.data) expect(v).toBe(0);
    for (const v of velV.data) expect(v).toBe(0);
  });

  it("rejects an output map with a mismatched shape", () => {
    const g = grid(41, [16, 16]);

    expect(() =>
      hmap.hydraulicVpipes(g, {
        iterations: 1,
        waterDepth: hmap.makeGrid(8, 8),
      }),
    ).toThrow(/waterDepth/);
  });

  it("rejects an output map that aliases the terrain grid", () => {
    const g = grid(41, [16, 16]);

    expect(() =>
      hmap.hydraulicVpipes(g, { iterations: 1, waterDepth: g }),
    ).toThrow(/waterDepth/);
  });

  it("rejects two outputs that alias each other", () => {
    const g = grid(41, [16, 16]);
    const out = hmap.makeGrid(16, 16);

    expect(() =>
      hmap.hydraulicVpipes(g, {
        iterations: 1,
        sediment: out,
        velU: out,
      }),
    ).toThrow(/velU/);
  });

  it("flux diffusion changes the result", () => {
    const a = grid(47, [16, 16]);
    const b = grid(47, [16, 16]);

    hmap.hydraulicVpipes(a, { iterations: 3, fluxDiffusion: true });
    hmap.hydraulicVpipes(b, { iterations: 3, fluxDiffusion: false });

    expect(values(a)).not.toEqual(values(b));
  });

  it("is deterministic and finite", () => {
    const a = grid(53, [16, 16]);
    const b = grid(53, [16, 16]);

    hmap.hydraulicVpipes(a, { iterations: 3 });
    hmap.hydraulicVpipes(b, { iterations: 3 });

    expect(values(a)).toEqual(values(b));
    for (const v of a.data) expect(Number.isFinite(v)).toBe(true);
  });
});
