import { DistanceFunction, NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "falloff",
    title: "Falloff",
    description:
      "Fractal terrain multiplied by a Euclidean distance falloff, pulling the landmass toward the centre and drowning the borders.",
    family: "shaping",
    features: ["noiseFbm", "remap", "falloff"],
    grid: GRID,
    seed: 1,
    palette: "temperate",
    relief: 0.2,
    source: {
      example: "examples/ex_falloff/ex_falloff.cpp",
      panel: "Euclidian distance panel",
      referenceImage: "docs/images/ex_falloff.png",
    },
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [4, 4], 1);
    try {
      hmap.remap(z);
      hmap.falloff(z, {
        strength: 1,
        distFct: DistanceFunction.EUCLIDIAN,
      });
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
