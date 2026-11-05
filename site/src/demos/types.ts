import type { Highmap } from "highmap-wasm";
import type { PaletteId } from "../terrain/palette.js";

export type TerrainFamily = "noise" | "shaping" | "erosion";

/** Heightmap returned by a demo recipe, independent of wasm wrappers. */
export interface Heightmap {
  data: Float32Array;
  shape: [number, number];
}

/** Provenance for one panel or variant of a HighMap example. */
export interface DemoSource {
  /** Repository-relative path to the C++ example. */
  example: string;
  /** Panel, banner column, or variant ported by this demo. */
  panel: string;
  /** Optional repository-relative path to the example's reference image. */
  referenceImage?: string;
}

/** Metadata shared by the gallery, detail page, and preview manifest. */
export interface DemoMeta {
  id: string;
  title: string;
  description: string;
  family: TerrainFamily;
  features: readonly string[];
  grid: readonly [number, number];
  seed: number;
  palette: PaletteId;
  /** Vertical scale of the terrain in tile units. */
  relief: number;
  source: DemoSource;
  /** Recorded when the source example's grid or parameters were adapted. */
  resolutionNote?: string;
}

/** A pure recipe module: metadata plus the builder the worker executes. */
export interface DemoDefinition {
  meta: DemoMeta;
  build: (hmap: Highmap) => Heightmap;
}
