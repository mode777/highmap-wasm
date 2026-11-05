import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";

/**
 * Preview provenance covers the whole demo module: metadata and recipe live in
 * the same file, so a metadata or parameter change invalidates the image.
 */
export function hashDemoSource(filePath) {
  return createHash("sha256").update(readFileSync(filePath)).digest("hex");
}
