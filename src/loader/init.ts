import HighmapFactoryUntyped from "../../dist/highmap.js";
import type { HighmapModule } from "./module.ts";

/** The generated Emscripten ES6 factory (default export of dist/highmap.js). */
const HighmapFactory = HighmapFactoryUntyped as unknown as (
  overrides?: Record<string, unknown>,
) => Promise<HighmapModule>;

/** Options for {@link init}. */
export interface InitOptions {
  /**
   * Raw bytes of `highmap.wasm`. When set, no location logic runs at all —
   * useful for SSR setups and unusual bundlers.
   */
  wasmBinary?: ArrayBuffer | Uint8Array;

  /**
   * Map the artifact filename (e.g. `highmap.wasm`) to the URL it is served
   * from. Takes precedence over the default location, but not over
   * `wasmBinary`.
   */
  locateWasm?(filename: string): string | URL;
}

async function defaultFactory(): Promise<HighmapModule> {
  // The bundled glue references its wasm sibling via the standard
  // `new URL('./highmap.wasm', import.meta.url)` pattern; bundlers emit the
  // artifact as an asset without any plugin.
  return HighmapFactory();
}

/**
 * Instantiate the highmap-wasm module (AD-003).
 *
 * Every call creates an independent module instance with its own wasm heap;
 * objects from one instance must not be passed to functions of another.
 */
export async function init(options: InitOptions = {}): Promise<HighmapModule> {
  if (options.wasmBinary) {
    return HighmapFactory({ wasmBinary: options.wasmBinary });
  }
  if (options.locateWasm) {
    const locate = options.locateWasm;
    return HighmapFactory({
      locateFile: (filename: string) => String(locate(filename)),
    });
  }
  return defaultFactory();
}
