import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "thermal-inflate",
    title: "Thermal erosion (inflate)",
    description:
      "Thermal inflation deposits material on low-lying cells, inflating valleys and basins into rounded, bulbous landforms.",
    family: "erosion",
    features: ["noiseFbm", "remap", "thermalInflate"],
    grid: GRID,
    seed: 1,
    palette: "volcanic",
    relief: 0.2,
    source: {
      example: "examples/ex_thermal_ridge/ex_thermal_ridge.cpp",
      panel: "thermal_inflate panel",
      referenceImage: "docs/images/ex_thermal_ridge.png",
    },
    adaptationNote:
      "Ported at 256² with talus 2/256; the example uses 1024² and 2/1024. The 500 iterations and noise parameters are unchanged.",
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [4, 4], 1);
    try {
      hmap.remap(z);
      hmap.thermalInflate(z, 2 / 256, { iterations: 500 });
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
