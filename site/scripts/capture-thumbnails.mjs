import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import { mkdir, readdir, writeFile } from "node:fs/promises";
import { createServer } from "node:net";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import puppeteer from "puppeteer-core";
import { hashDemoSource } from "./preview-hash.mjs";

const SITE_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const DEMO_DIR = join(SITE_ROOT, "src", "demos");
const OUT_DIR = join(SITE_ROOT, "src", "assets", "previews");
const VITE_BIN = join(SITE_ROOT, "node_modules", "vite", "bin", "vite.js");
const PREVIEW_VERSION = 2;
const VIEWPORT = { width: 640, height: 400, deviceScaleFactor: 2 };
const QUALITY = 82;

const EXCLUDED = new Set([
  "types.ts",
  "helpers.ts",
  "index.ts",
  "sources.ts",
]);

async function demoIds() {
  const entries = await readdir(DEMO_DIR);
  return entries
    .filter((name) => name.endsWith(".ts") && !EXCLUDED.has(name))
    .map((name) => name.replace(/\.ts$/, ""))
    .sort();
}

function freePort() {
  return new Promise((resolvePort, reject) => {
    const server = createServer();
    server.unref();
    server.on("error", reject);
    server.listen(0, "127.0.0.1", () => {
      const address = server.address();
      if (!address || typeof address === "string") {
        server.close();
        reject(new Error("could not allocate a preview port"));
        return;
      }
      const { port } = address;
      server.close(() => resolvePort(port));
    });
  });
}

async function waitForServer(url, timeoutMs = 30_000) {
  const started = Date.now();
  while (Date.now() - started < timeoutMs) {
    try {
      const response = await fetch(url);
      if (response.ok) return;
    } catch {
      // server is still starting
    }
    await new Promise((resolveDelay) => setTimeout(resolveDelay, 250));
  }
  throw new Error(`preview server did not become ready at ${url}`);
}

function chromePath() {
  if (process.env.CHROME_SHELL_PATH) return process.env.CHROME_SHELL_PATH;
  const candidates = [
    join(process.env.ProgramFiles ?? "", "Google", "Chrome", "Application", "chrome.exe"),
    join(
      process.env["ProgramFiles(x86)"] ?? "",
      "Google",
      "Chrome",
      "Application",
      "chrome.exe",
    ),
    join(
      process.env.LOCALAPPDATA ?? "",
      "Google",
      "Chrome",
      "Application",
      "chrome.exe",
    ),
    join(
      process.env["ProgramFiles(x86)"] ?? "",
      "Microsoft",
      "Edge",
      "Application",
      "msedge.exe",
    ),
    join(process.env.ProgramFiles ?? "", "Microsoft", "Edge", "Application", "msedge.exe"),
    "/usr/bin/google-chrome",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser",
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
  ];
  return candidates.find((candidate) => candidate && existsSync(candidate)) ?? null;
}

async function main() {
  const ids = await demoIds();
  await mkdir(OUT_DIR, { recursive: true });

  let server = null;
  let baseUrl = process.env.CAPTURE_BASE_URL ?? null;
  if (!baseUrl) {
    const port = await freePort();
    baseUrl = `http://127.0.0.1:${port}/`;
    server = spawn(
      process.execPath,
      [VITE_BIN, "preview", "--host", "127.0.0.1", "--port", String(port), "--strictPort"],
      { cwd: SITE_ROOT, stdio: "inherit" },
    );
    await waitForServer(baseUrl);
  }
  if (!baseUrl.endsWith("/")) baseUrl += "/";

  let browser = null;
  let ownsBrowser = false;
  if (process.env.CHROME_URL) {
    browser = await puppeteer.connect({ browserURL: process.env.CHROME_URL });
  } else {
    const executablePath = chromePath();
    if (!executablePath) {
      throw new Error(
        "No Chrome found. Set CHROME_SHELL_PATH or CHROME_URL, or install Chrome.",
      );
    }
    browser = await puppeteer.launch({
      executablePath,
      headless: true,
      defaultViewport: VIEWPORT,
      args: [
        "--no-sandbox",
        "--disable-dev-shm-usage",
        "--enable-webgl",
        "--ignore-gpu-blocklist",
        "--enable-unsafe-swiftshader",
        "--use-gl=angle",
        "--use-angle=swiftshader",
      ],
    });
    ownsBrowser = true;
  }

  const manifest = {
    previewVersion: PREVIEW_VERSION,
    capturedAt: new Date().toISOString(),
    viewport: VIEWPORT,
    quality: QUALITY,
    demos: {},
  };

  try {
    for (const id of ids) {
      const page = await browser.newPage();
      await page.setViewport(VIEWPORT);
      const errors = [];
      page.on("pageerror", (error) => errors.push(String(error)));
      page.on("console", (message) => {
        if (message.type() === "error") errors.push(message.text());
      });

      const url = `${baseUrl}#/p/${encodeURIComponent(id)}?preview=1`;
      console.log(`==> capturing ${id}`);
      await page.goto(url, { waitUntil: "networkidle0", timeout: 60_000 });
      await page.waitForSelector('[data-demo-ready="true"]', { timeout: 60_000 });
      await new Promise((resolveDelay) => setTimeout(resolveDelay, 350));

      const file = `${id}.webp`;
      await page.screenshot({
        path: join(OUT_DIR, file),
        type: "webp",
        quality: QUALITY,
      });

      manifest.demos[id] = {
        file,
        sourceHash: hashDemoSource(join(DEMO_DIR, `${id}.ts`)),
        width: VIEWPORT.width * VIEWPORT.deviceScaleFactor,
        height: VIEWPORT.height * VIEWPORT.deviceScaleFactor,
      };

      if (errors.length > 0) {
        console.warn(`    browser warnings for ${id}:\n    ${errors.join("\n    ")}`);
      }
      await page.close();
    }

    await writeFile(
      join(OUT_DIR, "manifest.json"),
      `${JSON.stringify(manifest, null, 2)}\n`,
      "utf8",
    );
    console.log(`==> wrote ${ids.length} previews and manifest.json`);
  } finally {
    if (ownsBrowser) await browser.close();
    else if (browser) await browser.disconnect();
    if (server) server.kill();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
