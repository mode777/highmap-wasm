import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "thermal-auto-bedrock",
    title: "Thermal erosion (auto bedrock)",
    description:
      "Auto-bedrock thermal erosion clamps each cell to the highest level it reaches, filling low ground and smoothing fbm terrain without cutting below its starting surface.",
    family: "erosion",
    features: ["noiseFbm", "thermalAutoBedrock"],
    grid: GRID,
    seed: 1,
    palette: "volcanic",
    relief: 0.2,
    source: {
      example:
        "examples/ex_thermal_auto_bedrock/ex_thermal_auto_bedrock.cpp",
      panel: "result panel (thermal_auto_bedrock)",
      referenceImage: "docs/images/ex_thermal_auto_bedrock.png",
    },
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [2, 2], 1);
    try {
      hmap.thermalAutoBedrock(z, 0.5 / 256, { iterations: 500 });
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
