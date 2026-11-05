import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "pingpong-noise",
    title: "Pingpong noise",
    description:
      "The pingpong fractal folds Perlin billows back on themselves, producing a field of sharp repeated terraces and needle-like crests.",
    family: "noise",
    features: ["noisePingpong", "remap"],
    grid: GRID,
    seed: 1,
    palette: "arid",
    relief: 0.16,
    source: {
      example: "examples/ex_noise_fbm/ex_noise_fbm.cpp",
      panel: "pingpong banner, PERLIN_BILLOW column",
      referenceImage: "docs/images/ex_noise_fbm3.png",
    },
  },
  build(hmap) {
    const z = hmap.noisePingpong(NoiseType.PERLIN_BILLOW, GRID, [4, 4], 1);
    try {
      hmap.remap(z);
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
