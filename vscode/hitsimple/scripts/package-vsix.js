"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { spawnSync } = require("node:child_process");

const manifest = require("../package.json");
const executable = process.platform === "win32"
  ? path.join(__dirname, "..", "node_modules", ".bin", "vsce.cmd")
  : path.join(__dirname, "..", "node_modules", ".bin", "vsce");
const outputPath = path.join(
  __dirname,
  "..",
  "dist",
  `hitsimple-vscode-${manifest.version}.vsix`,
);
fs.mkdirSync(path.dirname(outputPath), { recursive: true });
const result = spawnSync(executable, [
  "package",
  "--out",
  outputPath,
], { stdio: "inherit" });

if (result.error) {
  throw result.error;
}
process.exit(result.status === null ? 1 : result.status);
