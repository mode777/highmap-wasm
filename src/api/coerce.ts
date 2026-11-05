import type { HighmapModule, RawArray } from "../loader/module.ts";
import { HighmapError } from "./errors.ts";
import { HArray } from "./harray.ts";

/** Plain grid input form: values plus `[ni, nj]` shape. */
export interface GridInput {
  data: Float32Array;
  shape: [number, number];
}

/** Anything a grid-accepting API function takes (AD-001). */
export type GridLike = HArray | GridInput;

function isGridInput(value: unknown): value is GridInput {
  return (
    typeof value === "object" &&
    value !== null &&
    "data" in value &&
    "shape" in value
  );
}

export interface CoercedGrid {
  raw: RawArray;
  /**
   * The caller's `HArray` when the input was one (zero-copy path), or `null`
   * when a fresh wasm array was created from a plain input.
   */
  source: HArray | null;
}

/**
 * Coerce a `GridLike` into a wasm-side array (AD-001).
 *
 * - `HArray` inputs pass through zero-copy.
 * - Plain `{ data, shape }` inputs are copied into a fresh wasm array.
 * - Everything is validated in JS **before** any wasm call; violations throw
 *   `HighmapError` naming the argument.
 */
export function coerceGrid(value: GridLike, argName: string, mod: HighmapModule): CoercedGrid {
  if (HArray.isWrapped(value)) {
    if (value.isFreed()) {
      throw new HighmapError(
        `${argName}: the HArray was freed; grids cannot be reused after free()`,
      );
    }
    return { raw: value.getRaw(), source: value };
  }

  if (!isGridInput(value)) {
    throw new HighmapError(
      `${argName}: expected an HArray or { data: Float32Array, shape: [ni, nj] }`,
    );
  }

  const { data, shape } = value;
  if (!(data instanceof Float32Array)) {
    throw new HighmapError(
      `${argName}.data: expected a Float32Array, got ${
        data === null || data === undefined ? String(data) : (data as object).constructor?.name ?? typeof data
      }`,
    );
  }
  if (
    !Array.isArray(shape) ||
    shape.length !== 2 ||
    !Number.isInteger(shape[0]) ||
    !Number.isInteger(shape[1]) ||
    (shape[0] as number) <= 0 ||
    (shape[1] as number) <= 0
  ) {
    throw new HighmapError(
      `${argName}.shape: expected two positive integers [ni, nj], got ${JSON.stringify(shape)}`,
    );
  }
  const [ni, nj] = shape as [number, number];
  if (data.length !== ni * nj) {
    throw new HighmapError(
      `${argName}: data length ${data.length} does not match shape [${ni}, ${nj}] (expected ${ni * nj})`,
    );
  }

  const raw = mod.makeArray(ni, nj, 0);
  new Float32Array(mod.wasmMemory.buffer, raw.dataPtr(), data.length).set(data);
  return { raw, source: null };
}
