import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "worley-noise",
    title: "Worley noise",
    description:
      "Cellular Worley noise remapped into a cratered field. The cell boundaries become ridges around broad, bowl-shaped depressions.",
    family: "noise",
    features: ["noise", "remap"],
    grid: GRID,
    seed: 1,
    palette: "volcanic",
    relief: 0.18,
    source: {
      example: "examples/ex_noise/ex_noise.cpp",
      panel: "noise banner, WORLEY column",
    },
  },
  build(hmap) {
    const z = hmap.noise(NoiseType.WORLEY, GRID, [4, 4], 1);
    try {
      hmap.remap(z);
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
