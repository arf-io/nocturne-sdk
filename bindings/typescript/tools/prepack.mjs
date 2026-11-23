import { existsSync, readdirSync } from "node:fs";
import { fileURLToPath } from "node:url";

const dist = new URL("../dist/", import.meta.url);
for (const file of ["index.js", "index.d.ts", "nocturne.wasm", "Unicode-License.txt"]) {
  if (!existsSync(new URL(file, dist))) throw new Error(`Build the SDK before packaging. Missing dist/${file}.`);
}

const notices = fileURLToPath(new URL("third-party/", dist));
if (!existsSync(notices) || !readdirSync(notices, { recursive: true, withFileTypes: true }).some(entry => entry.isFile())) {
  throw new Error("Build with NOCTURNE_WASI_NOTICE_DIR set to the linked WASI runtime notices before packaging.");
}
