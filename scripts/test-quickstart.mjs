// Verifies the README quick-start block runs verbatim against a real
// installed package tarball (npm pack + npm install in a scratch project).

import { execFileSync, execSync } from "node:child_process";
import { mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("..", import.meta.url));
const readme = readFileSync(join(root, "README.md"), "utf8");
const match = readme.match(/<!-- quickstart:start -->\s*```js\n([\s\S]*?)```/);
if (!match) throw new Error("README quick-start block not found");
const snippet = match[1];

const scratch = mkdtempSync(join(tmpdir(), "highmap-quickstart-"));

try {
  execSync(`npm pack --pack-destination "${scratch}" --silent`, {
    cwd: root,
    encoding: "utf8",
  });
  const tarball = readdirSync(scratch).find((f) => f.endsWith(".tgz"));
  if (!tarball) throw new Error("npm pack produced no tarball");

  writeFileSync(
    join(scratch, "package.json"),
    JSON.stringify({ name: "quickstart", private: true, type: "module" }),
  );
  execSync(`npm install "${join(scratch, tarball)}" --no-save --silent`, {
    cwd: scratch,
    encoding: "utf8",
  });

  writeFileSync(join(scratch, "quickstart.mjs"), snippet);
  const out = execFileSync(process.execPath, [join(scratch, "quickstart.mjs")], {
    cwd: scratch,
    encoding: "utf8",
  });
  process.stdout.write(out);
  console.log("README quick-start runs verbatim OK");
} finally {
  rmSync(scratch, { recursive: true, force: true });
}
