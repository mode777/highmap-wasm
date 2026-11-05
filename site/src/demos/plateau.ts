import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "plateau",
    title: "Plateau",
    description:
      "A plateau filter flattens the upper terrain into stepped tablelands, leaving steep escarpments between the terraces.",
    family: "shaping",
    features: ["noiseFbm", "plateau"],
    grid: GRID,
    seed: 1,
    palette: "arid",
    relief: 0.22,
    source: {
      example: "examples/ex_plateau/ex_plateau.cpp",
      panel: "single plateau panel",
      referenceImage: "docs/images/ex_plateau.png",
    },
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [4, 4], 1);
    try {
      hmap.plateau(z, 64, 4);
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
