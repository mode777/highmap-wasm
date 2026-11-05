import { createHash } from "node:crypto";
import { readFileSync, statSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { demoIds } from "../site/src/demos/index.js";

const ROOT = fileURLToPath(new URL("..", import.meta.url));
const PREVIEW_DIR = resolve(ROOT, "site/src/assets/previews");

interface PreviewEntry {
  file: string;
  sourceHash: string;
  width: number;
  height: number;
}

interface PreviewManifest {
  previewVersion: number;
  viewport: { width: number; height: number; deviceScaleFactor: number };
  quality: number;
  demos: Record<string, PreviewEntry>;
}

function hashSource(id: string): string {
  const source = readFileSync(resolve(ROOT, `site/src/demos/${id}.ts`));
  return createHash("sha256").update(source).digest("hex");
}

describe("showcase previews", () => {
  const manifest = JSON.parse(
    readFileSync(resolve(PREVIEW_DIR, "manifest.json"), "utf8"),
  ) as PreviewManifest;

  it("covers every catalog entry and no orphan entries", () => {
    expect(Object.keys(manifest.demos).sort()).toEqual([...demoIds()].sort());
  });

  it("records the expected capture contract", () => {
    expect(manifest.previewVersion).toBe(3);
    expect(manifest.viewport).toEqual({
      width: 640,
      height: 400,
      deviceScaleFactor: 2,
    });
    expect(manifest.quality).toBe(82);
  });

  for (const id of demoIds()) {
    it(`${id} has a non-empty, current preview`, () => {
      const entry = manifest.demos[id];
      expect(entry).toBeDefined();
      expect(entry?.file).toBe(`${id}.webp`);
      expect(entry?.width).toBe(1280);
      expect(entry?.height).toBe(800);
      expect(entry?.sourceHash).toBe(hashSource(id));

      const file = resolve(PREVIEW_DIR, entry!.file);
      expect(statSync(file).size).toBeGreaterThan(0);
    });
  }
});
