"use strict";

const path = require("node:path");
const { spawnSync } = require("node:child_process");

const manifest = require("../package.json");
const executable = process.platform === "win32"
  ? path.join(__dirname, "..", "node_modules", ".bin", "vsce.cmd")
  : path.join(__dirname, "..", "node_modules", ".bin", "vsce");
const result = spawnSync(executable, [
  "package",
  "--out",
  `dist/hitsimple-vscode-${manifest.version}.vsix`,
], {
  stdio: "inherit",
  shell: process.platform === "win32",
});

if (result.error) {
  throw result.error;
}
process.exit(result.status === null ? 1 : result.status);
