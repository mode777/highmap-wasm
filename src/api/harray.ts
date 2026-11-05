import type { HighmapModule, RawArray } from "../loader/module.ts";
import { HighmapError } from "./errors.ts";

/**
 * JS wrapper over a wasm-side `hmap::Array` (AD-001).
 *
 * - `data` is a **live zero-copy view** over wasm memory, re-bound on every
 *   access so it survives heap growth. It is valid until `free()`.
 * - `copyData()` returns a JS-owned copy that outlives the wrapper.
 * - `free()` is idempotent; a `FinalizationRegistry` reclaims wrappers that
 *   are garbage collected without an explicit `free()`.
 */
export class HArray {
  private readonly raw: RawArray;
  private readonly mod: HighmapModule;
  private readonly registry: FinalizationRegistry<RawArray>;
  private freed = false;

  private constructor(
    raw: RawArray,
    mod: HighmapModule,
    registry: FinalizationRegistry<RawArray>,
  ) {
    this.raw = raw;
    this.mod = mod;
    this.registry = registry;
  }

  /** @internal */
  static wrap(
    raw: RawArray,
    mod: HighmapModule,
    registry: FinalizationRegistry<RawArray>,
  ): HArray {
    const wrapper = new HArray(raw, mod, registry);
    registry.register(wrapper, raw, wrapper);
    return wrapper;
  }

  /** @internal */
  static isWrapped(value: unknown): value is HArray {
    return value instanceof HArray;
  }

  /** @internal */
  getRaw(): RawArray {
    this.assertAlive("getRaw");
    return this.raw;
  }

  /** Shape `[ni, nj]` of the grid. */
  get shape(): [number, number] {
    this.assertAlive("shape");
    return [this.raw.ni(), this.raw.nj()];
  }

  /**
   * Live `Float32Array` view of length `ni * nj`. Re-bound on every access;
   * keep no long-lived references unless you also keep the wrapper alive —
   * use `copyData()` for owned data. Throws after `free()`.
   */
  get data(): Float32Array {
    this.assertAlive("data");
    const len = this.raw.ni() * this.raw.nj();
    return new Float32Array(this.mod.wasmMemory.buffer, this.raw.dataPtr(), len);
  }

  /** JS-owned copy of the grid values; survives `free()` of the wrapper. */
  copyData(): Float32Array {
    this.assertAlive("copyData");
    return new Float32Array(this.data);
  }

  /** Release the underlying wasm memory. Idempotent. */
  free(): void {
    if (this.freed) return;
    this.freed = true;
    this.registry.unregister(this);
    this.raw.delete();
  }

  /** @internal */
  isFreed(): boolean {
    return this.freed;
  }

  private assertAlive(what: string): void {
    if (this.freed) {
      throw new HighmapError(
        `HArray.${what}: the wrapper was freed; create a new grid or use copyData() before freeing`,
      );
    }
  }
}
