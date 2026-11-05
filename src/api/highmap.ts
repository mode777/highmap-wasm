import type {
  DistanceFunction,
  HighmapModule,
  NoiseType,
  RawArray,
} from "../loader/module.ts";
import { coerceGrid, type GridLike } from "./coerce.ts";
import { HighmapError } from "./errors.ts";
import { HArray } from "./harray.ts";

/** Accepted forms for enum arguments: string enum member, raw embind enum
 * value (`hmap.NoiseType.PERLIN`), or the underlying numeric value. */
export type EnumArg<E extends string> = E | number | { value: number };

function enumValue<E extends string>(
  table: Record<string, { value: number }> | undefined,
  value: EnumArg<E>,
  argName: string,
): { value: number } {
  if (typeof value === "number") return { value };
  if (typeof value === "string") {
    const entry = table?.[value];
    if (entry && typeof entry.value === "number") return entry;
    throw new HighmapError(`${argName}: unknown enum member "${value}"`);
  }
  if (value && typeof value === "object" && typeof value.value === "number") {
    return value;
  }
  throw new HighmapError(
    `${argName}: expected an enum member name, a raw enum value, or a number`,
  );
}

/** Defaults for the fractal noise family (pyHighMap binding defaults). */
export interface FractalOptions {
  octaves?: number;
  weight?: number;
  persistence?: number;
  lacunarity?: number;
}

/** Defaults for `noiseRidged`, on top of the fractal family. */
export interface RidgedOptions extends FractalOptions {
  kSmoothing?: number;
}

export interface ClampOptions {
  vmin?: number;
  vmax?: number;
}

export interface FalloffOptions {
  strength?: number;
  distFct?: EnumArg<DistanceFunction>;
}

export interface SmoothFillOptions {
  k?: number;
}

export interface HillshadeOptions {
  talusRef?: number;
}

export interface HydraulicMusgraveOptions {
  iterations?: number;
  cCapacity?: number;
  cErosion?: number;
  cDeposition?: number;
  waterLevel?: number;
  evapRate?: number;
}

export interface HydraulicStreamOptions {
  ir?: number;
  clippingRatio?: number;
}

/**
 * The high-level API surface (grows per the binding manifest, AD-008).
 *
 * Semantics: functions that mutate the grid in C++ accept either input form;
 * `HArray` inputs are modified **in place** and returned (zero-copy
 * pipelines), plain `{ data, shape }` inputs are copied first and a new
 * `HArray` is returned. Functions that produce a new array in C++
 * (`hillshade`, the algebra layer) always return a fresh `HArray`.
 */
export interface Highmap {
  /** Enum constants (bound 1:1 to the C++ enums). */
  readonly NoiseType: Record<NoiseType, { value: number }>;
  readonly DistanceFunction: Record<DistanceFunction, { value: number }>;
  /** Grid constructor: an `HArray` filled with `value` (default 0). */
  makeGrid(ni: number, nj: number, value?: number): HArray;

  // --- noise family ---------------------------------------------------------

  noise(
    noiseType: EnumArg<NoiseType>,
    shape: readonly [number, number],
    kw: readonly [number, number],
    seed: number,
  ): HArray;

  noiseFbm(
    noiseType: EnumArg<NoiseType>,
    shape: readonly [number, number],
    kw: readonly [number, number],
    seed: number,
    options?: FractalOptions,
  ): HArray;

  noisePingpong(
    noiseType: EnumArg<NoiseType>,
    shape: readonly [number, number],
    kw: readonly [number, number],
    seed: number,
    options?: FractalOptions,
  ): HArray;

  noiseRidged(
    noiseType: EnumArg<NoiseType>,
    shape: readonly [number, number],
    kw: readonly [number, number],
    seed: number,
    options?: RidgedOptions,
  ): HArray;

  // --- value range / filters (in place for HArray inputs) ---------------------

