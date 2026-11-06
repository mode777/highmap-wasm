import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "thermal-olsen",
    title: "Thermal erosion (Olsen)",
    description:
      "Thermal Olsen erosion redistributes material between neighbouring cells in a position-hashed order, smoothing steep fbm slopes into rounded landforms.",
    family: "erosion",
    features: ["noiseFbm", "remap", "thermalOlsen"],
    grid: GRID,
    seed: 0,
    palette: "arctic",
    relief: 0.2,
    source: {
      example: "examples/ex_thermal_olsen/ex_thermal_olsen.cpp",
      panel: "result panel (thermal_olsen)",
      referenceImage: "docs/images/ex_thermal_olsen.png",
    },
    adaptationNote:
      "Ported at 256² with talus 4/256; the example uses 1024² and 4/1024. Uses 100 iterations instead of 500 to keep the web specimen within a few seconds.",
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [4, 4], 0);
    try {
      hmap.remap(z);
      hmap.thermalOlsen(z, 4 / 256, { iterations: 100 });
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
