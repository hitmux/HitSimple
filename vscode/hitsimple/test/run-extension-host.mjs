import { createRequire } from "node:module";
import { mkdtemp, mkdir, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const { runTests } = require("@vscode/test-electron");

const testDirectory = path.dirname(fileURLToPath(import.meta.url));
const extensionDevelopmentPath = path.resolve(testDirectory, "..");
const repoRoot = path.resolve(extensionDevelopmentPath, "../..");
const extensionTestsPath = path.join(testDirectory, "extension-host", "index.js");
const compilerPath = process.env.HSC_PATH
  ? path.resolve(process.env.HSC_PATH)
  : path.join(repoRoot, "build/hsc");
const outputDirectory = ".hitsimple/extension-host";
const cachePath = path.join(tmpdir(), "hitsimple-vscode-test-electron-cache");
const vscodeVersion = process.env.VSCODE_TEST_VERSION || "1.128.0";

async function prepareWorkspace(root) {
  const workspacePath = path.join(root, "workspace");
  const userDataPath = path.join(root, "user-data");
  const extensionsPath = path.join(root, "extensions");
  await Promise.all([
    mkdir(path.join(workspacePath, ".vscode"), { recursive: true }),
    mkdir(path.join(userDataPath, "User"), { recursive: true }),
    mkdir(extensionsPath, { recursive: true }),
    mkdir(cachePath, { recursive: true }),
  ]);

  await Promise.all([
    writeFile(
      path.join(workspacePath, ".vscode", "settings.json"),
      `${JSON.stringify({
        "hitsimple.compilerPath": compilerPath,
        "hitsimple.mode": "unchecked",
        "hitsimple.outputDirectory": outputDirectory,
        "hitsimple.additionalArgs": [],
        "editor.autoIndent": "full",
        "editor.insertSpaces": true,
        "editor.tabSize": 4,
      }, null, 2)}\n`,
    ),
    writeFile(
      path.join(userDataPath, "User", "settings.json"),
      `${JSON.stringify({
        "security.workspace.trust.enabled": false,
        "workbench.startupEditor": "none",
        "telemetry.telemetryLevel": "off",
        "extensions.autoCheckUpdates": false,
        "extensions.autoUpdate": false,
      }, null, 2)}\n`,
    ),
    writeFile(
      path.join(workspacePath, "main.hs"),
      "func main() {\n    return 0\n}\n",
    ),
    writeFile(
      path.join(workspacePath, "sema-error.hs"),
      "func main() {\n    return add_two(1)\n}\n",
    ),
    writeFile(
      path.join(workspacePath, "missing-main.hs"),
      "func helper() {\n    return 0\n}\n",
    ),
    writeFile(
      path.join(workspacePath, "broken.hsi"),
      "func main(]\n",
    ),
    writeFile(
      path.join(workspacePath, "include-error.hs"),
      '$ include "broken.hsi"\n',
    ),
    writeFile(path.join(workspacePath, "snippet.hs"), ""),
    writeFile(path.join(workspacePath, "blocked-output"), "not a directory\n"),
    writeFile(path.join(workspacePath, "brace-indent.hs"), "func main() {"),
    writeFile(path.join(workspacePath, "directive-indent.hs"), "$ if ENABLED"),
  ]);

  return { workspacePath, userDataPath, extensionsPath };
}

const tempRoot = await mkdtemp(path.join(tmpdir(), "hitsimple-vscode-host-"));

try {
  const { workspacePath, userDataPath, extensionsPath } =
    await prepareWorkspace(tempRoot);
  await runTests({
    version: vscodeVersion,
    cachePath,
    extensionDevelopmentPath,
    extensionTestsPath,
    extensionTestsEnv: {
      HITSIMPLE_TEST_WORKSPACE: workspacePath,
      HITSIMPLE_TEST_COMPILER: compilerPath,
      HITSIMPLE_TEST_OUTPUT_DIRECTORY: outputDirectory,
    },
    launchArgs: [
      workspacePath,
      `--user-data-dir=${userDataPath}`,
      `--extensions-dir=${extensionsPath}`,
      "--disable-workspace-trust",
      "--skip-welcome",
      "--skip-release-notes",
    ],
  });
} finally {
  await rm(tempRoot, { recursive: true, force: true });
}
