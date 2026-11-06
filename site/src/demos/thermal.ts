import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "thermal",
    title: "Thermal erosion",
    description:
      "Thermal erosion relaxes slopes above the talus angle, settling fbm terrain into smooth, rounded hillsides while leaving the broader landforms intact.",
    family: "erosion",
    features: ["noiseFbm", "thermal"],
    grid: GRID,
    seed: 1,
    palette: "arid",
    relief: 0.18,
    source: {
      example: "examples/ex_thermal/ex_thermal.cpp",
      panel: "first result panel (thermal)",
      referenceImage: "docs/images/ex_thermal.png",
    },
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [4, 4], 1);
    try {
      hmap.thermal(z, 0.1 / 256, { iterations: 500 });
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
