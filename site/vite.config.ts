import { defineConfig } from "vite";

const base = process.env.SITE_BASE ?? "/";
const commit = process.env.GITHUB_SHA ?? process.env.COMMIT_SHA ?? "main";

export default defineConfig({
  base,
  worker: { format: "es" },
  define: {
    __COMMIT_SHA__: JSON.stringify(commit),
  },
  build: {
    target: "es2022",
    assetsInlineLimit: 0,
  },
});
