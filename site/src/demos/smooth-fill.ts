import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "smooth-fill",
    title: "Smooth fill",
    description:
      "A radius-32 smooth fill raises closed depressions toward their surroundings, turning pitted noise into broad, flat valley floors.",
    family: "shaping",
    features: ["noiseFbm", "smoothFill"],
    grid: GRID,
    seed: 1,
    palette: "temperate",
    relief: 0.16,
    source: {
      example: "examples/ex_smooth_fill/ex_smooth_fill.cpp",
      panel: "single smooth-fill panel",
      referenceImage: "docs/images/ex_smooth_fill.png",
    },
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [2, 2], 1);
    try {
      hmap.smoothFill(z, 32, { k: 0.1 });
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
