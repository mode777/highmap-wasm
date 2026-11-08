import { execFile } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";
import { afterAll, describe, expect, it } from "vitest";

const execFileAsync = promisify(execFile);

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const benchScript = join(root, "scripts", "bench.mjs");

/** Per-run temp dir; removed in `afterAll` so the repo stays clean. */
const tmpDir = mkdtempSync(join(tmpdir(), "highmap-bench-"));
const outFile = join(tmpDir, "bench-smoke.json");

afterAll(() => {
  rmSync(tmpDir, { recursive: true, force: true });
});

/** One cell of the harness report (shape locked by AD-029). */
interface BenchCell {
  name: string;
  grid: number;
  reps: number;
  median_ms: number;
  min_ms: number;
  max_ms: number;
}

interface BenchReport {
  context: Record<string, unknown>;
  benchmarks: BenchCell[];
}

/**
 * Smoke test for `scripts/bench.mjs`: run the real harness as a child process
 * on a single cheap function at the smallest grid, then assert the JSON
 * contract only. Durations are machine-dependent, so nothing checks their
 * values beyond "finite positive number" - timing must never gate CI.
 */
describe("bench harness smoke", () => {
  it(
    "writes structurally valid JSON for one function at 64²",
    async () => {
      let exitCode: number | null = null;
      let stdout = "";
      let stderr = "";

      try {
        const result = await execFileAsync(
          process.execPath,
          [
            benchScript,
            "--only",
            "noiseFbm",
            "--sizes",
            "64",
            "--out",
            outFile,
          ],
          { cwd: root, encoding: "utf8" },
        );
        exitCode = 0;
        stdout = result.stdout;
        stderr = result.stderr;
      } catch (error) {
        const failure = error as {
          code?: number;
          stdout?: string;
          stderr?: string;
        };
        exitCode = typeof failure.code === "number" ? failure.code : null;
        stdout = failure.stdout ?? "";
        stderr = failure.stderr ?? "";
      }

      expect(exitCode, `bench harness failed:\n${stderr || stdout}`).toBe(0);
      expect(stdout).toContain("noiseFbm");

      expect(existsSync(outFile), `missing report at ${outFile}`).toBe(true);
      const report = JSON.parse(readFileSync(outFile, "utf8")) as BenchReport;

      // Top level is the locked Google-Benchmark-compatible pair.
      expect(Object.keys(report).sort()).toEqual(["benchmarks", "context"]);

      // The context block identifies the machine, build and artifact.
      for (const key of ["node", "platform", "gitCommit", "wasmBytes"]) {
        expect(report.context, `context.${key}`).toHaveProperty(key);
      }

      expect(Array.isArray(report.benchmarks)).toBe(true);
      expect(report.benchmarks).toHaveLength(1);

      const cell = report.benchmarks[0];
      expect(cell).toBeDefined();
      if (!cell) throw new Error("unreachable: benchmarks[0] missing");

      expect(cell.name).toBe("noiseFbm");
      expect(cell.grid).toBe(64);
      expect(cell.reps).toBeGreaterThanOrEqual(3);

      for (const field of ["median_ms", "min_ms", "max_ms"] as const) {
        const value = cell[field];
        expect(Number.isFinite(value), `${field} is not finite`).toBe(true);
        expect(value, `${field} is not positive`).toBeGreaterThan(0);
      }
    },
    // Generous ceiling: the 64² cell takes well under a second once the wasm
    // module is instantiated, but a cold CI machine may still be slow.
    120_000,
  );
});
