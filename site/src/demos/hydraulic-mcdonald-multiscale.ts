import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [512, 512];

const demo: DemoDefinition = {
  meta: {
    id: "hydraulic-mcdonald-multiscale",
    title: "Hydraulic erosion (McDonald, multiscale)",
    description:
      "The multiscale McDonald solver runs the same rainfall-driven particle erosion over a coarse-to-fine resolution ladder, resolving broad valleys first and carving fine channels at each finer level.",
    family: "erosion",
    features: ["noiseFbm", "remap", "hydraulicMcdonaldMultiscale"],
    grid: GRID,
    seed: 1,
    palette: "arid",
    relief: 0.2,
    source: {
      example: "examples/ex_hydraulic_mcdonald/ex_hydraulic_mcdonald.cpp",
      panel: "zm multiscale panel (hydraulic_mcdonald_multiscale)",
      referenceImage: "docs/images/ex_hydraulic_mcdonald3.png",
    },
    adaptationNote:
      "Web-sized budget: 512 samples and maxage 32; the example uses the defaults 8192 samples and maxage 512. Grid 512², seed 1, the [256, 128, 64] step ladder, and all other McDonald parameters are unchanged.",
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [2, 2], 1);
    try {
      hmap.remap(z);
      hmap.hydraulicMcdonaldMultiscale(z, 1, {
        stepsPerLevel: [256, 128, 64],
        samples: 512,
        maxage: 32,
      });
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
