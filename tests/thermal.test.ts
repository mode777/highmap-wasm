import { beforeAll, describe, expect, it } from "vitest";
import { HArray, NoiseType, initHighmap } from "../src/index.ts";

/**
 * Thermal erosion surface (add-thermal-cpu-port). The algorithms are CPU ports
 * of upstream OpenCL kernels; their numerical agreement with the GPU family is
 * verified on the desktop build (tests/src/test_thermal_cpu.cpp) because this
 * Node environment has no OpenCL device. What is covered here is everything the
 * JS user can observe: in-place semantics, auxiliary-grid handling, option
 * defaults, validation and determinism.
 */

let hmap: Awaited<ReturnType<typeof initHighmap>>;

beforeAll(async () => {
  hmap = await initHighmap();
});

/** A remapped fBm grid in [0, 1]; `thermal` expects a bounded heightmap. */
const grid = (seed = 3, shape: [number, number] = [32, 32]): HArray => {
  const g = hmap.noiseFbm(NoiseType.PERLIN, shape, [4, 2], seed);
  hmap.remap(g, { vmin: 0, vmax: 1 });
  return g;
};

const values = (g: HArray) => Array.from(g.data);

/** Sum of absolute differences between horizontal and vertical neighbours. */
const totalVariation = (g: HArray): number => {
  const [ni, nj] = g.shape;
  const d = g.data;
  let sum = 0;
  for (let j = 0; j < nj; j++)
    for (let i = 0; i < ni; i++) {
      const k = j * ni + i;
      if (i + 1 < ni) sum += Math.abs(d[k + 1]! - d[k]!);
      if (j + 1 < nj) sum += Math.abs(d[k + ni]! - d[k]!);
    }
  return sum;
};

describe("thermal", () => {
  it("erodes an HArray in place and returns the same wrapper", () => {
    const g = grid();
    const before = values(g);

    const out = hmap.thermal(g, 0.01);

    expect(out).toBe(g);
    expect(out.shape).toEqual([32, 32]);
    expect(values(out)).not.toEqual(before);
  });

  it("plain grid input leaves the caller's data untouched", () => {
    const source = grid();
    const data = Float32Array.from(source.data);
    const original = Float32Array.from(data);

    const out = hmap.thermal({ data, shape: [32, 32] }, 0.01);

    expect(Array.from(data)).toEqual(Array.from(original));
    expect(out.shape).toEqual([32, 32]);
    expect(values(out)).not.toEqual(Array.from(original));
  });

  it("scalar talus equals a constant talus grid", () => {
    const a = grid();
    const b = grid();

    hmap.thermal(a, 0.01, { iterations: 3 });
    hmap.thermal(b, hmap.makeGrid(32, 32, 0.01), { iterations: 3 });

    expect(values(a)).toEqual(values(b));
  });

  it("omitted options equal explicit defaults", () => {
    const a = grid();
    const b = grid();

    hmap.thermal(a, 0.01);
    hmap.thermal(b, 0.01, { iterations: 10 });

    expect(values(a)).toEqual(values(b));
  });

  it("iterations: 0 leaves the interior untouched", () => {
    const g = grid(5, [16, 16]);
    const before = Float32Array.from(g.data);

    hmap.thermal(g, 0.01, { iterations: 0 });

    const [ni, nj] = g.shape;
    for (let j = 1; j < nj - 1; j++)
      for (let i = 1; i < ni - 1; i++)
        expect(g.data[j * ni + i]).toBe(before[j * ni + i]);

    // the one-cell border is still extrapolated
    let borderChanged = false;
    for (let i = 0; i < ni; i++) {
      if (g.data[i] !== before[i]) borderChanged = true;
      const k = (nj - 1) * ni + i;
      if (g.data[k] !== before[k]) borderChanged = true;
    }
    for (let j = 0; j < nj; j++) {
      const k = j * ni;
      if (g.data[k] !== before[k]) borderChanged = true;
      if (g.data[k + ni - 1] !== before[k + ni - 1]) borderChanged = true;
    }
    expect(borderChanged).toBe(true);
  });

  it("rejects a talus grid whose shape differs from the grid", () => {
    const g = grid();

    expect(() => hmap.thermal(g, hmap.makeGrid(8, 8, 0.01))).toThrow(/talus/);
  });

  it("is deterministic and finite", () => {
    const a = grid(7);
    const b = grid(7);

    hmap.thermal(a, 0.01, { iterations: 5 });
    hmap.thermal(b, 0.01, { iterations: 5 });

    expect(values(a)).toEqual(values(b));
    for (const v of a.data) expect(Number.isFinite(v)).toBe(true);
  });

  it("reduces total variation (talus-capped smoothing)", () => {
    const g = grid(9);
    const tv0 = totalVariation(g);

    hmap.thermal(g, 0.01, { iterations: 10 });

    expect(totalVariation(g)).toBeLessThan(tv0);
  });
});

