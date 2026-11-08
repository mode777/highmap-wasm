import { NoiseType, type HArray } from "highmap-wasm";
import { copyOut } from "./helpers.js";
import type { DemoDefinition } from "./types.js";

const GRID: [number, number] = [256, 256];

const demo: DemoDefinition = {
  meta: {
    id: "advection-particle",
    title: "Particle advection",
    description:
      "Ten thousand particles walk the elevation's downhill gradient for ten passes, dragging a low-frequency fbm field into stretched, flow-like deposits.",
    family: "shaping",
    features: ["noiseFbm", "remap", "advectionParticle"],
    grid: GRID,
    seed: 1,
    palette: "arid",
    relief: 0.2,
    source: {
      example: "examples/ex_advection_particle/ex_advection_particle.cpp",
      panel: "advected field `n` banner panel",
      referenceImage: "docs/images/ex_advection_particle.png",
    },
  },
  build(hmap) {
    const z = hmap.noiseFbm(NoiseType.PERLIN, GRID, [2, 2], 1);
    const n0 = hmap.noiseFbm(NoiseType.PERLIN, GRID, [32, 32], 1);
    let out: HArray | undefined;
    try {
      hmap.remap(z);
      out = hmap.advectionParticle(z, n0, 10000, 1, { iterations: 10 });
      return copyOut(out);
    } finally {
      out?.free();
      n0.free();
      z.free();
    }
  },
};

export default demo;
