#!/usr/bin/env node
// Validates a release tag against package.json before the publish workflow
// builds anything (design D1/D5, AD-031).
//
// Usage: node scripts/release-guard.mjs vX.Y.Z[-prerelease]
//
// Prints `version=` and `dist_tag=` lines and, when GITHUB_OUTPUT is set,
// appends them as step outputs. Exported helpers are unit-tested in
// tests/release-guard.test.ts.

import { appendFileSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const TAG_PATTERN =
  /^v(\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?)$/;

/**
 * Strip the leading `v` from a release tag and reject anything that is not
 * SemVer-shaped (so `v1.2` fails loudly instead of being skipped by a narrow
 * trigger glob).
 *
 * @param {string} tag
 * @returns {string} the version without the `v` prefix
 */
export function parseReleaseTag(tag) {
  const match = TAG_PATTERN.exec(tag);
  if (!match || match[1] === undefined) {
    throw new Error(
      `invalid release tag "${tag}": expected v<major>.<minor>.<patch>[-prerelease]`,
    );
  }
  return match[1];
}

/**
 * Check a release tag against the package version and derive the npm dist-tag.
 *
 * @param {string} tag
 * @param {string} packageVersion
 * @returns {{ version: string, distTag: "latest" | "next" }}
 */
export function checkReleaseTag(tag, packageVersion) {
  const version = parseReleaseTag(tag);
  if (version !== packageVersion) {
    throw new Error(
      `tag ${tag} does not match package.json version ${packageVersion}: ` +
        `bump package.json and commit before tagging`,
    );
  }
  const distTag = version.includes("-") ? "next" : "latest";
  return { version, distTag };
}

function readPackageVersion() {
  const root = join(dirname(fileURLToPath(import.meta.url)), "..");
  const pkg = JSON.parse(readFileSync(join(root, "package.json"), "utf8"));
  return /** @type {string} */ (pkg.version);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const tag = process.argv[2];
  if (!tag) {
    console.error("usage: node scripts/release-guard.mjs vX.Y.Z[-prerelease]");
    process.exit(2);
  }
  try {
    const { version, distTag } = checkReleaseTag(tag, readPackageVersion());
    const output = `version=${version}\ndist_tag=${distTag}\n`;
    process.stdout.write(output);
    if (process.env.GITHUB_OUTPUT) {
      appendFileSync(process.env.GITHUB_OUTPUT, output);
    }
  } catch (error) {
    console.error(`error: ${error instanceof Error ? error.message : error}`);
    process.exit(1);
  }
}
