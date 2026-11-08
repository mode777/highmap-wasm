import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "rifts",
    title: "Rifts",
    description:
      "A directional Voronoi rift network carves smooth-bottomed, steep-walled valleys across fbm terrain, oriented by the [8, 1.5] frequency and 30° angle.",
    family: "erosion",
    features: ["noiseFbm", "remap", "rifts"],
    grid: GRID,
    seed: 0,
    palette: "volcanic",
    relief: 0.2,
    source: {
      example: "examples/ex_rifts/ex_rifts.cpp",
      panel: "post-rifts `z2` banner panel",
      referenceImage: "docs/images/ex_rifts.png",
    },
    adaptationNote:
      "The example computes 1 + fbm, then zeroed_edges, then remap; the wasm API does not expose zeroed_edges, so that pre-pass is omitted and no substitute is applied, and the constant offset is subsumed by the remap. Grid 256², base seed 0, the rifts parameters (kw [8, 1.5], angle 30, amplitude 0.1, rifts seed 1), the default rifts options, and the post-rifts remap are unchanged.",
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [4, 4], 0);
    try {
      hmap.remap(z);
      hmap.rifts(z, [8, 1.5], 30, 0.1, 1);
      hmap.remap(z);
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
