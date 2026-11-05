import { createApi, type Highmap } from "./api/highmap.ts";
import { HArray } from "./api/harray.ts";
import { HighmapError } from "./api/errors.ts";
import { init, type InitOptions } from "./loader/init.ts";

export { HArray, HighmapError };
export type { GridInput, GridLike } from "./api/coerce.ts";
export type {
  Highmap,
  FractalOptions,
  RidgedOptions,
  ClampOptions,
  FalloffOptions,
  SmoothFillOptions,
  HillshadeOptions,
  HydraulicMusgraveOptions,
  HydraulicStreamOptions,
} from "./api/highmap.ts";
export type { InitOptions } from "./loader/init.ts";
export { DistanceFunction, NoiseType } from "./loader/module.ts";

export { init };

/**
 * Convenience: initialize the module and return the high-level API object
 * directly (`const hmap = await initHighmap()`).
 */
export async function initHighmap(options?: InitOptions): Promise<Highmap> {
  return createApi(await init(options));
}
