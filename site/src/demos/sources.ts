import falloff from "./falloff.ts?raw";
import fractalNoise from "./fractal-noise.ts?raw";
import pingpongNoise from "./pingpong-noise.ts?raw";
import plateau from "./plateau.ts?raw";
import ridgedNoise from "./ridged-noise.ts?raw";
import smoothFill from "./smooth-fill.ts?raw";
import streamErosion from "./stream-erosion.ts?raw";
import warp from "./warp.ts?raw";
import worleyNoise from "./worley-noise.ts?raw";

const sources: Record<string, string> = {
  "fractal-noise": fractalNoise,
  "pingpong-noise": pingpongNoise,
  "ridged-noise": ridgedNoise,
  "worley-noise": worleyNoise,
  warp: warp,
  falloff: falloff,
  plateau: plateau,
  "smooth-fill": smoothFill,
  "stream-erosion": streamErosion,
};

export function getDemoSource(id: string): string {
  return sources[id] ?? "";
}

export function demoSources(): Record<string, string> {
  return sources;
}
