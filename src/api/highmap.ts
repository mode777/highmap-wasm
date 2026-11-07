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

/** Options shared by the thermal family (a trailing options object each). */
export interface ThermalOptions {
  iterations?: number;
}

/** `thermalConserve` adds the redistribution rate. */
export interface ThermalConserveOptions extends ThermalOptions {
  rate?: number;
}

/** `thermalFlatten` relaxes slopes below and above the talus threshold. */
export interface ThermalFlattenOptions extends ThermalOptions {
  sigmaInf?: number;
  sigmaSup?: number;
}

/** `thermalSchott` adds the per-iteration intensity. */
export interface ThermalSchottOptions extends ThermalOptions {
  intensity?: number;
}

/** Options shared by the McDonald hydraulic functions. `sediment` and
 * `discharge` are optional caller-allocated output grids. */
export interface HydraulicMcdonaldOptions {
  sediment?: HArray;
  discharge?: HArray;
  worldExtentKm?: number;
  zScaleKm?: number;
  samples?: number;
  maxage?: number;
  lrate?: number;
  timeStep?: number;
  rainfall?: number;
  evapRate?: number;
  gravity?: number;
  viscosity?: number;
  bedShear?: number;
  critSlope?: number;
  settleRate?: number;
  thermalRate?: number;
  depositionRate?: number;
  suspensionRate?: number;
  exitSlope?: number;
}

/** `hydraulicMcdonaldMultiscale` adds the resolution ladder. */
export interface HydraulicMcdonaldMultiscaleOptions
  extends HydraulicMcdonaldOptions {
  stepsPerLevel?: readonly number[];
}

/** Options for the full Schott procedure. `flow` is an optional output grid. */
export interface HydraulicSchottOptions {
  flow?: HArray;
  iterations?: number;
  cErosion?: number;
  cThermal?: number;
  cDeposition?: number;
  flowAccExponent?: number;
  flowAccExponentDepo?: number;
  flowRoutingExponent?: number;
  thermalWeight?: number;
  depositionWeight?: number;
}

/** Options for the erosion-only Schott form. */
export interface HydraulicSchottErosionOptions {
  flow?: HArray;
  iterations?: number;
  cErosion?: number;
  flowAccExponent?: number;
  flowRoutingExponent?: number;
}

/** Options for the virtual-pipes model. The four maps are optional outputs. */
export interface HydraulicVpipesOptions {
  waterDepth?: HArray;
  sediment?: HArray;
  velU?: HArray;
  velV?: HArray;
  waterHeight?: number;
  maintainWaterVolume?: boolean;
  evapRate?: number;
  iterations?: number;
  dt?: number;
  kCapacity?: number;
  kErode?: number;
  kDepose?: number;
  kDischargeExp?: number;
  downcuttingMaxDepthRatio?: number;
  fluxDiffusion?: boolean;
  fluxDiffusionStrength?: number;
}

/** Options for `rifts`. `center` is `[cx, cy]`; `bbox` is
 * `[xmin, xmax, ymin, ymax]`, mirroring the C++ `glm::vec4` order (it is NOT
 * `[xmin, ymin, xmax, ymax]`). */
export interface RiftsOptions {
  elevationNoiseShift?: number;
  kSmoothBottom?: number;
  kSmoothTop?: number;
  radialSpreadAmp?: number;
  elevationNoiseAmp?: number;
  clampVmin?: number;
  remapVmin?: number;
  applyMask?: boolean;
  reverseMask?: boolean;
  maskGamma?: number;
  center?: readonly [number, number];
  bbox?: readonly [number, number, number, number];
}

/** Options for `advectionParticle`; `iterations` defaults to 1, the single
 * pass of the C++ overload without an iterations argument (0 is a no-op). */
export interface AdvectionParticleOptions {
  iterations?: number;
  reverse?: boolean;
  postFilter?: boolean;
  postFilterSigma?: number;
  advectionLength?: number;
  valuePersistence?: number;
  inertia?: number;
}

/** Options for the z-driven `advectionWarp` form. */
export interface AdvectionWarpOptions {
  advectionLength?: number;
  valuePersistence?: number;
}

