"use strict";

const DEBUG_INFO_FLAG = "-g";

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
    ...buildArguments.filter((argument) => argument !== DEBUG_INFO_FLAG),
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

function validateGdbPath(gdbPath) {
  if (typeof gdbPath !== "string" || gdbPath.trim().length === 0) {
    throw new DebugPlanError(
      "invalid-gdb-path",
      "hitsimple.gdbPath 不能为空；请填写 gdb 或其可执行文件路径。",
    );
  }
  if (gdbPath.includes("\0")) {
    throw new DebugPlanError(
      "nul-gdb-path",
      "hitsimple.gdbPath 包含无效的 NUL 字符。",
    );
  }
  return gdbPath;
}

function isSupportedDebugPlatform(platform, architecture) {
  return platform === "linux" && architecture === "x64";
}

function hasCppdbgDebugger(extension) {
  const debuggers = extension && extension.packageJSON &&
    extension.packageJSON.contributes &&
    extension.packageJSON.contributes.debuggers;
  return Array.isArray(debuggers) &&
    debuggers.some((debuggerContribution) =>
      debuggerContribution && debuggerContribution.type === "cppdbg");
}

function createCppdbgLaunchConfiguration(options) {
  const {
    program,
    args,
    cwd,
    gdbPath,
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
    type: "cppdbg",
    request: "launch",
    name,
    program,
    args: validateDebugArguments(args),
    cwd,
    MIMode: "gdb",
    miDebuggerPath: validateGdbPath(gdbPath),
    console: "integratedTerminal",
  };
}

module.exports = {
  DEBUG_INFO_FLAG,
  DebugPlanError,
  createCppdbgLaunchConfiguration,
  hasCppdbgDebugger,
  isSupportedDebugPlatform,
  normalizeDebugBuildArgs,
  validateDebugArguments,
  validateGdbPath,
};
