"use strict";

const path = require("node:path");

const DEBUG_INFO_FLAG = "-g";
const DEBUG_OPTIMIZATION_FLAG = "-O0";
const OPTIMIZATION_FLAGS = new Set(["-O0", "-O1", "-O2", "-O3", "-Os"]);
const DEBUG_PLATFORM_MATRIX = Object.freeze([
  Object.freeze({
    platform: "linux",
    architectures: Object.freeze(["x64", "arm64"]),
    adapterType: "cppdbg",
    debuggerKind: "gdb",
    miMode: "gdb",
    debuggerPathSetting: "gdbPath",
    defaultDebuggerPath: "gdb",
  }),
  Object.freeze({
    platform: "darwin",
    architectures: Object.freeze(["x64", "arm64"]),
    adapterType: "cppdbg",
    debuggerKind: "lldb",
    miMode: "lldb",
    debuggerPathSetting: "lldbPath",
    defaultDebuggerPath: "lldb",
  }),
  Object.freeze({
    platform: "win32",
    architectures: Object.freeze(["x64"]),
    adapterType: "cppvsdbg",
    debuggerKind: "visual-studio",
  }),
]);

const SUPPORTED_PLATFORM_TEXT =
  "Linux x86_64/aarch64 (cppdbg + GDB)、macOS arm64/x86_64 " +
  "(cppdbg + LLDB)、Windows x64 (cppvsdbg + CodeView/PDB)";

class DebugPlanError extends Error {
  constructor(code, message) {
    super(message);
    this.name = "DebugPlanError";
    this.code = code;
  }
}

function normalizeDebugBuildArgs(buildArguments) {
  if (!Array.isArray(buildArguments) ||
      buildArguments.some((argument) => typeof argument !== "string")) {
    throw new DebugPlanError(
      "invalid-build-arguments",
      "调试构建参数必须是字符串数组。",
    );
  }
  return [
    DEBUG_INFO_FLAG,
    DEBUG_OPTIMIZATION_FLAG,
    ...buildArguments.filter((argument) =>
      argument !== DEBUG_INFO_FLAG && !OPTIMIZATION_FLAGS.has(argument)),
  ];
}

function validateDebugArguments(debugArguments) {
  if (!Array.isArray(debugArguments)) {
    throw new DebugPlanError(
      "invalid-debug-arguments",
      "hitsimple.debugArguments 必须是字符串数组。",
    );
  }

  return debugArguments.map((argument, index) => {
    if (typeof argument !== "string") {
      throw new DebugPlanError(
        "invalid-debug-argument",
        `hitsimple.debugArguments[${index}] 必须是字符串。`,
      );
    }
    if (argument.includes("\0")) {
      throw new DebugPlanError(
        "nul-debug-argument",
        `hitsimple.debugArguments[${index}] 包含无效的 NUL 字符。`,
      );
    }
    return argument;
  });
}

function validateDebuggerPath(debuggerPath, settingName, debuggerName) {
  const errorName = settingName.replace(/[A-Z]/g, (letter) =>
    `-${letter.toLowerCase()}`);
  if (typeof debuggerPath !== "string" || debuggerPath.trim().length === 0) {
    throw new DebugPlanError(
      `invalid-${errorName}`,
      `hitsimple.${settingName} 不能为空；请填写 ${debuggerName} 或其可执行文件路径。`,
    );
  }
  if (debuggerPath.includes("\0")) {
    throw new DebugPlanError(
      `nul-${errorName}`,
      `hitsimple.${settingName} 包含无效的 NUL 字符。`,
    );
  }
  return debuggerPath;
}

function validateGdbPath(gdbPath) {
  return validateDebuggerPath(gdbPath, "gdbPath", "gdb");
}

function validateLldbPath(lldbPath) {
  return validateDebuggerPath(lldbPath, "lldbPath", "lldb");
}

function resolveDebugPlatform(platform, architecture) {
  return DEBUG_PLATFORM_MATRIX.find((entry) =>
    entry.platform === platform && entry.architectures.includes(architecture));
}

function isSupportedDebugPlatform(platform, architecture) {
  return Boolean(resolveDebugPlatform(platform, architecture));
}

function hasDebugger(extension, type) {
  const debuggers = extension && extension.packageJSON &&
    extension.packageJSON.contributes &&
    extension.packageJSON.contributes.debuggers;
  return Array.isArray(debuggers) &&
    debuggers.some((debuggerContribution) =>
      debuggerContribution && debuggerContribution.type === type);
}

function hasCppdbgDebugger(extension) {
  return hasDebugger(extension, "cppdbg");
}

function hasCppvsdbgDebugger(extension) {
  return hasDebugger(extension, "cppvsdbg");
}

function createCppdbgLaunchConfiguration(options) {
  const {
    program,
    args,
    cwd,
    debuggerPath,
    gdbPath,
    lldbPath,
    miMode = "gdb",
    name = "HitSimple: Debug Current File",
  } = options || {};
  if (typeof program !== "string" || program.length === 0 ||
      typeof cwd !== "string" || cwd.length === 0) {
    throw new DebugPlanError(
      "invalid-launch-path",
      "调试启动配置缺少程序或工作目录路径。",
    );
  }

  const selectedDebuggerPath = debuggerPath || gdbPath || lldbPath;
  const validatedDebuggerPath = miMode === "lldb"
    ? validateLldbPath(selectedDebuggerPath)
    : validateGdbPath(selectedDebuggerPath);
  return {
    type: "cppdbg",
    request: "launch",
    name,
    program,
    args: validateDebugArguments(args),
    cwd,
    MIMode: miMode,
    miDebuggerPath: validatedDebuggerPath,
    console: "integratedTerminal",
  };
}

function createCppvsdbgLaunchConfiguration(options) {
  const {
    program,
    args,
    cwd,
    name = "HitSimple: Debug Current File",
  } = options || {};
  if (typeof program !== "string" || program.length === 0 ||
      typeof cwd !== "string" || cwd.length === 0) {
    throw new DebugPlanError(
      "invalid-launch-path",
      "调试启动配置缺少程序或工作目录路径。",
    );
  }
  return {
    type: "cppvsdbg",
    request: "launch",
    name,
    program,
    args: validateDebugArguments(args),
    cwd,
    console: "integratedTerminal",
  };
}

function pdbPathForOutput(outputPath) {
  if (typeof outputPath !== "string" || outputPath.length === 0) {
    throw new DebugPlanError(
      "invalid-launch-path",
      "调试启动配置缺少程序路径。",
    );
  }
  const parsed = path.win32.parse(outputPath);
  return path.win32.join(parsed.dir, `${parsed.name}.pdb`);
}

module.exports = {
  DEBUG_INFO_FLAG,
  DEBUG_PLATFORM_MATRIX,
  SUPPORTED_PLATFORM_TEXT,
  DebugPlanError,
  createCppdbgLaunchConfiguration,
  createCppvsdbgLaunchConfiguration,
  hasCppdbgDebugger,
  hasCppvsdbgDebugger,
  hasDebugger,
  isSupportedDebugPlatform,
  normalizeDebugBuildArgs,
  pdbPathForOutput,
  resolveDebugPlatform,
  validateDebugArguments,
  validateGdbPath,
  validateLldbPath,
};
