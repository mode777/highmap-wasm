import { NoiseType, type HArray } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "advection-warp",
    title: "Advection warp",
    description:
      "The terrain warps along its own gradient with fading memory, stretching fbm landforms into downhill-transported ridges and hollows.",
    family: "shaping",
    features: ["noiseFbm", "remap", "advectionWarp"],
    grid: GRID,
    seed: 1,
    palette: "temperate",
    relief: 0.2,
    source: {
      example: "examples/ex_advection_warp/ex_advection_warp.cpp",
      panel: "warped terrain `za` banner panel",
      referenceImage: "docs/images/ex_advection_warp.png",
    },
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [2, 2], 1);
    let out: HArray | undefined;
    try {
      hmap.remap(z);
      out = hmap.advectionWarp(z, z, {
        advectionLength: 0.05,
        valuePersistence: 0.96,
      });
      return copyOut(out);
    } finally {
      out?.free();
      z.free();
    }
  },
};

export default demo;
