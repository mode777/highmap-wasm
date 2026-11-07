import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "hydraulic-schott",
    title: "Hydraulic erosion (Schott)",
    description:
      "Schott's full hydraulic erosion alternates stream-power incision, thermal relaxation, and deposition, cutting valleys into the fbm terrain while smoothing their walls.",
    family: "erosion",
    features: ["noiseFbm", "remap", "hydraulicSchott"],
    grid: GRID,
    seed: 2,
    palette: "temperate",
    relief: 0.18,
    source: {
      example: "examples/ex_hydraulic_schott/ex_hydraulic_schott.cpp",
      panel: "z1 full-Schott panel (hydraulic_schott)",
      referenceImage: "docs/images/ex_hydraulic_schott.png",
    },
    adaptationNote:
      "Web-sized budget: 60 iterations; the example uses 400. Grid 256², seed 2, talus 2/256, and all other Schott parameters are unchanged.",
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [2, 2], 2);
    try {
      hmap.remap(z);
      hmap.hydraulicSchott(z, 2 / 256, { iterations: 60 });
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
