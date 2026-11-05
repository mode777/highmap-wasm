import type { HArray } from "highmap-wasm";
import type { Heightmap } from "./types.js";

/** Copy wasm-backed values into JS-owned data. Call inside a try/finally. */
export function copyOut(grid: HArray): Heightmap {
  return { data: grid.copyData(), shape: grid.shape };
}
