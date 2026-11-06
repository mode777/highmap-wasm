import falloff from "./falloff.js";
import fractalNoise from "./fractal-noise.js";
import pingpongNoise from "./pingpong-noise.js";
import plateau from "./plateau.js";
import ridgedNoise from "./ridged-noise.js";
import smoothFill from "./smooth-fill.js";
import streamErosion from "./stream-erosion.js";
import thermal from "./thermal.js";
import thermalAutoBedrock from "./thermal-auto-bedrock.js";
import thermalConserve from "./thermal-conserve.js";
import thermalFlatten from "./thermal-flatten.js";
import thermalInflate from "./thermal-inflate.js";
import thermalOlsen from "./thermal-olsen.js";
import thermalRib from "./thermal-rib.js";
import thermalRidge from "./thermal-ridge.js";
import thermalSchott from "./thermal-schott.js";
import thermalScree from "./thermal-scree.js";
import warp from "./warp.js";
import worleyNoise from "./worley-noise.js";
import type { DemoDefinition, TerrainFamily } from "./types.js";

export type DemoEntry = DemoDefinition;

export const FAMILIES = ["noise", "shaping", "erosion"] as const;

export const FAMILY_LABELS: Record<TerrainFamily, string> = {
  noise: "Noise",
  shaping: "Shaping",
  erosion: "Erosion",
};

export const demos: readonly DemoEntry[] = [
  fractalNoise,
  pingpongNoise,
  ridgedNoise,
  worleyNoise,
  warp,
  falloff,
  plateau,
  smoothFill,
  streamErosion,
  thermal,
  thermalConserve,
  thermalAutoBedrock,
  thermalRib,
  thermalFlatten,
  thermalOlsen,
  thermalRidge,
  thermalInflate,
  thermalScree,
  thermalSchott,
];

const byId = new Map(demos.map((demo) => [demo.meta.id, demo] as const));

export function getDemo(id: string): DemoEntry | undefined {
  return byId.get(id);
}

export function demoIds(): string[] {
  return demos.map((demo) => demo.meta.id);
}