  clamp(grid: GridLike, options?: ClampOptions): HArray;
  remap(grid: GridLike, options?: ClampOptions): HArray;
  falloff(grid: GridLike, options?: FalloffOptions): HArray;
  plateau(grid: GridLike, ir: number, factor: number): HArray;
  smoothFill(grid: GridLike, ir: number, options?: SmoothFillOptions): HArray;

  // --- shading (returns a new grid) --------------------------------------------

  hillshade(
    grid: GridLike,
    azimuth: number,
    zenith: number,
    options?: HillshadeOptions,
  ): HArray;

  // --- transforms (in place for HArray inputs) -----------------------------------

  warp(grid: GridLike, dx: GridLike, dy: GridLike): HArray;

  // --- algebra layer (always new grids) -------------------------------------------

  add(a: GridLike, b: GridLike | number): HArray;
  subtract(a: GridLike, b: GridLike | number): HArray;
  multiply(a: GridLike, b: GridLike | number): HArray;
  divide(a: GridLike, b: GridLike | number): HArray;
  scale(grid: GridLike, factor: number): HArray;

  // --- erosion (in place for HArray inputs) -----------------------------------------

  hydraulicMusgrave(
    grid: GridLike,
    options?: HydraulicMusgraveOptions,
  ): HArray;

  hydraulicStream(
    grid: GridLike,
    cErosion: number,
    talusRef: number,
    options?: HydraulicStreamOptions,
  ): HArray;
}

const FRACTAL_DEFAULTS = {
  octaves: 8,
  weight: 0.7,
  persistence: 0.5,
  lacunarity: 2,
} as const;

