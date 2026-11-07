import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "hydraulic-mcdonald",
    title: "Hydraulic erosion (McDonald)",
    description:
      "McDonald hydraulic erosion routes simulated rainfall across the surface, scouring channels and redepositing sediment as alluvial fans over the remapped fbm terrain.",
    family: "erosion",
    features: ["noiseFbm", "remap", "hydraulicMcdonald"],
    grid: GRID,
    seed: 1,
    palette: "temperate",
    relief: 0.2,
    source: {
      example: "examples/ex_hydraulic_mcdonald/ex_hydraulic_mcdonald.cpp",
      panel: "post-erosion z panel (hydraulic_mcdonald)",
      referenceImage: "docs/images/ex_hydraulic_mcdonald1.png",
    },
    adaptationNote:
      "Web-sized budget: 64 steps, 1024 samples, and maxage 64; the example uses 256 steps, 8192 samples, and maxage 512. Grid 256², seed 1, and all other McDonald parameters are unchanged. The reference is the post-erosion terrain panel, not the log-scaled discharge render.",
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [2, 2], 1);
    try {
      hmap.remap(z);
      hmap.hydraulicMcdonald(z, 64, 1, { samples: 1024, maxage: 64 });
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
