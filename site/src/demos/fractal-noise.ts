import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "fractal-noise",
    title: "Fractal noise",
    description:
      "Eight-octave Perlin fbm remapped to the full value range. The baseline landscape: broad basins, soft ridges, and no erosion.",
    family: "noise",
    features: ["noiseFbm", "remap"],
    grid: GRID,
    seed: 1,
    palette: "temperate",
    relief: 0.18,
    source: {
      example: "examples/ex_noise_fbm/ex_noise_fbm.cpp",
      panel: "fbm banner, PERLIN column",
      referenceImage: "docs/images/ex_noise_fbm0.png",
    },
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [4, 4], 1);
    try {
      hmap.remap(z);
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