/**
 * The high-level API surface (grows per the binding manifest, AD-008).
 *
 * Semantics: functions that mutate the grid in C++ accept either input form;
 * `HArray` inputs are modified **in place** and returned (zero-copy
 * pipelines), plain `{ data, shape }` inputs are copied first and a new
 * `HArray` is returned. Functions that produce a new array in C++
 * (`hillshade`, the algebra layer, the advection family) always return a fresh
 * `HArray`.
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

  // --- hydraulic family (in place; optional caller-allocated output maps) ------

  hydraulicMcdonald(
    grid: GridLike,
    steps: number,
    seed: number,
    options?: HydraulicMcdonaldOptions,
  ): HArray;

  hydraulicMcdonaldMultiscale(
    grid: GridLike,
    seed: number,
    options?: HydraulicMcdonaldMultiscaleOptions,
  ): HArray;

  hydraulicSchott(
    grid: GridLike,
    talus: number | GridLike,
    options?: HydraulicSchottOptions,
  ): HArray;

  hydraulicSchottErosion(
    grid: GridLike,
    options?: HydraulicSchottErosionOptions,
  ): HArray;

  hydraulicVpipes(
    grid: GridLike,
    options?: HydraulicVpipesOptions,
  ): HArray;

  // --- thermal family (in place; talus is uniform or per-cell) ------------------

  thermal(
    grid: GridLike,
    talus: number | GridLike,
    options?: ThermalOptions,
  ): HArray;

  thermalAutoBedrock(
    grid: GridLike,
    talus: number | GridLike,
    options?: ThermalOptions,
  ): HArray;

  thermalConserve(
    grid: GridLike,
    talus: number | GridLike,
    options?: ThermalConserveOptions,
  ): HArray;

  thermalFlatten(
    grid: GridLike,
    talus: number | GridLike,
    options?: ThermalFlattenOptions,
  ): HArray;

  thermalOlsen(
    grid: GridLike,
    talus: number | GridLike,
    options?: ThermalOptions,
  ): HArray;

  thermalRib(grid: GridLike, options?: ThermalOptions): HArray;

  thermalInflate(
    grid: GridLike,
    talus: number | GridLike,
    options?: ThermalOptions,
  ): HArray;

  thermalRidge(
    grid: GridLike,
    talus: number | GridLike,
    options?: ThermalOptions,
  ): HArray;

  thermalScree(
    grid: GridLike,
    talus: number | GridLike,
    zmax: number | GridLike,
    options?: ThermalOptions,
  ): HArray;

  thermalSchott(
    grid: GridLike,
    talus: number | GridLike,
    options?: ThermalSchottOptions,
  ): HArray;

  // --- rifts / advection family (rifts in place; advection new grids) ----------

  rifts(
    grid: GridLike,
    kw: readonly [number, number],
    angle: number,
    amplitude: number,
    seed: number,
    options?: RiftsOptions,
  ): HArray;

  advectionParticle(
    grid: GridLike,
    field: GridLike,
    nparticles: number,
    seed: number,
    options?: AdvectionParticleOptions,
  ): HArray;

  advectionWarp(
    grid: GridLike,
    field: GridLike,
    options?: AdvectionWarpOptions,
  ): HArray;
}

const FRACTAL_DEFAULTS = {
  octaves: 8,
  weight: 0.7,
  persistence: 0.5,
  lacunarity: 2,
} as const;

/** Apply the C++ defaults for the shared McDonald parameters. */
function mcdonaldOptions(options: HydraulicMcdonaldOptions) {
  return {
    worldExtentKm: options.worldExtentKm ?? 40,
    zScaleKm: options.zScaleKm ?? 4,
    samples: options.samples ?? 8192,
    maxage: options.maxage ?? 512,
    lrate: options.lrate ?? 0.2,
    timeStep: options.timeStep ?? 10,
    rainfall: options.rainfall ?? 1,
    evapRate: options.evapRate ?? 1e-4,
    gravity: options.gravity ?? 9.81,
    viscosity: options.viscosity ?? 0.025,
    bedShear: options.bedShear ?? 0.01,
    critSlope: options.critSlope ?? 0.57,
    settleRate: options.settleRate ?? 0.1,
    thermalRate: options.thermalRate ?? 2.5e-3,
    depositionRate: options.depositionRate ?? 5e-3,
    suspensionRate: options.suspensionRate ?? 2.5e-4,
    exitSlope: options.exitSlope ?? 0.01,
    sediment: options.sediment,
    discharge: options.discharge,
  };
}

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

  /**
   * Run a C++ op that produces a new grid (advection family, AD-027). Every
   * input is coerced; later inputs must match the first grid's shape. Plain
   * `{ data, shape }` inputs become temporaries that are freed in `finally`,
   * `HArray` inputs pass through untouched and the returned raw handle is
   * always wrapped in a fresh `HArray`.
   */
  const newGrid = (
    inputs: { value: GridLike; name: string }[],
    run: (raws: RawArray[]) => RawArray,
  ): HArray => {
    const raws: RawArray[] = [];
    const owned: RawArray[] = [];

    try {
      let shape: [number, number] | null = null;

      for (const { value, name } of inputs) {
        const c = coerceGrid(value, name, mod);
        if (!c.source) owned.push(c.raw);

        const ni = c.raw.ni();
        const nj = c.raw.nj();
        if (shape === null) {
          shape = [ni, nj];
        } else if (ni !== shape[0] || nj !== shape[1]) {
          throw new HighmapError(
            `${name}: shape [${ni}, ${nj}] does not match grid shape [${shape[0]}, ${shape[1]}]`,
          );
        }
        raws.push(c.raw);
      }

      return wrap(run(raws));
    } finally {
      for (const temp of owned) temp.delete();
    }
  };

  /**
   * Run an in-place op that needs auxiliary grids (thermal talus / zmax).
   * Numeric auxiliary values become constant grids sized like the target; those
   * temporaries are freed after the call, and plain-object auxiliaries are
   * copied in and freed too (design D4).
   */
  const inplaceWithAux = (
    grid: GridLike,
    aux: { value: number | GridLike; name: string }[],
    run: (raw: RawArray, auxRaws: RawArray[]) => void,
  ): HArray => {
    const { raw, source } = coerceGrid(grid, "grid", mod);
    const owned: RawArray[] = [];
    const auxRaws: RawArray[] = [];

    try {
      for (const { value, name } of aux) {
        if (typeof value === "number") {
          const temp = mod.makeArray(raw.ni(), raw.nj(), value);
          owned.push(temp);
          auxRaws.push(temp);
          continue;
        }

        const c = coerceGrid(value, name, mod);
        const ni = c.raw.ni();
        const nj = c.raw.nj();
        if (ni !== raw.ni() || nj !== raw.nj()) {
          if (!c.source) c.raw.delete();
          throw new HighmapError(
            `${name}: shape [${ni}, ${nj}] does not match grid shape [${raw.ni()}, ${raw.nj()}]`,
          );
        }
        if (!c.source) owned.push(c.raw);
        auxRaws.push(c.raw);
      }

      run(raw, auxRaws);
    } finally {
      for (const temp of owned) temp.delete();
    }

    return source ?? wrap(raw);
  };

  /** Validate a required numeric argument before the wasm call. */
  const requireNumber = (value: number | undefined, name: string): number => {
    if (typeof value !== "number" || !Number.isFinite(value)) {
      throw new HighmapError(
        `${name}: required number argument is missing or invalid`,
      );
    }
    return value;
  };

  /** Validate a required fixed-length numeric tuple before the wasm call. */
  const requireTuple = (
    value: readonly number[] | undefined,
    length: number,
    name: string,
  ): readonly number[] => {
    if (
      !Array.isArray(value) ||
      value.length !== length ||
      value.some((v) => typeof v !== "number" || !Number.isFinite(v))
    ) {
      const expected = new Array(length).fill("number").join(", ");
      throw new HighmapError(
        `${name}: required [${expected}] tuple is missing or invalid`,
      );
    }
    return value;
  };

  /** Validate the McDonald resolution ladder before the wasm call. */
  const requireIntArray = (
    value: readonly number[] | undefined,
    name: string,
  ): number[] => {
    if (!Array.isArray(value) || value.some((v) => !Number.isInteger(v))) {
      throw new HighmapError(`${name}: expected an array of integers`);
    }
    return value;
  };

  /**
   * Run an in-place op with optional caller-allocated output maps. Outputs must
   * be live HArrays of the terrain grid's shape and must not alias the terrain
   * grid or one another; omitted maps become null output pointers (design D4).
   */
  const inplaceWithOutputs = (
    grid: GridLike,
    outputs: { value: HArray | undefined; name: string }[],
    run: (raw: RawArray, outRaws: (RawArray | null)[]) => void,
  ): HArray => {
    const { raw, source } = coerceGrid(grid, "grid", mod);
    const outRaws: (RawArray | null)[] = [];
    const seen = new Set<RawArray>();

    for (const { value, name } of outputs) {
      if (value === undefined) {
        outRaws.push(null);
        continue;
      }
      if (!HArray.isWrapped(value)) {
        throw new HighmapError(`${name}: expected an HArray output grid`);
      }
      if (value.isFreed()) {
        throw new HighmapError(
          `${name}: the HArray was freed; create a new output grid`,
        );
      }
      if (source && value === source) {
        throw new HighmapError(
          `${name}: output map must not be the terrain grid`,
        );
      }
      const [ni, nj] = value.shape;
      if (ni !== raw.ni() || nj !== raw.nj()) {
        throw new HighmapError(
          `${name}: shape [${ni}, ${nj}] does not match grid shape [${raw.ni()}, ${raw.nj()}]`,
        );
      }
      const outRaw = value.getRaw();
      if (seen.has(outRaw)) {
        throw new HighmapError(
          `${name}: output map aliases another output map`,
        );
      }
      seen.add(outRaw);
      outRaws.push(outRaw);
    }

    run(raw, outRaws);
    return source ?? wrap(raw);
  };

  /**
   * Combined auxiliary-input and output-map form (Schott's talus + flow).
   * Auxiliary values are materialized exactly like `inplaceWithAux`; outputs
   * are validated like `inplaceWithOutputs` and may not alias an input.
   */
  const inplaceWithAuxAndOutputs = (
    grid: GridLike,
    aux: { value: number | GridLike; name: string }[],
    outputs: { value: HArray | undefined; name: string }[],
    run: (
      raw: RawArray,
      auxRaws: RawArray[],
      outRaws: (RawArray | null)[],
    ) => void,
  ): HArray => {
    const { raw, source } = coerceGrid(grid, "grid", mod);
    const owned: RawArray[] = [];
    const auxRaws: RawArray[] = [];
    const outRaws: (RawArray | null)[] = [];
    const seen = new Set<RawArray>();

    try {
      for (const { value, name } of aux) {
        if (typeof value === "number") {
          const temp = mod.makeArray(raw.ni(), raw.nj(), value);
          owned.push(temp);
          auxRaws.push(temp);
          seen.add(temp);
          continue;
        }

        const c = coerceGrid(value, name, mod);
        const ni = c.raw.ni();
        const nj = c.raw.nj();
        if (ni !== raw.ni() || nj !== raw.nj()) {
          if (!c.source) c.raw.delete();
          throw new HighmapError(
            `${name}: shape [${ni}, ${nj}] does not match grid shape [${raw.ni()}, ${raw.nj()}]`,
          );
        }
        if (!c.source) owned.push(c.raw);
        auxRaws.push(c.raw);
        seen.add(c.raw);
      }

      for (const { value, name } of outputs) {
        if (value === undefined) {
          outRaws.push(null);
          continue;
        }
        if (!HArray.isWrapped(value)) {
          throw new HighmapError(`${name}: expected an HArray output grid`);
        }
        if (value.isFreed()) {
          throw new HighmapError(
            `${name}: the HArray was freed; create a new output grid`,
          );
        }
        if (source && value === source) {
          throw new HighmapError(
            `${name}: output map must not be the terrain grid`,
          );
        }
        const [ni, nj] = value.shape;
        if (ni !== raw.ni() || nj !== raw.nj()) {
          throw new HighmapError(
            `${name}: shape [${ni}, ${nj}] does not match grid shape [${raw.ni()}, ${raw.nj()}]`,
          );
        }
        const outRaw = value.getRaw();
        if (seen.has(outRaw)) {
          throw new HighmapError(
            `${name}: output map must not alias an input or another output`,
          );
        }
        seen.add(outRaw);
        outRaws.push(outRaw);
      }

      run(raw, auxRaws, outRaws);
    } finally {
      for (const temp of owned) temp.delete();
    }

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

    hydraulicMcdonald: (grid, steps, seed, options = {}) => {
      const stepsValue = requireNumber(steps, "steps");
      const seedValue = requireNumber(seed, "seed");
      const o = mcdonaldOptions(options);
      return inplaceWithOutputs(
        grid,
        [
          { value: o.sediment, name: "sediment" },
          { value: o.discharge, name: "discharge" },
        ],
        (raw, out) =>
          mod.hydraulicMcdonald(
            raw,
            stepsValue,
            seedValue,
            out[0]!,
            out[1]!,
            o.worldExtentKm,
            o.zScaleKm,
            o.samples,
            o.maxage,
            o.lrate,
            o.timeStep,
            o.rainfall,
            o.evapRate,
            o.gravity,
            o.viscosity,
            o.bedShear,
            o.critSlope,
            o.settleRate,
            o.thermalRate,
            o.depositionRate,
            o.suspensionRate,
            o.exitSlope,
          ),
      );
    },

    hydraulicMcdonaldMultiscale: (grid, seed, options = {}) => {
      const seedValue = requireNumber(seed, "seed");
      const stepsPerLevel = requireIntArray(
        options.stepsPerLevel ?? [512, 256, 128],
        "stepsPerLevel",
      );
      const o = mcdonaldOptions(options);
      return inplaceWithOutputs(
        grid,
        [
          { value: o.sediment, name: "sediment" },
          { value: o.discharge, name: "discharge" },
        ],
        (raw, out) =>
          mod.hydraulicMcdonaldMultiscale(
            raw,
            seedValue,
            stepsPerLevel,
            out[0]!,
            out[1]!,
            o.worldExtentKm,
            o.zScaleKm,
            o.samples,
            o.maxage,
            o.lrate,
            o.timeStep,
            o.rainfall,
            o.evapRate,
            o.gravity,
            o.viscosity,
            o.bedShear,
            o.critSlope,
            o.settleRate,
            o.thermalRate,
            o.depositionRate,
            o.suspensionRate,
            o.exitSlope,
          ),
      );
    },

    hydraulicSchott: (grid, talus, options = {}) => {
      const iterations = options.iterations ?? 10;
      const cErosion = options.cErosion ?? 1;
      const cThermal = options.cThermal ?? 0.1;
      const cDeposition = options.cDeposition ?? 0.2;
      const flowAccExponent = options.flowAccExponent ?? 0.8;
      const flowAccExponentDepo = options.flowAccExponentDepo ?? 0.8;
      const flowRoutingExponent = options.flowRoutingExponent ?? 1.3;
      const thermalWeight = options.thermalWeight ?? 1.5;
      const depositionWeight = options.depositionWeight ?? 2.5;

      return inplaceWithAuxAndOutputs(
        grid,
        [{ value: talus, name: "talus" }],
        [{ value: options.flow, name: "flow" }],
        (raw, aux, out) =>
          mod.hydraulicSchott(
            raw,
            iterations,
            aux[0]!,
            out[0]!,
            cErosion,
            cThermal,
            cDeposition,
            flowAccExponent,
            flowAccExponentDepo,
            flowRoutingExponent,
            thermalWeight,
            depositionWeight,
          ),
      );
    },

    hydraulicSchottErosion: (grid, options = {}) => {
      const iterations = options.iterations ?? 10;
      const cErosion = options.cErosion ?? 1;
      const flowAccExponent = options.flowAccExponent ?? 0.8;
      const flowRoutingExponent = options.flowRoutingExponent ?? 1.3;

      return inplaceWithOutputs(
        grid,
        [{ value: options.flow, name: "flow" }],
        (raw, out) =>
          mod.hydraulicSchottErosion(
            raw,
            iterations,
            out[0]!,
            cErosion,
            flowAccExponent,
            flowRoutingExponent,
          ),
      );
    },

    hydraulicVpipes: (grid, options = {}) => {
      const waterHeight = options.waterHeight ?? 1e-2;
      const maintainWaterVolume = options.maintainWaterVolume ?? true;
      const evapRate = options.evapRate ?? 0.1;
      const iterations = options.iterations ?? 50;
      const dt = options.dt ?? 0.5;
      const kCapacity = options.kCapacity ?? 0.5;
      const kErode = options.kErode ?? 0.001;
      const kDepose = options.kDepose ?? 0.01;
      const kDischargeExp = options.kDischargeExp ?? 1;
      const downcuttingMaxDepthRatio = options.downcuttingMaxDepthRatio ?? 10;
      const fluxDiffusion = options.fluxDiffusion ?? true;
      const fluxDiffusionStrength = options.fluxDiffusionStrength ?? 0.01;

      return inplaceWithOutputs(
        grid,
        [
          { value: options.waterDepth, name: "waterDepth" },
          { value: options.sediment, name: "sediment" },
          { value: options.velU, name: "velU" },
          { value: options.velV, name: "velV" },
        ],
        (raw, out) =>
          mod.hydraulicVpipes(
            raw,
            waterHeight,
            maintainWaterVolume,
            evapRate,
            iterations,
            dt,
            kCapacity,
            kErode,
            kDepose,
            kDischargeExp,
            downcuttingMaxDepthRatio,
            fluxDiffusion,
            fluxDiffusionStrength,
            out[0]!,
            out[1]!,
            out[2]!,
            out[3]!,
          ),
      );
    },

    thermal: (grid, talus, options = {}) => {
      const iterations = options.iterations ?? 10;
      return inplaceWithAux(
        grid,
        [{ value: talus, name: "talus" }],
        (raw, aux) => mod.thermal(raw, aux[0]!, iterations),
      );
    },

    thermalAutoBedrock: (grid, talus, options = {}) => {
      const iterations = options.iterations ?? 10;
      return inplaceWithAux(
        grid,
        [{ value: talus, name: "talus" }],
        (raw, aux) => mod.thermalAutoBedrock(raw, aux[0]!, iterations),
      );
    },

    thermalConserve: (grid, talus, options = {}) => {
      const iterations = options.iterations ?? 10;
      const rate = options.rate ?? 0.5;
      return inplaceWithAux(
        grid,
        [{ value: talus, name: "talus" }],
        (raw, aux) => mod.thermalConserve(raw, aux[0]!, iterations, rate),
      );
    },

    thermalFlatten: (grid, talus, options = {}) => {
      const iterations = options.iterations ?? 10;
      const sigmaInf = options.sigmaInf ?? 0.5;
      const sigmaSup = options.sigmaSup ?? 0;
      return inplaceWithAux(
        grid,
        [{ value: talus, name: "talus" }],
        (raw, aux) =>
          mod.thermalFlatten(raw, aux[0]!, iterations, sigmaInf, sigmaSup),
      );
    },

    thermalOlsen: (grid, talus, options = {}) => {
      const iterations = options.iterations ?? 10;
      return inplaceWithAux(
        grid,
        [{ value: talus, name: "talus" }],
        (raw, aux) => mod.thermalOlsen(raw, aux[0]!, iterations),
      );
    },

    thermalRib: (grid, options = {}) => {
      const iterations = options.iterations ?? 10;
      return inplace(grid, "grid", (raw) => mod.thermalRib(raw, iterations));
    },

    thermalInflate: (grid, talus, options = {}) => {
      const iterations = options.iterations ?? 10;
      return inplaceWithAux(
        grid,
        [{ value: talus, name: "talus" }],
        (raw, aux) => mod.thermalInflate(raw, aux[0]!, iterations),
      );
    },

    thermalRidge: (grid, talus, options = {}) => {
      const iterations = options.iterations ?? 10;
      return inplaceWithAux(
        grid,
        [{ value: talus, name: "talus" }],
        (raw, aux) => mod.thermalRidge(raw, aux[0]!, iterations),
      );
    },

    thermalScree: (grid, talus, zmax, options = {}) => {
      const iterations = options.iterations ?? 10;
      return inplaceWithAux(
        grid,
        [
          { value: talus, name: "talus" },
          { value: zmax, name: "zmax" },
        ],
        (raw, aux) => mod.thermalScree(raw, aux[0]!, aux[1]!, iterations),
      );
    },

    thermalSchott: (grid, talus, options = {}) => {
      const iterations = options.iterations ?? 10;
      const intensity = options.intensity ?? 0.2;
      return inplaceWithAux(
        grid,
        [{ value: talus, name: "talus" }],
        (raw, aux) => mod.thermalSchott(raw, aux[0]!, iterations, intensity),
      );
    },

    rifts: (grid, kw, angle, amplitude, seed, options = {}) => {
      const kwValue = requireTuple(kw, 2, "kw");
      const angleValue = requireNumber(angle, "angle");
      const amplitudeValue = requireNumber(amplitude, "amplitude");
      const seedValue = requireNumber(seed, "seed");
      const center = requireTuple(options.center ?? [0.5, 0.5], 2, "center");
      const bbox = requireTuple(options.bbox ?? [0, 1, 0, 1], 4, "bbox");
      const elevationNoiseShift = options.elevationNoiseShift ?? 0;
      const kSmoothBottom = options.kSmoothBottom ?? 0.05;
      const kSmoothTop = options.kSmoothTop ?? 0.05;
      const radialSpreadAmp = options.radialSpreadAmp ?? 0.2;
      const elevationNoiseAmp = options.elevationNoiseAmp ?? 0.1;
      const clampVmin = options.clampVmin ?? 0;
      const remapVmin = options.remapVmin ?? 0;
      const applyMask = options.applyMask ?? true;
      const reverseMask = options.reverseMask ?? false;
      const maskGamma = options.maskGamma ?? 1;

      return inplace(grid, "grid", (raw) =>
        mod.rifts(
          raw,
          kwValue[0]!,
          kwValue[1]!,
          angleValue,
          amplitudeValue,
          seedValue,
          elevationNoiseShift,
          kSmoothBottom,
          kSmoothTop,
          radialSpreadAmp,
          elevationNoiseAmp,
          clampVmin,
          remapVmin,
          applyMask,
          reverseMask,
          maskGamma,
          center[0]!,
          center[1]!,
          bbox[0]!,
          bbox[1]!,
          bbox[2]!,
          bbox[3]!,
        ),
      );
    },

    advectionParticle: (grid, field, nparticles, seed, options = {}) => {
      const nparticlesValue = requireNumber(nparticles, "nparticles");
      const seedValue = requireNumber(seed, "seed");
      const iterations = options.iterations ?? 1;
      const reverse = options.reverse ?? false;
      const postFilter = options.postFilter ?? true;
      const postFilterSigma = options.postFilterSigma ?? 0.125;
      const advectionLength = options.advectionLength ?? 0.1;
      const valuePersistence = options.valuePersistence ?? 0.99;
      const inertia = options.inertia ?? 0;

      return newGrid(
        [
          { value: grid, name: "grid" },
          { value: field, name: "field" },
        ],
        (raws) =>
          mod.advectionParticle(
            raws[0]!,
            raws[1]!,
            iterations,
            nparticlesValue,
            seedValue,
            reverse,
            postFilter,
            postFilterSigma,
            advectionLength,
            valuePersistence,
            inertia,
          ),
      );
    },

    advectionWarp: (grid, field, options = {}) => {
      const advectionLength = options.advectionLength ?? 0.1;
      const valuePersistence = options.valuePersistence ?? 0.9;

      return newGrid(
        [
          { value: grid, name: "grid" },
          { value: field, name: "field" },
        ],
        (raws) =>
          mod.advectionWarp(
            raws[0]!,
            raws[1]!,
            advectionLength,
            valuePersistence,
          ),
      );
    },
  };

  return api;
}
