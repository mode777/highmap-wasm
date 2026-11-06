// Hand-written public typings for the seed surface (design open-question
// fallback: raw typings hand-written once; regenerated per family later).
// Installed as dist/index.d.ts by scripts/build-js.mjs.

export declare class HighmapError extends Error {}

export declare enum NoiseType {
  PARBERRY = "PARBERRY",
  PERLIN = "PERLIN",
  PERLIN_BILLOW = "PERLIN_BILLOW",
  PERLIN_HALF = "PERLIN_HALF",
  SIMPLEX2 = "SIMPLEX2",
  SIMPLEX2S = "SIMPLEX2S",
  VALUE = "VALUE",
  VALUE_CUBIC = "VALUE_CUBIC",
  WORLEY = "WORLEY",
  WORLEY_DOUBLE = "WORLEY_DOUBLE",
  WORLEY_VALUE = "WORLEY_VALUE",
}

export declare enum DistanceFunction {
  CHEBYSHEV = "CHEBYSHEV",
  EUCLIDIAN = "EUCLIDIAN",
  EUCLISHEV = "EUCLISHEV",
  MANHATTAN = "MANHATTAN",
}

export declare interface GridInput {
  data: Float32Array;
  shape: [number, number];
}

export declare type GridLike = HArray | GridInput;

export declare class HArray {
  get shape(): [number, number];
  get data(): Float32Array;
  copyData(): Float32Array;
  free(): void;
}

export declare interface InitOptions {
  wasmBinary?: ArrayBuffer | Uint8Array;
  locateWasm?(filename: string): string | URL;
}

export declare interface FractalOptions {
  octaves?: number;
  weight?: number;
  persistence?: number;
  lacunarity?: number;
}
export declare interface RidgedOptions extends FractalOptions {
  kSmoothing?: number;
}
export declare interface ClampOptions {
  vmin?: number;
  vmax?: number;
}
export declare interface FalloffOptions {
  strength?: number;
  distFct?: DistanceFunction;
}
export declare interface SmoothFillOptions {
  k?: number;
}
export declare interface HillshadeOptions {
  talusRef?: number;
}
export declare interface HydraulicMusgraveOptions {
  iterations?: number;
  cCapacity?: number;
  cErosion?: number;
  cDeposition?: number;
  waterLevel?: number;
  evapRate?: number;
}
export declare interface HydraulicStreamOptions {
  ir?: number;
  clippingRatio?: number;
}
export declare interface ThermalOptions {
  iterations?: number;
}
export declare interface ThermalConserveOptions extends ThermalOptions {
  rate?: number;
}
export declare interface ThermalFlattenOptions extends ThermalOptions {
  sigmaInf?: number;
  sigmaSup?: number;
}
export declare interface ThermalSchottOptions extends ThermalOptions {
  intensity?: number;
}

export declare interface Highmap {
  readonly NoiseType: Record<NoiseType, number>;
  readonly DistanceFunction: Record<DistanceFunction, number>;
  makeGrid(ni: number, nj: number, value?: number): HArray;
  noise(
    noiseType: NoiseType,
    shape: readonly [number, number],
    kw: readonly [number, number],
    seed: number,
  ): HArray;
  noiseFbm(
    noiseType: NoiseType,
    shape: readonly [number, number],
    kw: readonly [number, number],
    seed: number,
    options?: FractalOptions,
  ): HArray;
  noisePingpong(
    noiseType: NoiseType,
    shape: readonly [number, number],
    kw: readonly [number, number],
    seed: number,
    options?: FractalOptions,
  ): HArray;
  noiseRidged(
    noiseType: NoiseType,
    shape: readonly [number, number],
    kw: readonly [number, number],
    seed: number,
    options?: RidgedOptions,
  ): HArray;
  clamp(grid: GridLike, options?: ClampOptions): HArray;
  remap(grid: GridLike, options?: ClampOptions): HArray;
  falloff(grid: GridLike, options?: FalloffOptions): HArray;
  plateau(grid: GridLike, ir: number, factor: number): HArray;
  smoothFill(grid: GridLike, ir: number, options?: SmoothFillOptions): HArray;
  hillshade(
    grid: GridLike,
    azimuth: number,
    zenith: number,
    options?: HillshadeOptions,
  ): HArray;
  warp(grid: GridLike, dx: GridLike, dy: GridLike): HArray;
  add(a: GridLike, b: GridLike | number): HArray;
  subtract(a: GridLike, b: GridLike | number): HArray;
  multiply(a: GridLike, b: GridLike | number): HArray;
  divide(a: GridLike, b: GridLike | number): HArray;
  scale(grid: GridLike, factor: number): HArray;
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
}

export declare function init(options?: InitOptions): Promise<unknown>;
export declare function initHighmap(options?: InitOptions): Promise<Highmap>;
