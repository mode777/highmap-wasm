import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [512, 512];

const demo: DemoDefinition = {
  meta: {
    id: "hydraulic-vpipes",
    title: "Hydraulic erosion (vpipes)",
    description:
      "The virtual-pipes model routes water through a shallow flow network, eroding channels and depositing sediment to carve branching drainage into the fbm terrain.",
    family: "erosion",
    features: ["noiseFbm", "remap", "hydraulicVpipes"],
    grid: GRID,
    seed: 2,
    palette: "volcanic",
    relief: 0.2,
    source: {
      example: "examples/ex_hydraulic_vpipes_gpu/ex_hydraulic_vpipes_gpu.cpp",
      panel: "post-erosion z panel (hydraulic_vpipes)",
      referenceImage: "docs/images/ex_hydraulic_vpipes_gpu.png",
    },
    adaptationNote:
      "The example applies bulkify(PRIM_CUBIC_PULSE, 1) before remap; the wasm API does not expose bulkify, so the pre-pass is omitted and no substitute is applied. Grid 512², seed 2, the fbm recipe (octaves 8, weight 0.7), and remap are unchanged. Web-sized budget: 30 iterations; the example uses the default 50.",
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [4, 4], 2, {
      octaves: 8,
      weight: 0.7,
    });
    try {
      hmap.remap(z);
      hmap.hydraulicVpipes(z, { iterations: 30 });
      return copyOut(z);
    } finally {
      z.free();
    }
  },
};

export default demo;
