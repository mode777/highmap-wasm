import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "thermal-conserve",
    title: "Thermal erosion (conserve)",
    description:
      "Mass-conserving thermal erosion redistributes material downslope instead of removing it, smoothing the fbm surface while keeping its total elevation.",
    family: "erosion",
    features: ["noiseFbm", "thermalConserve"],
    grid: GRID,
    seed: 1,
    palette: "temperate",
    relief: 0.18,
    source: {
      example: "examples/ex_thermal/ex_thermal.cpp",
      panel: "second result panel (thermal_conserve)",
      referenceImage: "docs/images/ex_thermal.png",
    },
    adaptationNote:
      "Uses 100 iterations instead of the example's 500 to keep the web specimen within a few seconds; the 256² grid and 0.1/256 talus are unchanged.",
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [4, 4], 1);
    try {
      hmap.thermalConserve(z, 0.1 / 256, { iterations: 100 });
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
