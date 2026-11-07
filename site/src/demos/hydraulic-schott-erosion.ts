import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "hydraulic-schott-erosion",
    title: "Hydraulic erosion (Schott, erosion only)",
    description:
      "The erosion-only Schott pass applies stream-power incision without thermal relaxation or deposition, sharpening flow paths across the remapped fbm while the broader landforms survive.",
    family: "erosion",
    features: ["noiseFbm", "remap", "hydraulicSchottErosion"],
    grid: GRID,
    seed: 2,
    palette: "arid",
    relief: 0.18,
    source: {
      example: "examples/ex_hydraulic_schott/ex_hydraulic_schott.cpp",
      panel: "z2 erosion-only panel (hydraulic_schott_erosion)",
      referenceImage: "docs/images/ex_hydraulic_schott.png",
    },
    adaptationNote:
      "Web-sized budget: 20 iterations; the example uses 30. Grid 256², seed 2, and all other Schott erosion parameters are unchanged.",
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [2, 2], 2);
    try {
      hmap.remap(z);
      hmap.hydraulicSchottErosion(z, { iterations: 20 });
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
