import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "thermal-flatten",
    title: "Thermal erosion (flatten)",
    description:
      "Thermal flattening relaxes slopes below and above the talus threshold, turning fbm terrain into broad, rounded landforms with softened crests.",
    family: "erosion",
    features: ["noiseFbm", "thermalFlatten"],
    grid: GRID,
    seed: 1,
    palette: "arid",
    relief: 0.18,
    source: {
      example: "examples/ex_thermal_flatten/ex_thermal_flatten.cpp",
      panel: "second result panel (sigma_sup = 0.05)",
      referenceImage: "docs/images/ex_thermal_flatten.png",
    },
    adaptationNote:
      "Uses 100 iterations instead of the example's 500 to keep the web specimen within a few seconds; the 256² grid, 1.5/256 talus, sigma_inf, and sigma_sup are unchanged.",
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [2, 2], 1);
    try {
      hmap.thermalFlatten(z, 1.5 / 256, {
        iterations: 100,
        sigmaInf: 0.5,
        sigmaSup: 0.05,
      });
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