describe("thermalAutoBedrock", () => {
  it("never lowers an interior cell below its starting elevation", () => {
    const g = grid(13, [24, 24]);
    const before = Float32Array.from(g.data);

    hmap.thermalAutoBedrock(g, 0.01, { iterations: 5 });

    const [ni, nj] = g.shape;
    for (let j = 1; j < nj - 1; j++)
      for (let i = 1; i < ni - 1; i++) {
        const k = j * ni + i;
        expect(g.data[k]).toBeGreaterThanOrEqual(before[k]!);
      }
  });

  it("accepts scalar and grid talus identically", () => {
    const a = grid(4);
    const b = grid(4);

    hmap.thermalAutoBedrock(a, 0.02, { iterations: 2 });
    hmap.thermalAutoBedrock(b, hmap.makeGrid(32, 32, 0.02), { iterations: 2 });

    expect(values(a)).toEqual(values(b));
  });

  it("is deterministic and honours the iterations default", () => {
    const a = grid(8);
    const b = grid(8);
    const c = grid(8);

    hmap.thermalAutoBedrock(a, 0.01);
    hmap.thermalAutoBedrock(b, 0.01, { iterations: 10 });
    hmap.thermalAutoBedrock(c, 0.01, { iterations: 3 });

    expect(values(a)).toEqual(values(b));
    expect(values(a)).not.toEqual(values(c));
  });
});

describe("thermalConserve", () => {
  it("leaves every border cell exactly unchanged", () => {
    const g = grid(17, [20, 20]);
    const before = Float32Array.from(g.data);

    hmap.thermalConserve(g, 0.01, { iterations: 5 });

    const [ni, nj] = g.shape;
    for (let i = 0; i < ni; i++) {
      expect(g.data[i]).toBe(before[i]);
      expect(g.data[(nj - 1) * ni + i]).toBe(before[(nj - 1) * ni + i]);
    }
    for (let j = 0; j < nj; j++) {
      expect(g.data[j * ni]).toBe(before[j * ni]);
      expect(g.data[j * ni + ni - 1]).toBe(before[j * ni + ni - 1]);
    }
  });

  it("conserves total elevation", () => {
    const g = grid(19, [24, 24]);
    const total = (d: Float32Array) => {
      let s = 0;
      for (const v of d) s += v;
      return s;
    };

    const before = total(Float32Array.from(g.data));
    hmap.thermalConserve(g, 0.01, { iterations: 5 });
    const after = total(g.data);

    expect(Math.abs(after - before)).toBeLessThan(1e-2);
  });

  it("rate 0 leaves the grid unchanged", () => {
    const g = grid(21, [16, 16]);
    const before = values(g);

    hmap.thermalConserve(g, 0.01, { iterations: 3, rate: 0 });

    expect(values(g)).toEqual(before);
  });

  it("rate 0.5 is the default and differs from rate 0", () => {
    const a = grid(23);
    const b = grid(23);

    hmap.thermalConserve(a, 0.01, { iterations: 3 });
    hmap.thermalConserve(b, 0.01, { iterations: 3, rate: 0.5 });

    expect(values(a)).toEqual(values(b));

    const c = grid(23);
    hmap.thermalConserve(c, 0.01, { iterations: 3, rate: 0.25 });
    expect(values(c)).not.toEqual(values(a));
  });
});

