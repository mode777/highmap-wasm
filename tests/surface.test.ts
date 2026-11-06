import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { beforeAll, describe, expect, it } from "vitest";
import { initHighmap } from "../src/index.ts";

const require = createRequire(import.meta.url);
const manifest = readFileSync(
  require.resolve("../HighMap/wasm/manifest.md"),
  "utf8",
);

/**
 * JS names the high-level API must expose, parsed from the manifest's
 * Functions table. Alias rows (`clampValues` (JS: `clamp`)) map to the alias;
 * rows the API reaches through options or overloads (scalar algebra variants,
 * the moisture overload, the raw `makeArray`) are excluded here and covered
 * by their own tests.
 */
function manifestApiFunctionNames(): string[] {
  const skip = new Set([
    "makeArray", // exposed as makeGrid
    "addScalar",
    "subtractScalar",
    "multiplyScalar",
    "divideScalar",
    "hydraulicMusgraveMoisture", // reachable via hydraulicMusgrave options
  ]);
  const names = new Set<string>();

  for (const line of manifest.split(/\r?\n/)) {
    const m = line.match(/^\|([^|]*)\|/);
    if (!m) continue;
    const cell = m[1] ?? "";

    // alias form first: `bindingName` (JS: `apiName`)
    const alias = cell.match(/`[A-Za-z]+`\s*\(JS:\s*`([A-Za-z]+)`\)/);
    if (alias) {
      names.add(alias[1]!);
      continue;
    }

    for (const t of cell.matchAll(/`([A-Za-z]+)`/g)) {
      const name = t[1]!;
      if (!skip.has(name)) names.add(name);
    }
  }
  return [...names].sort();
}

let hmap: Awaited<ReturnType<typeof initHighmap>>;

beforeAll(async () => {
  hmap = await initHighmap();
});

describe("binding manifest is the source of truth (AD-008)", () => {
  it("exposes every manifest function", () => {
    expect(manifestApiFunctionNames()).toEqual([
      "add",
      "clamp",
      "divide",
      "falloff",
      "hillshade",
      "hydraulicMcdonald",
      "hydraulicMcdonaldMultiscale",
      "hydraulicMusgrave",
      "hydraulicSchott",
      "hydraulicSchottErosion",
      "hydraulicStream",
      "hydraulicVpipes",
      "multiply",
      "noise",
      "noiseFbm",
      "noisePingpong",
      "noiseRidged",
      "plateau",
      "remap",
      "scale",
      "smoothFill",
      "subtract",
      "thermal",
      "thermalAutoBedrock",
      "thermalConserve",
      "thermalFlatten",
      "thermalInflate",
      "thermalOlsen",
      "thermalRib",
      "thermalRidge",
      "thermalSchott",
      "thermalScree",
      "warp",
    ]);

    for (const name of manifestApiFunctionNames()) {
      expect(
        typeof (hmap as unknown as Record<string, unknown>)[name],
        name,
      ).toBe("function");
    }
    expect(typeof hmap.makeGrid).toBe("function");
  });

  it("exposes the manifest enums", () => {
    const enumKeys = (e: Record<string, unknown>) =>
      Object.keys(e).filter((k) => /^[A-Z0-9_]+$/.test(k));

    expect(enumKeys(hmap.NoiseType)).toEqual([
      "PARBERRY",
      "PERLIN",
      "PERLIN_BILLOW",
      "PERLIN_HALF",
      "SIMPLEX2",
      "SIMPLEX2S",
      "VALUE",
      "VALUE_CUBIC",
      "WORLEY",
      "WORLEY_DOUBLE",
      "WORLEY_VALUE",
    ]);
    expect(enumKeys(hmap.DistanceFunction)).toEqual([
      "CHEBYSHEV",
      "EUCLIDIAN",
      "EUCLISHEV",
      "MANHATTAN",
    ]);
  });
});
