import { execFileSync } from "node:child_process";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";

const ROOT = fileURLToPath(new URL("..", import.meta.url));

interface PackFile {
  path: string;
}

interface PackResult {
  files: PackFile[];
}

describe("showcase package boundary", () => {
  it("does not ship the site in the npm tarball", () => {
    const npm = process.platform === "win32" ? "npm.cmd" : "npm";
    const stdout = execFileSync(
      npm,
      ["pack", "--dry-run", "--json", "--ignore-scripts"],
      { cwd: ROOT, encoding: "utf8", shell: process.platform === "win32" },
    );
    const result = JSON.parse(stdout) as PackResult[];
    const paths = result[0]?.files.map((file) => file.path) ?? [];

    expect(paths.some((path) => path.startsWith("site/"))).toBe(false);
    expect(paths).toContain("dist/index.js");
    expect(paths).toContain("README.md");
    expect(paths).toContain("LICENSE");
    expect(paths).toContain("THIRD_PARTY_LICENSES.md");
  });
});