/** Uniform ramp in i, slope 0.1 per cell (ten times the talus used below). */
const ramp = (n = 16): HArray => {
  const g = hmap.makeGrid(n, n, 0);
  for (let j = 0; j < n; j++) for (let i = 0; i < n; i++) g.data[j * n + i] = 0.1 * i;
  return g;
};

describe("thermalFlatten", () => {
  it("sigmaSup: 0 leaves a steep uniform ramp's core untouched", () => {
    const g = ramp();
    const before = values(g);

    hmap.thermalFlatten(g, 0.01, {
      iterations: 1,
      sigmaInf: 0.5,
      sigmaSup: 0,
    });

    const [ni, nj] = g.shape;
    for (let j = 2; j < nj - 2; j++)
      for (let i = 2; i < ni - 2; i++)
        expect(g.data[j * ni + i]).toBe(before[j * ni + i]);
  });

  it("sigmaSup > 0 relaxes the same ramp", () => {
    const g = ramp();
    const before = values(g);

    hmap.thermalFlatten(g, 0.01, {
      iterations: 1,
      sigmaInf: 0,
      sigmaSup: 0.5,
    });

    const [ni, nj] = g.shape;
    let changed = false;
    for (let j = 2; j < nj - 2; j++)
      for (let i = 2; i < ni - 2; i++)
        if (g.data[j * ni + i] !== before[j * ni + i]) changed = true;
    expect(changed).toBe(true);
  });

  it("honours the sigma defaults and scalar talus", () => {
    const a = grid(31);
    const b = grid(31);
    const c = grid(31);

    hmap.thermalFlatten(a, 0.01, { iterations: 2 });
    hmap.thermalFlatten(b, 0.01, {
      iterations: 2,
      sigmaInf: 0.5,
      sigmaSup: 0,
    });
    hmap.thermalFlatten(c, hmap.makeGrid(32, 32, 0.01), { iterations: 2 });

    expect(values(a)).toEqual(values(b));
    expect(values(a)).toEqual(values(c));
  });
});

describe("thermalOlsen", () => {
  it("lowers a spike monotonically and is deterministic", () => {
    const build = () => {
      const g = hmap.makeGrid(24, 24, 0.2);
      g.data[12 * 24 + 12] = 1;
      return g;
    };

    const a = build();
    const b = build();

    hmap.thermalOlsen(a, 0.01, { iterations: 3 });
    hmap.thermalOlsen(b, 0.01, { iterations: 3 });

    expect(a.data[12 * 24 + 12]!).toBeLessThan(1);
    expect(values(a)).toEqual(values(b));
  });

  it("honours the iterations default", () => {
    const a = grid(37);
    const b = grid(37);

    hmap.thermalOlsen(a, 0.01);
    hmap.thermalOlsen(b, 0.01, { iterations: 10 });

    expect(values(a)).toEqual(values(b));
  });
});

describe("thermalRib", () => {
  it("never raises an interior cell", () => {
    const g = grid(41, [24, 24]);
    const before = Float32Array.from(g.data);

    hmap.thermalRib(g, { iterations: 2 });

    // thermal_rib finishes with extrapolate_borders(z, 3): the outer three
    // cells are rewritten by the extrapolation and may move either way.
    const [ni, nj] = g.shape;
    for (let j = 3; j < nj - 3; j++)
      for (let i = 3; i < ni - 3; i++) {
        const k = j * ni + i;
        expect(g.data[k]).toBeLessThanOrEqual(before[k]!);
      }
  });

  it("is deterministic and honours the iterations default", () => {
    const a = grid(43);
    const b = grid(43);
    const c = grid(43);

    hmap.thermalRib(a);
    hmap.thermalRib(b, { iterations: 10 });
    hmap.thermalRib(c, { iterations: 3 });

    expect(values(a)).toEqual(values(b));
    expect(values(a)).not.toEqual(values(c));
  });
});

/** Constant plateau with one modified centre cell. */
const plateauWithCentre = (n: number, centre: number): HArray => {
  const g = hmap.makeGrid(n, n, 0.5);
  g.data[(n / 2) * n + n / 2] = centre;
  return g;
};

