import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "thermal-schott",
    title: "Thermal erosion (Schott)",
    description:
      "Thermal Schott erosion uses a per-cell talus map aligned with elevation, relaxing steep slopes while preserving the broad fbm landforms.",
    family: "erosion",
    features: ["noiseFbm", "remap", "scale", "thermalSchott"],
    grid: GRID,
    seed: 1,
    palette: "temperate",
    relief: 0.18,
    source: {
      example: "examples/ex_thermal_schott/ex_thermal_schott.cpp",
      panel: "elevation-aligned talus panel (thermal_schott)",
      referenceImage: "docs/images/ex_thermal_schott.png",
    },
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [2, 2], 1);
    const talusMap = hmap.scale(z, 1);
    try {
      hmap.remap(z);
      hmap.remap(talusMap, { vmin: 1 / 256, vmax: 2 / 256 });
      hmap.thermalSchott(z, talusMap, { iterations: 500 });
      return copyOut(z);
    } finally {
      z.free();
      talusMap.free();
    }
  },
};

export default demo;
