#!/usr/bin/env node
// Asserts that `npm pack --dry-run` ships only the allowlisted files
// (design D7, AD-031). package.json is always packed by npm and is part of the
// contract; everything else must live under dist/ or be a root notice file.
//
// Usage: node scripts/release-pack-check.mjs

import { spawnSync } from "node:child_process";
import { pathToFileURL } from "node:url";

const ROOT_FILES = new Set([
  "package.json",
  "README.md",
  "LICENSE",
  "THIRD_PARTY_LICENSES.md",
]);

/**
 * @param {string[]} paths
 * @returns {string[]} packed paths outside the allowlist
 */
export function unexpectedPackedPaths(paths) {
  return paths.filter(
    (path) => !path.startsWith("dist/") && !ROOT_FILES.has(path),
  );
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const result = spawnSync("npm", ["pack", "--dry-run", "--json"], {
    encoding: "utf8",
    shell: process.platform === "win32",
  });

  if (result.status !== 0) {
    process.stderr.write(result.stderr || result.stdout || "");
    process.exit(result.status ?? 1);
  }

  let pack;
  try {
    [pack] = JSON.parse(result.stdout);
  } catch {
    console.error("error: could not parse `npm pack --dry-run --json` output");
    console.error(result.stdout);
    process.exit(1);
  }

  const paths = (pack?.files ?? []).map((file) => file.path);
  const unexpected = unexpectedPackedPaths(paths);
  if (unexpected.length > 0) {
    console.error("error: unexpected files in the npm tarball:");
    for (const path of unexpected) console.error(`  ${path}`);
    process.exit(1);
  }

  console.log(`ok: ${paths.length} files in the tarball`);
  for (const path of paths.sort()) console.log(`  ${path}`);
}
