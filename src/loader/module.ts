/**
 * Typed view over the embind module produced by the Emscripten factory
 * (dist/highmap.js). Only the symbols used by the JS layer are declared;
 * the binding manifest (HighMap/wasm/manifest.md) is the source of truth
 * for what exists on the module.
 */

/** Raw embind handle for an `hmap::Array`; `.delete()` releases it. */
export interface RawArray {
  ni(): number;
  nj(): number;
  dataPtr(): number;
  delete(): void;
}

export enum NoiseType {
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

export enum DistanceFunction {
  CHEBYSHEV = "CHEBYSHEV",
  EUCLIDIAN = "EUCLIDIAN",
  EUCLISHEV = "EUCLISHEV",
  MANHATTAN = "MANHATTAN",
}

export interface HighmapModule {
  /** The module's wasm memory; views derive from `wasmMemory.buffer`. */
  readonly wasmMemory: WebAssembly.Memory;

  // enums (embind enum members carry their numeric value on `.value`)
  readonly NoiseType: Record<NoiseType, { value: number }>;
  readonly DistanceFunction: Record<DistanceFunction, { value: number }>;

  // classes
  makeArray(ni: number, nj: number, value: number): RawArray;

  // noise family
  noise(
    noiseType: { value: number },
    ni: number,
    nj: number,
    kx: number,
    ky: number,
    seed: number,
  ): RawArray;
  noiseFbm(
    noiseType: { value: number },
    ni: number,
    nj: number,
    kx: number,
    ky: number,
    seed: number,
    octaves: number,
    weight: number,
    persistence: number,
    lacunarity: number,
  ): RawArray;
  noisePingpong(
    noiseType: { value: number },
    ni: number,
    nj: number,
    kx: number,
    ky: number,
    seed: number,
    octaves: number,
    weight: number,
    persistence: number,
    lacunarity: number,
  ): RawArray;
  noiseRidged(
    noiseType: { value: number },
    ni: number,
    nj: number,
    kx: number,
    ky: number,
    seed: number,
    octaves: number,
    weight: number,
    persistence: number,
    lacunarity: number,
    kSmoothing: number,
  ): RawArray;

  // value range / filters (in place)
  clampValues(raw: RawArray, vmin: number, vmax: number): void;
  remapValues(raw: RawArray, vmin: number, vmax: number): void;
  falloff(raw: RawArray, strength: number, distFct: { value: number }): void;
  plateau(raw: RawArray, ir: number, factor: number): void;
  smoothFill(raw: RawArray, ir: number, k: number): void;

  // shading (new array)
  hillshade(
    raw: RawArray,
    azimuth: number,
    zenith: number,
    talusRef: number,
  ): RawArray;

  // transforms (in place)
  warp(raw: RawArray, dx: RawArray, dy: RawArray): void;

  // algebra (new arrays)
  add(a: RawArray, b: RawArray): RawArray;
  subtract(a: RawArray, b: RawArray): RawArray;
  multiply(a: RawArray, b: RawArray): RawArray;
  divide(a: RawArray, b: RawArray): RawArray;
  addScalar(a: RawArray, v: number): RawArray;
  subtractScalar(a: RawArray, v: number): RawArray;
  multiplyScalar(a: RawArray, v: number): RawArray;
  divideScalar(a: RawArray, v: number): RawArray;
  scale(raw: RawArray, factor: number): void;

  // erosion (in place)
  hydraulicMusgrave(
    z: RawArray,
    iterations: number,
    cCapacity: number,
    cErosion: number,
    cDeposition: number,
    waterLevel: number,
    evapRate: number,
  ): void;
  hydraulicStream(
    z: RawArray,
    cErosion: number,
    talusRef: number,
    ir: number,
    clippingRatio: number,
  ): void;

