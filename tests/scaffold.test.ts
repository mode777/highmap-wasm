import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const pkg = JSON.parse(
  readFileSync(require.resolve("../package.json"), "utf8"),
) as Record<string, unknown>;

describe("package scaffold", () => {
  it("is a private ESM package with no runtime dependencies", () => {
    expect(pkg["type"]).toBe("module");
    expect(pkg["private"]).toBe(true);
    expect(pkg["dependencies"]).toBeUndefined();
  });

  it("ships only the dist artifacts and license files", () => {
    expect(pkg["files"]).toEqual([
      "dist",
      "README.md",
      "LICENSE",
      "THIRD_PARTY_LICENSES.md",
    ]);
  });

  it("requires Node >= 18", () => {
    expect(pkg["engines"]).toEqual({ node: ">=18" });
  });
});
