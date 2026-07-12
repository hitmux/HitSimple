"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const path = require("node:path");
const vscode = require("vscode");

const workspacePath = process.env.HITSIMPLE_TEST_WORKSPACE;
const compilerPath = process.env.HITSIMPLE_TEST_COMPILER;
const outputDirectory = process.env.HITSIMPLE_TEST_OUTPUT_DIRECTORY;

function fileUri(name) {
  return vscode.Uri.file(path.join(workspacePath, name));
}

async function waitForValue(read, accept, description, timeoutMs = 10000) {
  const deadline = Date.now() + timeoutMs;
  let value;
  while (Date.now() < deadline) {
    value = await read();
    if (accept(value)) {
      return value;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`Timed out waiting for ${description}: ${JSON.stringify(value)}`);
}

async function runStep(name, action, timeoutMs = 60000) {
  console.log(`HitSimple Extension Host: ${name}`);
  let timer;
  try {
    await Promise.race([
      action(),
      new Promise((_, reject) => {
        timer = setTimeout(
          () => reject(new Error(`Timed out during Extension Host step: ${name}`)),
          timeoutMs,
        );
      }),
    ]);
  } finally {
    clearTimeout(timer);
  }
}

async function openEditor(name) {
  const document = await vscode.workspace.openTextDocument(fileUri(name));
  const editor = await vscode.window.showTextDocument(document);
  return { document, editor };
}

async function executeTaskAndWait(task) {
  let execution;
  const earlyEvents = [];
  let resolveEnd;
  const ended = new Promise((resolve) => {
    resolveEnd = resolve;
  });
  const subscription = vscode.tasks.onDidEndTaskProcess((event) => {
    if (!execution) {
      earlyEvents.push(event);
    } else if (event.execution === execution) {
      resolveEnd(event);
    }
  });

  try {
    execution = await vscode.tasks.executeTask(task);
    const early = earlyEvents.find((event) => event.execution === execution);
    return early || await ended;
  } finally {
    subscription.dispose();
  }
}

function compilerTask(name, sourceUri, outputPath) {
  const folder = vscode.workspace.getWorkspaceFolder(sourceUri);
  assert.ok(folder, `${sourceUri.fsPath} must belong to the test workspace`);
  const execution = new vscode.ProcessExecution(
    compilerPath,
    ["--unchecked", sourceUri.fsPath, "-o", outputPath],
    { cwd: workspacePath },
  );
  return new vscode.Task(
    { type: "hitsimple-extension-host", name },
    folder,
    name,
    "HitSimple Extension Host",
    execution,
    "$hsc",
  );
}

async function assertDiagnosticsRemainEmpty(uri, durationMs = 1200) {
  const deadline = Date.now() + durationMs;
  while (Date.now() < deadline) {
    assert.deepEqual(
      vscode.languages.getDiagnostics(uri),
      [],
      `unexpected Problems marker for ${uri.fsPath}`,
    );
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
}

async function captureStartedTasks(action) {
  const tasks = [];
  const subscription = vscode.tasks.onDidStartTask((event) => {
    tasks.push(event.execution.task);
  });
  try {
    return { result: await action(), tasks };
  } finally {
    subscription.dispose();
  }
}

async function verifyBuildAndRun(mainUri, extension) {
  const configuration = vscode.workspace.getConfiguration("hitsimple", mainUri);
  assert.equal(configuration.get("compilerPath"), compilerPath);
  assert.equal(configuration.get("outputDirectory"), outputDirectory);
  const expectedOutput = path.join(workspacePath, outputDirectory, "main");
  const modeFlags = {
    unchecked: "--unchecked",
    "static-checked": "--static-checked",
    checked: "--checked",
  };

  for (const [mode, modeFlag] of Object.entries(modeFlags)) {
    await configuration.update("mode", mode, vscode.ConfigurationTarget.Workspace);
    await fs.rm(expectedOutput, { force: true });
    const result = await vscode.commands.executeCommand(
      "hitsimple.buildCurrentFile",
    );
    assert.ok(result && result.ok, `buildCurrentFile failed in ${mode} mode`);
    assert.equal(result.plan.mode, mode);
    assert.equal(result.plan.modeFlag, modeFlag);
    assert.equal(result.outputPath, expectedOutput);
    assert.ok((await fs.stat(expectedOutput)).isFile());
    assert.equal(extension.isActive, true, "command execution must activate extension");
  }

  await configuration.update(
    "mode",
    "unchecked",
    vscode.ConfigurationTarget.Workspace,
  );
  const runResult = await vscode.commands.executeCommand(
    "hitsimple.runCurrentFile",
  );
  assert.ok(runResult && runResult.ok, "runCurrentFile must exit successfully");
  assert.equal(runResult.stage, "run");
  assert.equal(runResult.exitCode, 0);
}

async function verifyProblemMatcher() {
  const outputRoot = path.join(workspacePath, outputDirectory, "diagnostics");
  await fs.mkdir(outputRoot, { recursive: true });

  const sema = await openEditor("sema-error.hs");
  assert.equal(sema.document.languageId, "hitsimple");
  assert.deepEqual(vscode.languages.getDiagnostics(sema.document.uri), []);
  const semaEvent = await executeTaskAndWait(compilerTask(
    "HitSimple sema diagnostic",
    sema.document.uri,
    path.join(outputRoot, "sema-error"),
  ));
  assert.equal(semaEvent.exitCode, 1);

  const diagnostics = await waitForValue(
    () => vscode.languages.getDiagnostics(sema.document.uri),
    (items) => items.length > 0,
    "the $hsc Problems marker",
  );
  const diagnostic = diagnostics.find((item) =>
    item.message.includes("unsupported function call 'add_two'"));
  assert.ok(diagnostic, "expected the real sema diagnostic in Problems");
  assert.equal(diagnostic.severity, vscode.DiagnosticSeverity.Error);
  assert.equal(diagnostic.source, "hsc");
  assert.equal(diagnostic.range.start.line, 1);
  assert.equal(diagnostic.range.start.character, 11);

  const includeSource = await openEditor("include-error.hs");
  const includeUri = fileUri("broken.hsi");
  assert.deepEqual(vscode.languages.getDiagnostics(includeUri), []);
  const includeEvent = await executeTaskAndWait(compilerTask(
    "HitSimple include diagnostic",
    includeSource.document.uri,
    path.join(outputRoot, "include-error"),
  ));
  assert.equal(includeEvent.exitCode, 1);
  const includeDiagnostics = await waitForValue(
    () => vscode.languages.getDiagnostics(includeUri),
    (items) => items.length > 0,
    "the included-file $hsc Problems marker",
  );
  const includeDiagnostic = includeDiagnostics.find((item) =>
    item.message.includes("syntax error, unexpected ]"));
  assert.ok(includeDiagnostic, "expected parser diagnostic on broken.hsi");
  assert.equal(includeDiagnostic.source, "hsc");
  assert.equal(includeDiagnostic.range.start.line, 0);
  assert.equal(includeDiagnostic.range.start.character, 10);

  const missingMain = await openEditor("missing-main.hs");
  assert.equal(missingMain.document.languageId, "hitsimple");
  assert.deepEqual(vscode.languages.getDiagnostics(missingMain.document.uri), []);
  const missingMainEvent = await executeTaskAndWait(compilerTask(
    "HitSimple locationless diagnostic",
    missingMain.document.uri,
    path.join(outputRoot, "missing-main"),
  ));
  assert.equal(missingMainEvent.exitCode, 1);
  await assertDiagnosticsRemainEmpty(missingMain.document.uri);
}

async function verifyFailureGating(mainUri) {
  const configuration = vscode.workspace.getConfiguration("hitsimple", mainUri);

  console.log("HitSimple Extension Host: failed Build blocks Run");
  const compileError = await openEditor("sema-error.hs");
  const compileFailure = await captureStartedTasks(() =>
    vscode.commands.executeCommand("hitsimple.runCurrentFile"));
  assert.ok(compileFailure.result && !compileFailure.result.ok);
  assert.equal(compileFailure.result.stage, "compile");
  assert.equal(compileFailure.tasks.length, 1, "failed Build must not start Run");
  assert.equal(compileFailure.tasks[0].definition.command, "build");

  console.log("HitSimple Extension Host: missing compiler");
  await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(mainUri));
  const originalCompiler = configuration.get("compilerPath");
  const missingCompiler = path.join(workspacePath, "missing-hsc");
  await configuration.update(
    "compilerPath",
    missingCompiler,
    vscode.ConfigurationTarget.Workspace,
  );
  const missingCompilerResult = await vscode.commands.executeCommand(
    "hitsimple.buildCurrentFile",
  );
  assert.ok(missingCompilerResult && !missingCompilerResult.ok);
  assert.ok(
    ["start-build", "compile"].includes(missingCompilerResult.stage),
    `unexpected missing compiler stage: ${missingCompilerResult.stage}`,
  );
  assert.match(missingCompilerResult.message, /compilerPath|构建失败/);
  await configuration.update(
    "compilerPath",
    originalCompiler,
    vscode.ConfigurationTarget.Workspace,
  );

  console.log("HitSimple Extension Host: unwritable output directory");
  const originalOutputDirectory = configuration.get("outputDirectory");
  await configuration.update(
    "outputDirectory",
    "blocked-output",
    vscode.ConfigurationTarget.Workspace,
  );
  const outputFailure = await vscode.commands.executeCommand(
    "hitsimple.buildCurrentFile",
  );
  assert.ok(outputFailure && !outputFailure.ok);
  assert.equal(outputFailure.stage, "prepare-output");
  assert.match(outputFailure.message, /无法准备构建目录/);
  await configuration.update(
    "outputDirectory",
    originalOutputDirectory,
    vscode.ConfigurationTarget.Workspace,
  );
}

async function verifySnippet() {
  const { document } = await openEditor("snippet.hs");
  assert.equal(document.languageId, "hitsimple");
  await vscode.commands.executeCommand("editor.action.insertSnippet", {
    langId: "hitsimple",
    name: "main function",
  });
  await waitForValue(
    () => document.getText(),
    (text) => text.includes("func main()"),
    "the contributed main snippet",
  );
  assert.equal(
    document.getText(),
    "func main() {\n    // code\n    return 0\n}",
  );
}

async function verifyIndentation(name, firstLine) {
  const { document, editor } = await openEditor(name);
  assert.equal(document.languageId, "hitsimple");
  const position = new vscode.Position(0, firstLine.length);
  editor.selection = new vscode.Selection(position, position);
  await vscode.commands.executeCommand("type", { text: "\n" });
  await waitForValue(
    () => document.lineCount,
    (lineCount) => lineCount === 2,
    `newline insertion for ${name}`,
  );
  assert.equal(
    document.lineAt(1).text,
    "    ",
    `${firstLine} should increase indentation after Enter`,
  );
}

async function run() {
  assert.ok(workspacePath, "HITSIMPLE_TEST_WORKSPACE is required");
  assert.ok(compilerPath, "HITSIMPLE_TEST_COMPILER is required");
  assert.ok(outputDirectory, "HITSIMPLE_TEST_OUTPUT_DIRECTORY is required");
  assert.equal(vscode.workspace.isTrusted, true);

  const extension = vscode.extensions.getExtension("hitmux.hitsimple-vscode");
  assert.ok(extension, "development extension must be installed");

  const main = await openEditor("main.hs");
  assert.equal(main.document.languageId, "hitsimple");
  await runStep("Build and Run", () => verifyBuildAndRun(main.document.uri, extension));
  const commands = await vscode.commands.getCommands(true);
  assert.ok(commands.includes("hitsimple.buildCurrentFile"));
  assert.ok(commands.includes("hitsimple.runCurrentFile"));
  await runStep("Problems", verifyProblemMatcher);
  await runStep("failure gating", () => verifyFailureGating(main.document.uri));
  await runStep("snippet", verifySnippet);
  await runStep(
    "brace indentation",
    () => verifyIndentation("brace-indent.hs", "func main() {"),
  );
  await runStep(
    "preprocessor indentation",
    () => verifyIndentation("directive-indent.hs", "$ if ENABLED"),
  );

  console.log("HitSimple Extension Host assertions passed");
}

module.exports = { run };