  // thermal family (CPU ports; in place)
  thermal(z: RawArray, talus: RawArray, iterations: number): void;
  thermalAutoBedrock(z: RawArray, talus: RawArray, iterations: number): void;
  thermalConserve(
    z: RawArray,
    talus: RawArray,
    iterations: number,
    rate: number,
  ): void;
  thermalFlatten(
    z: RawArray,
    talus: RawArray,
    iterations: number,
    sigmaInf: number,
    sigmaSup: number,
  ): void;
  thermalOlsen(z: RawArray, talus: RawArray, iterations: number): void;
  thermalRib(z: RawArray, iterations: number): void;
  thermalInflate(z: RawArray, talus: RawArray, iterations: number): void;
  thermalRidge(z: RawArray, talus: RawArray, iterations: number): void;
  thermalScree(
    z: RawArray,
    talus: RawArray,
    zmax: RawArray,
    iterations: number,
  ): void;
  thermalSchott(
    z: RawArray,
    talus: RawArray,
    iterations: number,
    intensity: number,
  ): void;

  // hydraulic family (CPU ports; in place, optional output maps)
  hydraulicMcdonald(
    z: RawArray,
    steps: number,
    seed: number,
    sediment: RawArray | null,
    discharge: RawArray | null,
    worldExtentKm: number,
    zScaleKm: number,
    samples: number,
    maxage: number,
    lrate: number,
    timeStep: number,
    rainfall: number,
    evapRate: number,
    gravity: number,
    viscosity: number,
    bedShear: number,
    critSlope: number,
    settleRate: number,
    thermalRate: number,
    depositionRate: number,
    suspensionRate: number,
    exitSlope: number,
  ): void;
  hydraulicMcdonaldMultiscale(
    z: RawArray,
    seed: number,
    stepsPerLevel: number[],
    sediment: RawArray | null,
    discharge: RawArray | null,
    worldExtentKm: number,
    zScaleKm: number,
    samples: number,
    maxage: number,
    lrate: number,
    timeStep: number,
    rainfall: number,
    evapRate: number,
    gravity: number,
    viscosity: number,
    bedShear: number,
    critSlope: number,
    settleRate: number,
    thermalRate: number,
    depositionRate: number,
    suspensionRate: number,
    exitSlope: number,
  ): void;
  hydraulicSchott(
    z: RawArray,
    iterations: number,
    talus: RawArray,
    flow: RawArray | null,
    cErosion: number,
    cThermal: number,
    cDeposition: number,
    flowAccExponent: number,
    flowAccExponentDepo: number,
    flowRoutingExponent: number,
    thermalWeight: number,
    depositionWeight: number,
  ): void;
  hydraulicSchottErosion(
    z: RawArray,
    iterations: number,
    flow: RawArray | null,
    cErosion: number,
    flowAccExponent: number,
    flowRoutingExponent: number,
  ): void;
  hydraulicVpipes(
    z: RawArray,
    waterHeight: number,
    maintainWaterVolume: boolean,
    evapRate: number,
    iterations: number,
    dt: number,
    kCapacity: number,
    kErode: number,
    kDepose: number,
    kDischargeExp: number,
    downcuttingMaxDepthRatio: number,
    fluxDiffusion: boolean,
    fluxDiffusionStrength: number,
    waterDepth: RawArray | null,
    sediment: RawArray | null,
    velU: RawArray | null,
    velV: RawArray | null,
  ): void;

  // rifts / advection family (CPU ports; rifts in place, advection new grids)
  rifts(
    z: RawArray,
    kwX: number,
    kwY: number,
    angle: number,
    amplitude: number,
    seed: number,
    elevationNoiseShift: number,
    kSmoothBottom: number,
    kSmoothTop: number,
    radialSpreadAmp: number,
    elevationNoiseAmp: number,
    clampVmin: number,
    remapVmin: number,
    applyMask: boolean,
    reverseMask: boolean,
    maskGamma: number,
    centerX: number,
    centerY: number,
    bboxXmin: number,
    bboxXmax: number,
    bboxYmin: number,
    bboxYmax: number,
  ): void;
  advectionParticle(
    z: RawArray,
    field: RawArray,
    iterations: number,
    nparticles: number,
    seed: number,
    reverse: boolean,
    postFilter: boolean,
    postFilterSigma: number,
    advectionLength: number,
    valuePersistence: number,
    inertia: number,
  ): RawArray;
  advectionWarp(
    z: RawArray,
    field: RawArray,
    advectionLength: number,
    valuePersistence: number,
  ): RawArray;
}

export type HighmapFactory = (
  overrides?: Record<string, unknown>,
) => Promise<HighmapModule>;