describe("thermalInflate", () => {
  it("leaves a perfectly flat grid unchanged (no NaN from slope_max = 0)", () => {
    const g = hmap.makeGrid(16, 16, 0.5);
    const before = values(g);

    hmap.thermalInflate(g, 0.01, { iterations: 2 });

    expect(values(g)).toEqual(before);
    for (const v of g.data) expect(Number.isNaN(v)).toBe(false);
  });

  it("fills a pit", () => {
    const g = plateauWithCentre(24, 0);
    const centre = (g.shape[0] / 2) * g.shape[0] + g.shape[0] / 2;

    hmap.thermalInflate(g, 0.1, { iterations: 2 });

    expect(g.data[centre]!).toBeGreaterThan(0);
  });

  it("honours the iterations default and scalar talus", () => {
    const a = grid(47);
    const b = grid(47);
    const c = grid(47);

    hmap.thermalInflate(a, 0.01);
    hmap.thermalInflate(b, 0.01, { iterations: 10 });
    hmap.thermalInflate(c, hmap.makeGrid(32, 32, 0.01));

    expect(values(a)).toEqual(values(b));
    expect(values(a)).toEqual(values(c));
  });
});

describe("thermalRidge", () => {
  it("lowers a spike", () => {
    const g = plateauWithCentre(24, 1);
    const n = g.shape[0];
    const centre = (n / 2) * n + n / 2;

    hmap.thermalRidge(g, 0.1, { iterations: 2 });

    expect(g.data[centre]!).toBeLessThan(1);
  });

  it("is deterministic", () => {
    const a = grid(53);
    const b = grid(53);

    hmap.thermalRidge(a, 0.01, { iterations: 3 });
    hmap.thermalRidge(b, 0.01, { iterations: 3 });

    expect(values(a)).toEqual(values(b));
  });
});

describe("thermalScree", () => {
  it("limits its effect near the zmax ceiling", () => {
    const build = () => {
      const g = plateauWithCentre(24, 0);
      return g;
    };

    const low = build();
    const high = build();

    hmap.thermalScree(low, 0.1, 0.5, { iterations: 2 });
    hmap.thermalScree(high, 0.1, 10, { iterations: 2 });

    const n = low.shape[0];
    const centre = (n / 2) * n + n / 2;

    // the ceiling scales the amplitude down, so the effect differs
    expect(low.data[centre]).not.toBe(high.data[centre]);
  });

  it("keeps cells at the zmax ceiling untouched", () => {
    const g = plateauWithCentre(24, 0);
    const before = Float32Array.from(g.data);

    hmap.thermalScree(g, 0.1, 0.5, { iterations: 2 });

    const n = g.shape[0];
    // a plateau cell far from the pit sits exactly at zmax -> amp 0
    const k = 3 * n + 3;
    expect(g.data[k]).toBe(before[k]);
  });

  it("honours the iterations default and rejects a mismatched zmax", () => {
    const a = grid(59);
    const b = grid(59);

    hmap.thermalScree(a, 0.01, 1);
    hmap.thermalScree(b, 0.01, 1, { iterations: 10 });

    expect(values(a)).toEqual(values(b));
    expect(() => hmap.thermalScree(b, 0.01, hmap.makeGrid(8, 8, 1))).toThrow(
      /zmax/,
    );
  });
});

describe("thermalSchott", () => {
  it("lowers a spike and is deterministic", () => {
    const build = () => plateauWithCentre(24, 1);
    const a = build();
    const b = build();
    const n = a.shape[0];
    const centre = (n / 2) * n + n / 2;

    hmap.thermalSchott(a, 0.01, { iterations: 2 });
    hmap.thermalSchott(b, 0.01, { iterations: 2 });

    expect(a.data[centre]!).toBeLessThan(1);
    expect(values(a)).toEqual(values(b));
  });

  it("honours the intensity default", () => {
    const a = plateauWithCentre(24, 1);
    const b = plateauWithCentre(24, 1);

    hmap.thermalSchott(a, 0.01, { iterations: 2 });
    hmap.thermalSchott(b, 0.01, { iterations: 2, intensity: 0.2 });

    expect(values(a)).toEqual(values(b));
  });
});
