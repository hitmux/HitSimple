import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { access, mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { createRequire } from "node:module";
import { constants } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const {
  BuildPlanError,
  MODE_FLAGS,
  createBuildPlan,
  validateAdditionalArgs,
} = require("../src/buildPlan");
const { findExecutable } = require("../src/executable");
const {
  DebugPlanError,
  createCppdbgLaunchConfiguration,
  normalizeDebugBuildArgs,
  validateDebugArguments,
  validateGdbPath,
} = require("../src/debugPlan");
const { createDebugWorkflow } = require("../src/debugWorkflow");
const {
  createTaskRunner,
  executeProcessTask,
} = require("../src/taskRunner");
const { createWorkflows } = require("../src/workflows");
const { registerExtension } = require("../src/extension");
const repoRoot = path.resolve(fileURLToPath(new URL("../../..", import.meta.url)));
const testCompilerPath = process.env.HSC_PATH
  ? path.resolve(process.env.HSC_PATH)
  : path.join(repoRoot, "build/hsc");

function basePlanOptions(overrides = {}) {
  return {
    sourcePath: "/workspace/src/demo.hs",
    workspacePath: "/workspace",
    compilerPath: "hsc",
    mode: "unchecked",
    outputDirectory: ".hitsimple/build",
    additionalArgs: [],
    platform: "linux",
    ...overrides,
  };
}

test("build plan maps modes and preserves each additional argv entry", () => {
  for (const [mode, flag] of Object.entries(MODE_FLAGS)) {
    const additionalArgs = [
      "--c-compat",
      "path with spaces/input.c",
      'value="quoted"',
      ";",
      "$()",
    ];
    const plan = createBuildPlan(basePlanOptions({ mode, additionalArgs }));

    assert.equal(plan.modeFlag, flag);
    assert.equal(plan.outputPath, "/workspace/.hitsimple/build/src/demo");
    assert.deepEqual(plan.args, [
      flag,
      ...additionalArgs,
      "/workspace/src/demo.hs",
      "-o",
      "/workspace/.hitsimple/build/src/demo",
    ]);
  }
});

test("build plan handles Windows executable paths", () => {
  const plan = createBuildPlan({
    sourcePath: "C:\\workspace\\src\\demo.hs",
    workspacePath: "C:\\workspace",
    compilerPath: "C:\\Program Files\\HitSimple\\hsc.exe",
    mode: "checked",
    outputDirectory: ".hitsimple\\build",
    additionalArgs: [],
    platform: "win32",
  });

  assert.equal(
    plan.outputPath,
    "C:\\workspace\\.hitsimple\\build\\src\\demo.exe",
  );
  assert.equal(plan.args[0], "--checked");
});

test("real hsc builds and runs all three configured safety modes", async () => {
  const workspacePath = await mkdtemp(path.join(tmpdir(), "hitsimple-vscode-build-"));
  const sourcePath = path.join(workspacePath, "src", "main.hs");
  try {
    await mkdir(path.dirname(sourcePath), { recursive: true });
    await writeFile(sourcePath, "func main() {\n    return 0\n}\n", "utf8");

    for (const mode of Object.keys(MODE_FLAGS)) {
      const plan = createBuildPlan({
        sourcePath,
        workspacePath,
        compilerPath: testCompilerPath,
        mode,
        outputDirectory: path.join(workspacePath, `out-${mode}`),
        additionalArgs: [],
        platform: process.platform,
      });
      await mkdir(plan.outputParent, { recursive: true });
      const built = spawnSync(plan.compilerPath, plan.args, {
        cwd: plan.cwd,
        encoding: "utf8",
      });
      assert.equal(built.status, 0, built.stderr);
      await access(plan.outputPath, constants.X_OK);

      const ran = spawnSync(plan.outputPath, [], {
        cwd: plan.cwd,
        encoding: "utf8",
      });
      assert.equal(ran.status, 0, ran.stderr);
    }
  } finally {
    await rm(workspacePath, { recursive: true, force: true });
  }
});

function executableFileSystem(executables) {
  return {
    async stat(filePath) {
      if (!executables.has(filePath)) {
        throw new Error("missing file");
      }
      return { isFile: () => true };
    },
    async access(filePath) {
      if (!executables.has(filePath)) {
        throw new Error("missing file");
      }
    },
  };
}

test("executable lookup resolves Linux and Windows compiler paths", async () => {
  const linuxCompiler = "/workspace/bin/hsc-test";
  const linuxFileSystem = executableFileSystem(new Set([linuxCompiler]));
  assert.equal(
    await findExecutable("hsc-test", {
      environment: { PATH: "/workspace/bin" },
      fileSystem: linuxFileSystem,
      platform: "linux",
    }),
    linuxCompiler,
  );
  assert.equal(
    await findExecutable("./hsc-test", {
      cwd: "/workspace/bin",
      environment: { PATH: "" },
      fileSystem: linuxFileSystem,
      platform: "linux",
    }),
    linuxCompiler,
  );
  assert.equal(
    await findExecutable("missing-hsc", {
      environment: { PATH: "/workspace/bin" },
      fileSystem: linuxFileSystem,
      platform: "linux",
    }),
    undefined,
  );

  const windowsCompiler = "C:\\toolchain\\hsc-test.EXE";
  assert.equal(
    await findExecutable("hsc-test", {
      environment: { Path: "C:\\toolchain", PATHEXT: ".EXE" },
      fileSystem: executableFileSystem(new Set([windowsCompiler])),
      platform: "win32",
    }),
    windowsCompiler,
  );
});

test("build plan rejects path escape and extension-controlled arguments", () => {
  assert.throws(
    () => createBuildPlan(basePlanOptions({ outputDirectory: "../outside" })),
    (error) => error instanceof BuildPlanError && error.code === "output-outside-workspace",
  );
  assert.throws(
    () => createBuildPlan(basePlanOptions({ sourcePath: "/outside/demo.hs" })),
    (error) => error instanceof BuildPlanError && error.code === "source-outside-workspace",
  );

  for (const argument of ["-o", "--checked", "--dump-ast", "-E", "--version"]) {
    assert.throws(
      () => validateAdditionalArgs([argument]),
      (error) => error instanceof BuildPlanError && error.code === "controlled-additional-arg",
    );
  }
});

test("debug plan injects one -g and preserves program argv without shell parsing", () => {
  assert.deepEqual(
    normalizeDebugBuildArgs([
      "--checked",
      "-g",
      "--timing",
      "-g",
      "/workspace/main.hs",
      "-o",
      "/workspace/main",
    ]),
    [
      "-g",
      "--checked",
      "--timing",
      "/workspace/main.hs",
      "-o",
      "/workspace/main",
    ],
  );
  assert.deepEqual(validateDebugArguments(["", "argument with spaces", "$()"]), [
    "",
    "argument with spaces",
    "$()",
  ]);
  assert.throws(
    () => validateDebugArguments(["ok", 42]),
    (error) => error instanceof DebugPlanError &&
      error.code === "invalid-debug-argument",
  );
  assert.throws(
    () => validateDebugArguments(["bad\0argument"]),
    (error) => error instanceof DebugPlanError &&
      error.code === "nul-debug-argument",
  );
  assert.throws(
    () => validateGdbPath(""),
    (error) => error instanceof DebugPlanError &&
      error.code === "invalid-gdb-path",
  );

  assert.deepEqual(createCppdbgLaunchConfiguration({
    program: "/workspace/.hitsimple/build/main",
    args: ["one", "two words"],
    cwd: "/workspace",
    gdbPath: "/usr/bin/gdb",
  }), {
    type: "cppdbg",
    request: "launch",
    name: "HitSimple: Debug Current File",
    program: "/workspace/.hitsimple/build/main",
    args: ["one", "two words"],
    cwd: "/workspace",
    MIMode: "gdb",
    miDebuggerPath: "/usr/bin/gdb",
    console: "integratedTerminal",
  });
});

function createTaskEvents({
  exitCode,
  early = false,
  unrelated = false,
  taskEndOnly = false,
}) {
  const execution = { id: "target" };
  let processListener;
  let taskListener;
  let disposed = false;
  const tasksApi = {
    onDidEndTaskProcess(callback) {
      processListener = callback;
      return {
        dispose() {
          disposed = true;
        },
      };
    },
    onDidEndTask(callback) {
      taskListener = callback;
      return {
        dispose() {
          disposed = true;
        },
      };
    },
    async executeTask() {
      if (unrelated) {
        processListener({ execution: { id: "other" }, exitCode: 99 });
        taskListener({ execution: { id: "other" } });
      }
      const notify = taskEndOnly
        ? () => taskListener({ execution })
        : () => {
            processListener({ execution, exitCode });
            taskListener({ execution });
          };
      if (early) {
        notify();
      } else {
        setImmediate(notify);
      }
      return execution;
    },
  };
  return {
    execution,
    tasksApi,
    wasDisposed: () => disposed,
  };
}

test("task runner correlates normal, early, unrelated, and cancelled events", async () => {
  for (const scenario of [
    { exitCode: 0 },
    { exitCode: 7, early: true },
    { exitCode: undefined, unrelated: true },
  ]) {
    const events = createTaskEvents(scenario);
    const result = await executeProcessTask(events.tasksApi, { name: "task" });
    assert.equal(result.execution, events.execution);
    assert.equal(result.exitCode, scenario.exitCode);
    assert.equal(result.processStarted, true);
    assert.equal(events.wasDisposed(), true);
  }

  const noProcess = createTaskEvents({ taskEndOnly: true, early: true });
  const result = await executeProcessTask(noProcess.tasksApi, { name: "task" });
  assert.equal(result.execution, noProcess.execution);
  assert.equal(result.exitCode, undefined);
  assert.equal(result.processStarted, false);
  assert.equal(noProcess.wasDisposed(), true);
});

test("task factory uses ProcessExecution argv and the contributed matcher", () => {
  class ProcessExecution {
    constructor(process, args, options) {
      this.process = process;
      this.args = args;
      this.options = options;
    }
  }
  class Task {
    constructor(definition, scope, name, source, execution, problemMatchers) {
      Object.assign(this, {
        definition,
        scope,
        name,
        source,
        execution,
        problemMatchers,
      });
    }
  }
  const vscodeApi = {
    ProcessExecution,
    Task,
    TaskGroup: { Build: "build-group" },
    TaskPanelKind: { Dedicated: "dedicated" },
    TaskRevealKind: { Always: "always" },
    tasks: {},
  };
  const runner = createTaskRunner(vscodeApi);
  const plan = createBuildPlan(basePlanOptions({
    additionalArgs: ["argument with spaces", ";", "$()"],
  }));
  const workspaceFolder = { name: "workspace" };

  const buildTask = runner.createBuildTask(plan, workspaceFolder);
  assert.equal(buildTask.execution.process, "hsc");
  assert.deepEqual(buildTask.execution.args, plan.args);
  assert.equal(buildTask.problemMatchers, "$hsc");
  assert.equal(buildTask.group, "build-group");
  assert.equal(buildTask.presentationOptions.panel, "dedicated");

  const runTask = runner.createRunTask({ plan, workspaceFolder });
  assert.equal(runTask.execution.process, plan.outputPath);
  assert.deepEqual(runTask.execution.args, []);
  assert.equal(runTask.problemMatchers, undefined);
});

function createWorkflowHarness(options = {}) {
  const calls = [];
  const errors = [];
  const infos = [];
  const warnings = [];
  const document = {
    isUntitled: false,
    isDirty: false,
    languageId: "hitsimple",
    uri: {
      scheme: "file",
      fsPath: "/workspace/src/demo.hs",
    },
    async save() {
      calls.push("save");
      return true;
    },
    ...options.document,
  };
  const workspaceFolder = {
    name: "workspace",
    uri: {
      scheme: "file",
      fsPath: "/workspace",
    },
    ...options.workspaceFolder,
  };
  const values = {
    compilerPath: "/opt/HitSimple/build/hsc",
    mode: "checked",
    outputDirectory: ".hitsimple/build",
    additionalArgs: [],
    ...options.configuration,
  };
  const vscodeApi = {
    env: { remoteName: undefined },
    window: {
      activeTextEditor: { document },
      showErrorMessage(message) {
        errors.push(message);
      },
      showInformationMessage(message) {
        infos.push(message);
      },
      showWarningMessage(message) {
        warnings.push(message);
      },
    },
    workspace: {
      isTrusted: options.isTrusted ?? true,
      getWorkspaceFolder() {
        return options.hasWorkspace === false ? undefined : workspaceFolder;
      },
      getConfiguration() {
        return {
          get(key, fallback) {
            return values[key] ?? fallback;
          },
        };
      },
    },
  };
  const fileSystem = {
    async rm(filePath, rmOptions) {
      calls.push(["rm", filePath, rmOptions]);
    },
    async mkdir(directory, mkdirOptions) {
      calls.push(["mkdir", directory, mkdirOptions]);
      if (options.prepareError) {
        throw new Error("EACCES");
      }
    },
    async stat(filePath) {
      calls.push(["stat", filePath]);
      if (options.missingOutput) {
        throw new Error("ENOENT");
      }
      return { isFile: () => options.outputIsFile !== false };
    },
    async access(filePath, mode) {
      calls.push(["access", filePath, mode]);
    },
  };
  const runner = {
    createBuildTask(plan, folder) {
      calls.push(["create-build", plan, folder]);
      return { kind: "build", plan };
    },
    createRunTask(result) {
      calls.push(["create-run", result.outputPath]);
      return { kind: "run", result };
    },
    async executeProcessTask(task) {
      calls.push(["execute", task.kind]);
      if (task.kind === "build" && options.startBuildError) {
        throw new Error("ENOENT");
      }
      const exitCode = task.kind === "build"
        ? (options.buildExitCode ?? 0)
        : (options.runExitCode ?? 0);
      return { execution: { task }, exitCode };
    },
  };
  const workflows = createWorkflows(vscodeApi, {
    fileSystem,
    findExecutable: async () => options.compilerFound === false
      ? undefined
      : values.compilerPath,
    platform: "linux",
    taskRunner: runner,
  });
  return {
    calls,
    document,
    errors,
    infos,
    warnings,
    workflows,
  };
}

test("build workflow saves, removes stale output, builds, and verifies executable", async () => {
  const harness = createWorkflowHarness({ document: { isDirty: true } });
  const result = await harness.workflows.buildCurrentFile();

  assert.equal(result.ok, true);
  assert.equal(result.outputPath, "/workspace/.hitsimple/build/src/demo");
  assert.equal(harness.calls[0], "save");
  assert.deepEqual(harness.calls[1], [
    "rm",
    "/workspace/.hitsimple/build/src/demo",
    { force: true },
  ]);
  assert.deepEqual(harness.calls[2], [
    "mkdir",
    "/workspace/.hitsimple/build/src",
    { recursive: true },
  ]);
  assert.equal(harness.infos.length, 1);
  assert.deepEqual(harness.errors, []);
});

test("debug build normalizes configured -g flags before creating the task", async () => {
  const harness = createWorkflowHarness({
    configuration: { additionalArgs: ["-g", "--timing", "-g"] },
  });
  const result = await harness.workflows.buildCurrentFile({
    announceSuccess: false,
    debugInfo: true,
  });

  assert.equal(result.ok, true);
  assert.equal(result.plan.debugInfo, true);
  assert.deepEqual(result.plan.args, [
    "-g",
    "--checked",
    "--timing",
    "/workspace/src/demo.hs",
    "-o",
    "/workspace/.hitsimple/build/src/demo",
  ]);
});

test("run workflow is gated by build exit and output existence", async () => {
  const compileFailure = createWorkflowHarness({ buildExitCode: 2 });
  const failed = await compileFailure.workflows.runCurrentFile();
  assert.equal(failed.ok, false);
  assert.equal(failed.stage, "compile");
  assert.equal(
    compileFailure.calls.some((entry) => Array.isArray(entry) && entry[0] === "create-run"),
    false,
  );

  const missingOutput = createWorkflowHarness({ missingOutput: true });
  const missing = await missingOutput.workflows.runCurrentFile();
  assert.equal(missing.ok, false);
  assert.equal(missing.stage, "missing-output");

  const success = createWorkflowHarness({ runExitCode: 9 });
  const ran = await success.workflows.runCurrentFile();
  assert.equal(ran.stage, "run");
  assert.equal(ran.exitCode, 9);
  assert.equal(success.warnings.length, 1);
});

test("workflow reports output preparation and compiler startup failures", async () => {
  const unwritable = createWorkflowHarness({ prepareError: true });
  const prepareResult = await unwritable.workflows.buildCurrentFile();
  assert.equal(prepareResult.ok, false);
  assert.equal(prepareResult.stage, "prepare-output");
  assert.match(prepareResult.message, /EACCES/);

  const missingExecutable = createWorkflowHarness({ compilerFound: false });
  const missingExecutableResult = await missingExecutable.workflows.buildCurrentFile();
  assert.equal(missingExecutableResult.ok, false);
  assert.equal(missingExecutableResult.stage, "start-build");
  assert.match(missingExecutableResult.message, /找不到可执行的编译器/);

  const missingCompiler = createWorkflowHarness({ startBuildError: true });
  const startResult = await missingCompiler.workflows.buildCurrentFile();
  assert.equal(startResult.ok, false);
  assert.equal(startResult.stage, "start-build");
  assert.match(startResult.message, /compilerPath/);
});

test("workflow rejects unsafe editor and workspace contexts", async () => {
  const cases = [
    [{ document: { isUntitled: true } }, "untitled-document"],
    [{ document: { languageId: "haskell" } }, "wrong-language"],
    [{ isTrusted: false }, "untrusted-workspace"],
    [{ hasWorkspace: false }, "workspace-required"],
    [
      { document: { uri: { scheme: "file", fsPath: "/workspace/include.hsh" } } },
      "non-buildable-file",
    ],
    [
      { document: { uri: { scheme: "file", fsPath: "/workspace/module.hsi" } } },
      "non-buildable-file",
    ],
    [
      { workspaceFolder: { uri: { scheme: "memfs", fsPath: "/workspace" } } },
      "virtual-workspace",
    ],
  ];

  for (const [options, code] of cases) {
    const harness = createWorkflowHarness(options);
    const result = await harness.workflows.buildCurrentFile();
    assert.equal(result.ok, false);
    assert.equal(result.code, code);
  }
});

function createDebugHarness(options = {}) {
  const calls = [];
  const errors = [];
  const infos = [];
  const values = {
    gdbPath: "gdb",
    debugArguments: ["argument with spaces"],
    ...options.configuration,
  };
  const configuration = {
    get(key, fallback) {
      return values[key] ?? fallback;
    },
  };
  const build = options.build || {
    ok: true,
    outputPath: "/workspace/.hitsimple/build/src/demo",
    plan: { cwd: "/workspace" },
    workspaceFolder: { name: "workspace" },
    document: { uri: { fsPath: "/workspace/src/demo.hs" } },
    configuration,
  };
  const workflows = {
    async buildCurrentFile(buildOptions) {
      calls.push(["build", buildOptions]);
      return build;
    },
  };
  const vscodeApi = {
    window: {
      showErrorMessage(message) {
        errors.push(message);
      },
      showInformationMessage(message) {
        infos.push(message);
      },
    },
    workspace: {
      getConfiguration() {
        return configuration;
      },
    },
    extensions: {
      getExtension() {
        return options.cppTools === false ? undefined : {
          packageJSON: { contributes: { debuggers: [{ type: "cppdbg" }] } },
        };
      },
    },
    debug: {
      async startDebugging(workspaceFolder, launchConfiguration) {
        calls.push(["start-debug", workspaceFolder, launchConfiguration]);
        if (options.startDebugError) {
          throw new Error("debug adapter failed");
        }
        return options.startDebugging ?? true;
      },
    },
  };
  const debugWorkflow = createDebugWorkflow(vscodeApi, workflows, {
    platform: options.platform || "linux",
    architecture: options.architecture || "x64",
    findExecutable: async () => {
      if (options.gdbCheckError) {
        throw new Error("EACCES");
      }
      return options.gdbFound === false ? undefined : "/usr/bin/gdb";
    },
  });
  return { calls, debugWorkflow, errors, infos };
}

test("debug workflow builds first, launches cppdbg, and reports every preflight failure", async () => {
  const success = createDebugHarness();
  const result = await success.debugWorkflow.debugCurrentFile();
  assert.equal(result.ok, true);
  assert.equal(result.stage, "debug");
  assert.deepEqual(success.calls[0], ["build", {
    announceSuccess: false,
    debugInfo: true,
  }]);
  assert.equal(success.calls[1][0], "start-debug");
  assert.deepEqual(success.calls[1][2], result.launchConfiguration);
  assert.deepEqual(result.launchConfiguration.args, ["argument with spaces"]);
  assert.equal(result.launchConfiguration.miDebuggerPath, "/usr/bin/gdb");
  assert.equal(success.errors.length, 0);
  assert.equal(success.infos.length, 1);

  const buildFailure = createDebugHarness({ build: { ok: false, stage: "compile" } });
  const failedBuild = await buildFailure.debugWorkflow.debugCurrentFile();
  assert.equal(failedBuild.stage, "compile");
  assert.equal(buildFailure.calls.length, 1);

  const unsupported = createDebugHarness({ platform: "win32", architecture: "x64" });
  const unsupportedResult = await unsupported.debugWorkflow.debugCurrentFile();
  assert.equal(unsupportedResult.stage, "unsupported-platform");
  assert.equal(unsupported.calls.length, 1);

  const badArguments = createDebugHarness({ configuration: { debugArguments: "bad" } });
  const badArgumentsResult = await badArguments.debugWorkflow.debugCurrentFile();
  assert.equal(badArgumentsResult.stage, "debug-validation");
  assert.equal(badArgumentsResult.code, "invalid-debug-arguments");

  const badGdbPath = createDebugHarness({ configuration: { gdbPath: "" } });
  const badGdbPathResult = await badGdbPath.debugWorkflow.debugCurrentFile();
  assert.equal(badGdbPathResult.stage, "debug-validation");
  assert.equal(badGdbPathResult.code, "invalid-gdb-path");

  const missingGdb = createDebugHarness({ gdbFound: false });
  const missingGdbResult = await missingGdb.debugWorkflow.debugCurrentFile();
  assert.equal(missingGdbResult.stage, "gdb-check");
  assert.equal(missingGdb.calls.some((call) => call[0] === "start-debug"), false);

  const unreadableGdb = createDebugHarness({ gdbCheckError: true });
  const unreadableGdbResult = await unreadableGdb.debugWorkflow.debugCurrentFile();
  assert.equal(unreadableGdbResult.stage, "gdb-check");
  assert.equal(unreadableGdb.calls.some((call) => call[0] === "start-debug"), false);

  const missingCppTools = createDebugHarness({ cppTools: false });
  const missingCppToolsResult = await missingCppTools.debugWorkflow.debugCurrentFile();
  assert.equal(missingCppToolsResult.stage, "cppdbg-check");
  assert.equal(missingCppTools.calls.some((call) => call[0] === "start-debug"), false);

  const rejectedLaunch = createDebugHarness({ startDebugging: false });
  const rejectedLaunchResult = await rejectedLaunch.debugWorkflow.debugCurrentFile();
  assert.equal(rejectedLaunchResult.stage, "start-debug");

  const throwingLaunch = createDebugHarness({ startDebugError: true });
  const throwingLaunchResult = await throwingLaunch.debugWorkflow.debugCurrentFile();
  assert.equal(throwingLaunchResult.stage, "start-debug");
  assert.match(throwingLaunchResult.message, /debug adapter failed/);
});

test("extension registers and returns both commands", async () => {
  const registrations = new Map();
  const vscodeApi = {
    commands: {
      registerCommand(name, callback) {
        registrations.set(name, callback);
        return { dispose() {} };
      },
    },
  };
  const calls = [];
  const workflows = {
    async buildCurrentFile() {
      calls.push("build");
      return { ok: true };
    },
    async runCurrentFile() {
      calls.push("run");
      return { ok: true };
    },
  };
  const debugWorkflow = {
    async debugCurrentFile() {
      calls.push("debug");
      return { ok: true };
    },
  };
  const context = { subscriptions: [] };

  assert.equal(registerExtension(vscodeApi, context, workflows, debugWorkflow), workflows);
  assert.deepEqual([...registrations], [
    ["hitsimple.buildCurrentFile", registrations.get("hitsimple.buildCurrentFile")],
    ["hitsimple.runCurrentFile", registrations.get("hitsimple.runCurrentFile")],
    ["hitsimple.debugCurrentFile", registrations.get("hitsimple.debugCurrentFile")],
  ]);
  assert.equal(context.subscriptions.length, 3);
  await registrations.get("hitsimple.buildCurrentFile")();
  await registrations.get("hitsimple.runCurrentFile")();
  await registrations.get("hitsimple.debugCurrentFile")();
  assert.deepEqual(calls, ["build", "run", "debug"]);
});
