import { existsSync, readFileSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { afterEach, beforeAll, describe, expect, it } from "vitest";
import { initHighmap, type Highmap } from "../src/index.ts";
import { demos, FAMILIES } from "../site/src/demos/index.js";
import { demoSources } from "../site/src/demos/sources.js";
import { getPalette, type PaletteId } from "../site/src/terrain/palette.js";
import { buildDiorama } from "../site/src/terrain/geometry.js";

const ROOT = fileURLToPath(new URL("..", import.meta.url));

let hmap: Highmap;

beforeAll(async () => {
  hmap = await initHighmap();
});

// The recipes are synchronous and CPU-bound; yield between tests so Vitest can
// flush its task-update RPC messages instead of timing out on a long run.
afterEach(async () => {
  await new Promise((resolve) => setTimeout(resolve, 0));
});

describe("showcase catalog contract", () => {
  it("has twenty-four unique, kebab-case entries in known families", () => {
    expect(demos).toHaveLength(24);
    const ids = new Set<string>();
    for (const demo of demos) {
      expect(demo.meta.id).toMatch(/^[a-z0-9-]+$/);
      expect(ids.has(demo.meta.id)).toBe(false);
      ids.add(demo.meta.id);
      expect(FAMILIES).toContain(demo.meta.family);
      expect(demo.meta.title.length).toBeGreaterThan(0);
      expect(demo.meta.description.length).toBeGreaterThan(0);
      expect(demo.meta.features.length).toBeGreaterThan(0);
      expect(demo.meta.grid[0]).toBeGreaterThan(1);
      expect(demo.meta.grid[1]).toBeGreaterThan(1);
      expect(demo.meta.relief).toBeGreaterThan(0);
      expect(demo.meta.source.panel.length).toBeGreaterThan(0);
      expect(demo.meta.source.example).toMatch(/^examples\//);
      expect(existsSync(resolve(ROOT, demo.meta.source.example))).toBe(true);
      if (demo.meta.source.referenceImage) {
        expect(existsSync(resolve(ROOT, demo.meta.source.referenceImage))).toBe(
          true,
        );
      }
      expect(demo.meta.palette).toMatch(/^(temperate|arid|volcanic|arctic)$/);
      expect(() => getPalette(demo.meta.palette as PaletteId)).not.toThrow();
    }
  });

  it("shows the exact demo module source in the code tab", () => {
    const sources = demoSources();
    for (const demo of demos) {
      const file = readFileSync(
        resolve(ROOT, `site/src/demos/${demo.meta.id}.ts`),
        "utf8",
      );
      expect(sources[demo.meta.id]).toBe(file);
    }
  });

  it("declares only functions exposed by the initialized API", () => {
    const keys = new Set(Object.keys(hmap));
    for (const demo of demos) {
      for (const feature of demo.meta.features) {
        expect(keys.has(feature), `${demo.meta.id}: ${feature}`).toBe(true);
      }
    }
  });

  it("covers every CPU thermal variant", () => {
    const thermalFeatures = [
      "thermal",
      "thermalAutoBedrock",
      "thermalConserve",
      "thermalFlatten",
      "thermalOlsen",
      "thermalRib",
      "thermalInflate",
      "thermalRidge",
      "thermalScree",
      "thermalSchott",
    ];
    const declared = new Set(
      demos.flatMap((demo) => demo.meta.features as readonly string[]),
    );
    for (const feature of thermalFeatures) {
      expect(declared.has(feature), `missing thermal variant: ${feature}`).toBe(
        true,
      );
    }
  });

  it("covers every CPU hydraulic variant", () => {
    const hydraulicFeatures = [
      "hydraulicMcdonald",
      "hydraulicMcdonaldMultiscale",
      "hydraulicSchott",
      "hydraulicSchottErosion",
      "hydraulicVpipes",
    ];
    const declared = new Set(
      demos.flatMap((demo) => demo.meta.features as readonly string[]),
    );
    for (const feature of hydraulicFeatures) {
      expect(
        declared.has(feature),
        `missing hydraulic variant: ${feature}`,
      ).toBe(true);
    }
  });

  it("does not show hydraulicMusgrave while it is numerically unstable", () => {
    for (const demo of demos) {
      expect(demo.meta.features).not.toContain("hydraulicMusgrave");
      expect(demo.meta.source.example).not.toContain("hydraulic_musgrave");
    }
  });

  it("records the stream-erosion resolution adaptation", () => {
    const stream = demos.find((demo) => demo.meta.id === "stream-erosion");
    expect(stream).toBeDefined();
    expect(stream?.meta.grid).toEqual([256, 256]);
    expect(stream?.meta.adaptationNote).toContain("1024");
    expect(stream?.meta.adaptationNote).toContain("5/256");
  });
});

describe("showcase terrain recipes", () => {
  for (const demo of demos) {
    it(`${demo.meta.id} builds deterministically to a finite range`, () => {
      const first = demo.build(hmap);
      const second = demo.build(hmap);

      expect(first.shape).toEqual(demo.meta.grid);
      expect(first.data.length).toBe(demo.meta.grid[0] * demo.meta.grid[1]);
      expect(
        Buffer.from(first.data.buffer).equals(Buffer.from(second.data.buffer)),
      ).toBe(true);

      let min = Infinity;
      let max = -Infinity;
      for (const value of first.data) {
        expect(Number.isFinite(value)).toBe(true);
        if (value < min) min = value;
        if (value > max) max = value;
      }
      expect(min).toBeLessThan(max);
    });
  }
});

describe("showcase diorama geometry", () => {
  for (const demo of demos) {
    it(`${demo.meta.id} builds a closed specimen block`, () => {
      const heightmap = demo.build(hmap);
      const geometry = buildDiorama(heightmap, {
        palette: demo.meta.palette,
        relief: demo.meta.relief,
      });

      expect(geometry.baseY).toBeLessThan(geometry.topMin);
      expect(geometry.topMax).toBeGreaterThan(geometry.topMin);
      expect(geometry.totalTriangles).toBe(
        geometry.top.triangleCount + geometry.shell.triangleCount,
      );

      const [ni, nj] = demo.meta.grid;
      expect(geometry.top.triangleCount).toBe((ni - 1) * (nj - 1) * 2);
      const perimeterEdges = 2 * (ni + nj) - 4;
      expect(geometry.shell.triangleCount).toBeGreaterThan(perimeterEdges * 2);

      for (const value of geometry.top.positions) {
        expect(Number.isFinite(value)).toBe(true);
      }
      for (const value of geometry.shell.positions) {
        expect(Number.isFinite(value)).toBe(true);
      }

      const side = getPalette(demo.meta.palette).side;
      const sideVertexCount = geometry.shell.positions.length / 3 - 4;
      for (let vertex = 0; vertex < sideVertexCount; vertex += 1) {
        expect(geometry.shell.colors[vertex * 3]).toBeCloseTo(side[0], 6);
        expect(geometry.shell.colors[vertex * 3 + 1]).toBeCloseTo(side[1], 6);
        expect(geometry.shell.colors[vertex * 3 + 2]).toBeCloseTo(side[2], 6);
      }

      let topMin = Infinity;
      let topMax = -Infinity;
      for (let index = 1; index < geometry.top.positions.length; index += 3) {
        const y = geometry.top.positions[index]!;
        if (y < topMin) topMin = y;
        if (y > topMax) topMax = y;
      }
      expect(topMin).toBeGreaterThanOrEqual(geometry.baseY);
      expect(topMax).toBeLessThanOrEqual(geometry.topMax + 1e-6);

      const last = geometry.shell.indices.slice(-6);
      for (const index of last) {
        expect(geometry.shell.positions[index * 3 + 1]).toBeCloseTo(
          geometry.baseY,
          6,
        );
      }
    });
  }
});
