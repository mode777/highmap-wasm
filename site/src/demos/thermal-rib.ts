import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "thermal-rib",
    title: "Thermal erosion (rib)",
    description:
      "Thermal rib erosion lowers each cell by its shallowest neighbouring slope, etching fine radial ribs into the fbm surface.",
    family: "erosion",
    features: ["noiseFbm", "thermalRib"],
    grid: GRID,
    seed: 1,
    palette: "temperate",
    relief: 0.16,
    source: {
      example: "examples/ex_thermal_rib/ex_thermal_rib.cpp",
      panel: "result panel (thermal_rib)",
      referenceImage: "docs/images/ex_thermal_rib.png",
    },
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [2, 2], 1);
    try {
      hmap.thermalRib(z, { iterations: 5 });
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
