import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "ridged-noise",
    title: "Ridged noise",
    description:
      "Ridged fbm turns Simplex noise into sharp crests and narrow valleys, the raw material for alpine terrain before any erosion.",
    family: "noise",
    features: ["noiseRidged", "remap"],
    grid: GRID,
    seed: 1,
    palette: "arctic",
    relief: 0.24,
    source: {
      example: "examples/ex_noise_fbm/ex_noise_fbm.cpp",
      panel: "ridged banner, SIMPLEX2S column",
      referenceImage: "docs/images/ex_noise_fbm4.png",
    },
  },
  build(hmap) {
    const z = hmap.noiseRidged(NoiseType.SIMPLEX2S, GRID, [4, 4], 1);
    try {
      hmap.remap(z);
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
