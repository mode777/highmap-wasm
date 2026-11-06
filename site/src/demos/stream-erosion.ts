import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "stream-erosion",
    title: "Stream erosion",
    description:
      "Hydraulic stream erosion carves dendritic channels into remapped fbm. The example's first panel, scaled from 1024² to a web-sized grid.",
    family: "erosion",
    features: ["noiseFbm", "remap", "hydraulicStream"],
    grid: GRID,
    seed: 1,
    palette: "temperate",
    relief: 0.2,
    source: {
      example: "examples/ex_hydraulic_stream/ex_hydraulic_stream.cpp",
      panel: "first hydraulic_stream panel",
      referenceImage: "docs/images/ex_hydraulic_stream0.png",
    },
    adaptationNote:
      "Ported at 256² with talusRef 5/256; the example uses 1024² and 5/1024. cErosion, ir, and clippingRatio are unchanged.",
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [2, 2], 1);
    try {
      hmap.remap(z);
      hmap.hydraulicStream(z, 0.1, 5 / 256, {
        ir: 1,
        clippingRatio: 10,
      });
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
