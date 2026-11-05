import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { defineConfig } from "vitest/config";

const root = fileURLToPath(new URL(".", import.meta.url));

export default defineConfig({
  resolve: {
    alias: {
      // The showcase demo modules import the package by name so the site can
      // consume `file:..`; root tests resolve that name to the source entry.
      "highmap-wasm": resolve(root, "src/index.ts"),
    },
  },
  test: {
    include: ["tests/**/*.test.ts"],
    // wasm instantiation + first-run codegen can be slow on cold caches
    testTimeout: 60_000,
    hookTimeout: 60_000,
    pool: "forks",
    poolOptions: {
      forks: {
        // expose gc for the FinalizationRegistry reclamation test
        execArgv: ["--expose-gc"],
      },
    },
  },
});
