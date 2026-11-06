import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "thermal-scree",
    title: "Thermal erosion (scree)",
    description:
      "Thermal scree deposition fills slopes with fine talus material up to the zmax limit, giving remapped fbm a granular, scree-covered surface.",
    family: "erosion",
    features: ["noiseFbm", "remap", "thermalScree"],
    grid: GRID,
    seed: 1,
    palette: "arctic",
    relief: 0.2,
    source: {
      example: "examples/ex_thermal_ridge/ex_thermal_ridge.cpp",
      panel: "thermal_scree panel",
      referenceImage: "docs/images/ex_thermal_ridge.png",
    },
    adaptationNote:
      "Ported at 256² with talus 2/256; the example uses 1024² and 2/1024. The 0.5 zmax, 500 iterations, and noise parameters are unchanged.",
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [4, 4], 1);
    try {
      hmap.remap(z);
      hmap.thermalScree(z, 2 / 256, 0.5, { iterations: 500 });
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
