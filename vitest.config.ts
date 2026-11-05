import { defineConfig } from "vitest/config";

export default defineConfig({
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
