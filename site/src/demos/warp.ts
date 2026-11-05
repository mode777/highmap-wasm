import { NoiseType } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "warp",
    title: "Warp",
    description:
      "Two independent fbm fields displace the base terrain by up to a quarter of the tile, stretching ridges into folded, directional landforms.",
    family: "shaping",
    features: ["noiseFbm", "remap", "warp"],
    grid: GRID,
    seed: 1,
    palette: "temperate",
    relief: 0.2,
    source: {
      example: "examples/ex_warp/ex_warp.cpp",
      panel: "single warped fbm panel",
      referenceImage: "docs/images/ex_warp.png",
    },
  },
  build(hmap) {
    const base = hmap.noiseFbm(NoiseType.PERLIN, GRID, [2, 2], 1);
    const dx = hmap.noiseFbm(NoiseType.PERLIN, GRID, [4, 4], 2);
    const dy = hmap.noiseFbm(NoiseType.PERLIN, GRID, [4, 4], 3);
    try {
      hmap.remap(dx, { vmin: -0.25, vmax: 0.25 });
      hmap.remap(dy, { vmin: -0.25, vmax: 0.25 });
      hmap.warp(base, dx, dy);
      return copyOut(base);
    } finally {
      base.free();
      dx.free();
      dy.free();
    }
  },
};

export default demo;
