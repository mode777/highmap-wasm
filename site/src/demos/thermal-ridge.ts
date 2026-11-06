import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "thermal-ridge",
    title: "Thermal erosion (ridge)",
    description:
      "Thermal ridge erosion redistributes material along steep crests, carving sharp, branching ridge lines into remapped fbm terrain.",
    family: "erosion",
    features: ["noiseFbm", "remap", "thermalRidge"],
    grid: GRID,
    seed: 1,
    palette: "arid",
    relief: 0.2,
    source: {
      example: "examples/ex_thermal_ridge/ex_thermal_ridge.cpp",
      panel: "thermal_ridge panel",
      referenceImage: "docs/images/ex_thermal_ridge.png",
    },
    adaptationNote:
      "Ported at 256² with talus 2/256; the example uses 1024² and 2/1024. The 500 iterations and noise parameters are unchanged.",
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [4, 4], 1);
    try {
      hmap.remap(z);
      hmap.thermalRidge(z, 2 / 256, { iterations: 500 });
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
