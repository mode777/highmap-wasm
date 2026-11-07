import falloff from "./falloff.ts?raw";
import fractalNoise from "./fractal-noise.ts?raw";
import hydraulicMcdonald from "./hydraulic-mcdonald.ts?raw";
import hydraulicMcdonaldMultiscale from "./hydraulic-mcdonald-multiscale.ts?raw";
import hydraulicSchott from "./hydraulic-schott.ts?raw";
import hydraulicSchottErosion from "./hydraulic-schott-erosion.ts?raw";
import hydraulicVpipes from "./hydraulic-vpipes.ts?raw";
import pingpongNoise from "./pingpong-noise.ts?raw";
import plateau from "./plateau.ts?raw";
import ridgedNoise from "./ridged-noise.ts?raw";
import smoothFill from "./smooth-fill.ts?raw";
import streamErosion from "./stream-erosion.ts?raw";
import thermal from "./thermal.ts?raw";
import thermalAutoBedrock from "./thermal-auto-bedrock.ts?raw";
import thermalConserve from "./thermal-conserve.ts?raw";
import thermalFlatten from "./thermal-flatten.ts?raw";
import thermalInflate from "./thermal-inflate.ts?raw";
import thermalOlsen from "./thermal-olsen.ts?raw";
import thermalRib from "./thermal-rib.ts?raw";
import thermalRidge from "./thermal-ridge.ts?raw";
import thermalSchott from "./thermal-schott.ts?raw";
import thermalScree from "./thermal-scree.ts?raw";
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
  "hydraulic-mcdonald": hydraulicMcdonald,
  "hydraulic-mcdonald-multiscale": hydraulicMcdonaldMultiscale,
  "hydraulic-schott": hydraulicSchott,
  "hydraulic-schott-erosion": hydraulicSchottErosion,
  "hydraulic-vpipes": hydraulicVpipes,
  thermal: thermal,
  "thermal-conserve": thermalConserve,
  "thermal-auto-bedrock": thermalAutoBedrock,
  "thermal-rib": thermalRib,
  "thermal-flatten": thermalFlatten,
  "thermal-olsen": thermalOlsen,
  "thermal-ridge": thermalRidge,
  "thermal-inflate": thermalInflate,
  "thermal-scree": thermalScree,
  "thermal-schott": thermalSchott,
};

export function getDemoSource(id: string): string {
  return sources[id] ?? "";
}

export function demoSources(): Record<string, string> {
  return sources;
}