export function createApi(mod: HighmapModule): Highmap {
  // The registry releases wasm memory for wrappers garbage collected without
  // an explicit free() (AD-001 safety net).
  const registry = new FinalizationRegistry<RawArray>((raw) => {
    raw.delete();
  });

  const wrap = (raw: RawArray): HArray => HArray.wrap(raw, mod, registry);

  /** Run an in-place C++ op; HArray inputs pass through, plain ones copy in. */
  const inplace = (
    grid: GridLike,
    argName: string,
    run: (raw: RawArray) => void,
  ): HArray => {
    const { raw, source } = coerceGrid(grid, argName, mod);
    run(raw);
    return source ?? wrap(raw);
  };

  const fractalArgs = (
    noiseType: EnumArg<NoiseType>,
    shape: readonly [number, number],
    kw: readonly [number, number],
    seed: number,
    options: FractalOptions | undefined,
  ) => ({
    t: enumValue(mod.NoiseType, noiseType, "noiseType"),
    ni: shape[0],
    nj: shape[1],
    kx: kw[0],
    ky: kw[1],
    seed,
    ...FRACTAL_DEFAULTS,
    ...options,
  });

  const api: Highmap = {
    NoiseType: mod.NoiseType,
    DistanceFunction: mod.DistanceFunction,

    makeGrid: (ni, nj, value = 0) => wrap(mod.makeArray(ni, nj, value)),

    noise: (noiseType, shape, kw, seed) => {
      const a = fractalArgs(noiseType, shape, kw, seed, undefined);
      return wrap(mod.noise(a.t, a.ni, a.nj, a.kx, a.ky, a.seed));
    },

    noiseFbm: (noiseType, shape, kw, seed, options) => {
      const a = fractalArgs(noiseType, shape, kw, seed, options);
      return wrap(
        mod.noiseFbm(
          a.t, a.ni, a.nj, a.kx, a.ky, a.seed,
          a.octaves, a.weight, a.persistence, a.lacunarity,
        ),
      );
    },

    noisePingpong: (noiseType, shape, kw, seed, options) => {
      const a = fractalArgs(noiseType, shape, kw, seed, options);
      return wrap(
        mod.noisePingpong(
          a.t, a.ni, a.nj, a.kx, a.ky, a.seed,
          a.octaves, a.weight, a.persistence, a.lacunarity,
        ),
      );
    },

    noiseRidged: (noiseType, shape, kw, seed, options) => {
      const a = { ...fractalArgs(noiseType, shape, kw, seed, options), kSmoothing: 0.1, ...(options ?? {}) };
      return wrap(
        mod.noiseRidged(
          a.t, a.ni, a.nj, a.kx, a.ky, a.seed,
          a.octaves, a.weight, a.persistence, a.lacunarity, a.kSmoothing,
        ),
      );
    },

    clamp: (grid, options = {}) => {
      const vmin = options.vmin ?? 0;
      const vmax = options.vmax ?? 1;
      return inplace(grid, "grid", (raw) => mod.clampValues(raw, vmin, vmax));
    },

    remap: (grid, options = {}) => {
      const vmin = options.vmin ?? 0;
      const vmax = options.vmax ?? 1;
      return inplace(grid, "grid", (raw) => mod.remapValues(raw, vmin, vmax));
    },

    falloff: (grid, options = {}) => {
      const strength = options.strength ?? 1;
      const distFct = enumValue(
        mod.DistanceFunction,
        options.distFct ?? "EUCLIDIAN",
        "distFct",
      );
      return inplace(grid, "grid", (raw) =>
        mod.falloff(raw, strength, distFct),
      );
    },

    plateau: (grid, ir, factor) =>
      inplace(grid, "grid", (raw) => mod.plateau(raw, ir, factor)),

    smoothFill: (grid, ir, options = {}) => {
      const k = options.k ?? 0.1;
      return inplace(grid, "grid", (raw) => mod.smoothFill(raw, ir, k));
    },

    hillshade: (grid, azimuth, zenith, options = {}) => {
      const talusRef = options.talusRef ?? 1;
      const { raw } = coerceGrid(grid, "grid", mod);
      return wrap(mod.hillshade(raw, azimuth, zenith, talusRef));
    },

    warp: (grid, dx, dy) =>
      inplace(grid, "grid", (raw) => {
        const d = coerceGrid(dx, "dx", mod);
        const e = coerceGrid(dy, "dy", mod);
        mod.warp(raw, d.raw, e.raw);
      }),

    add: (a, b) =>
      typeof b === "number"
        ? wrap(mod.addScalar(coerceGrid(a, "a", mod).raw, b))
        : wrap(mod.add(coerceGrid(a, "a", mod).raw, coerceGrid(b, "b", mod).raw)),

    subtract: (a, b) =>
      typeof b === "number"
        ? wrap(mod.subtractScalar(coerceGrid(a, "a", mod).raw, b))
        : wrap(mod.subtract(coerceGrid(a, "a", mod).raw, coerceGrid(b, "b", mod).raw)),

    multiply: (a, b) =>
      typeof b === "number"
        ? wrap(mod.multiplyScalar(coerceGrid(a, "a", mod).raw, b))
        : wrap(mod.multiply(coerceGrid(a, "a", mod).raw, coerceGrid(b, "b", mod).raw)),

    divide: (a, b) =>
      typeof b === "number"
        ? wrap(mod.divideScalar(coerceGrid(a, "a", mod).raw, b))
        : wrap(mod.divide(coerceGrid(a, "a", mod).raw, coerceGrid(b, "b", mod).raw)),

    scale: (grid, factor) =>
      inplace(grid, "grid", (raw) => mod.scale(raw, factor)),

    hydraulicMusgrave: (grid, options = {}) => {
      const o = {
        iterations: 100,
        cCapacity: 1,
        cErosion: 0.1,
        cDeposition: 0.1,
        waterLevel: 0.01,
        evapRate: 0.01,
        ...options,
      };
      return inplace(grid, "grid", (raw) => {
        mod.hydraulicMusgrave(
          raw,
          o.iterations, o.cCapacity, o.cErosion,
          o.cDeposition, o.waterLevel, o.evapRate,
        );
      });
    },

    hydraulicStream: (grid, cErosion, talusRef, options = {}) => {
      const ir = options.ir ?? 1;
      const clippingRatio = options.clippingRatio ?? 10;
      return inplace(grid, "grid", (raw) =>
        mod.hydraulicStream(raw, cErosion, talusRef, ir, clippingRatio),
      );
    },
  };

  return api;
}
