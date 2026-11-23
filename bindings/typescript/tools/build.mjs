import { spawnSync } from "node:child_process";
import { copyFileSync, cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("../", import.meta.url));
const prefix = process.env.NOCTURNE_SDK_PREFIX;
const toolchain = process.env.NOCTURNE_WASI_TOOLCHAIN;
if (!prefix || !toolchain) {
  throw new Error("Set NOCTURNE_SDK_PREFIX and NOCTURNE_WASI_TOOLCHAIN");
}
const unicodeLicense = process.env.NOCTURNE_UNICODE_LICENSE ?? resolve(prefix, "share/licenses/nocturne/Unicode-License.txt");
const runtimeNotices = process.env.NOCTURNE_WASI_NOTICE_DIR;

function run(program, args) {
  const command = spawnSync(program, args, { cwd: root, stdio: "inherit" });
  if (command.error) throw command.error;
  if (command.status !== 0) throw new Error(`${program} failed (${command.status ?? command.signal})`);
}

function readVarUint32(bytes, offset) {
  let value = 0;
  for (let shift = 0; shift <= 28; shift += 7) {
    const byte = bytes[offset++];
    if (byte === undefined) break;
    value += (byte & 127) * 2 ** shift;
    if (byte < 128 && value <= 0xffffffff) return [value, offset];
  }
  throw new Error("Invalid WebAssembly section length");
}

function withoutDwarf(bytes) {
  if (bytes.length < 8 || bytes.subarray(0, 8).toString("hex") !== "0061736d01000000") {
    throw new Error("Expected a WebAssembly version 1 module");
  }
  const sections = [bytes.subarray(0, 8)];
  for (let start = 8; start < bytes.length;) {
    const [size, payload] = readVarUint32(bytes, start + 1);
    const end = payload + size;
    if (end > bytes.length) throw new Error("Truncated WebAssembly section");
    let dwarf = false;
    if (bytes[start] === 0) {
      const [length, name] = readVarUint32(bytes, payload);
      if (name + length > end) throw new Error("Truncated WebAssembly custom section name");
      dwarf = bytes.toString("utf8", name, name + length).startsWith(".debug_");
    }
    if (!dwarf) sections.push(bytes.subarray(start, end));
    start = end;
  }
  return Buffer.concat(sections);
}

const nativeBuild = mkdtempSync(resolve(tmpdir(), "nocturne-wasm-"));
try {
  run("cmake", ["-S", "native", "-B", nativeBuild, "-DCMAKE_BUILD_TYPE=Release",
    `-DNOCTURNE_SDK_PREFIX=${resolve(prefix)}`, `-DCMAKE_TOOLCHAIN_FILE=${resolve(toolchain)}`]);
  run("cmake", ["--build", nativeBuild, "--config", "Release"]);
  run(process.execPath, ["node_modules/typescript/lib/tsc.js", "--project", "tsconfig.json"]);
  const wasm = readFileSync(resolve(nativeBuild, "product/nocturne.wasm"));
  writeFileSync(resolve(root, "dist/nocturne.wasm"), withoutDwarf(wasm));
  copyFileSync(unicodeLicense, resolve(root, "dist/Unicode-License.txt"));
  const notices = resolve(root, "dist/third-party");
  rmSync(notices, { recursive: true, force: true });
  if (runtimeNotices) cpSync(runtimeNotices, notices, { recursive: true });
} finally {
  rmSync(nativeBuild, { recursive: true, force: true });
}
