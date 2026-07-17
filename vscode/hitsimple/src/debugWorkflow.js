"use strict";

const { findExecutable } = require("./executable");
const {
  createCppdbgLaunchConfiguration,
  hasCppdbgDebugger,
  isSupportedDebugPlatform,
  validateDebugArguments,
  validateGdbPath,
} = require("./debugPlan");

function errorText(error) {
  return error instanceof Error ? error.message : String(error);
}

function createDebugWorkflow(vscodeApi, workflows, dependencies = {}) {
  const platform = dependencies.platform || process.platform;
  const architecture = dependencies.architecture || process.arch;
  const locateExecutable = dependencies.findExecutable || findExecutable;
  const getCppToolsExtension = dependencies.getCppToolsExtension || (() =>
    vscodeApi.extensions && vscodeApi.extensions.getExtension(
      "ms-vscode.cpptools",
    ));

  function failed(stage, message, extra = {}) {
    vscodeApi.window.showErrorMessage(message);
    return {
      ok: false,
      stage,
      message,
      ...extra,
    };
  }

  async function debugCurrentFile() {
    const buildResult = await workflows.buildCurrentFile({
      announceSuccess: false,
      debugInfo: true,
    });
    if (!buildResult.ok) {
      return buildResult;
    }

    if (!isSupportedDebugPlatform(platform, architecture)) {
      return failed(
        "unsupported-platform",
        `Debug Current File 仅支持 Linux x86_64；当前平台为 ${platform} ${architecture}。`,
        { build: buildResult },
      );
    }

    const configuration = buildResult.configuration ||
      vscodeApi.workspace.getConfiguration("hitsimple", buildResult.document.uri);
    let debugArguments;
    let gdbPath;
    try {
      debugArguments = validateDebugArguments(
        configuration.get("debugArguments", []),
      );
      gdbPath = validateGdbPath(configuration.get("gdbPath", "gdb"));
    } catch (error) {
      return failed(
        "debug-validation",
        errorText(error),
        { error, code: error && error.code, build: buildResult },
      );
    }

    let resolvedGdbPath;
    try {
      resolvedGdbPath = await locateExecutable(gdbPath, {
        cwd: buildResult.plan.cwd,
        platform,
      });
    } catch (error) {
      return failed(
        "gdb-check",
        `无法检查 GDB ${gdbPath}：${errorText(error)}。请检查 hitsimple.gdbPath 和 VS Code 的环境变量。`,
        { error, build: buildResult },
      );
    }
    if (!resolvedGdbPath) {
      return failed(
        "gdb-check",
        `找不到可执行的 GDB ${gdbPath}。请安装 GDB 或检查 hitsimple.gdbPath。`,
        { build: buildResult },
      );
    }

    const cppToolsExtension = getCppToolsExtension();
    if (!hasCppdbgDebugger(cppToolsExtension)) {
      return failed(
        "cppdbg-check",
        "缺少 Microsoft C/C++ 扩展（ms-vscode.cpptools）的 cppdbg 调试器；请安装并启用该扩展后重试。",
        { build: buildResult },
      );
    }

    let launchConfiguration;
    try {
      launchConfiguration = createCppdbgLaunchConfiguration({
        program: buildResult.outputPath,
        args: debugArguments,
        cwd: buildResult.plan.cwd,
        gdbPath: resolvedGdbPath,
      });
    } catch (error) {
      return failed(
        "debug-validation",
        errorText(error),
        { error, code: error && error.code, build: buildResult },
      );
    }

    try {
      const started = await vscodeApi.debug.startDebugging(
        buildResult.workspaceFolder,
        launchConfiguration,
      );
      if (!started) {
        return failed(
          "start-debug",
          "VS Code 未能启动 GDB 调试会话。请检查 C/C++ 扩展输出和 GDB 配置。",
          { build: buildResult, launchConfiguration },
        );
      }
    } catch (error) {
      return failed(
        "start-debug",
        `无法启动 GDB 调试会话：${errorText(error)}`,
        { error, build: buildResult, launchConfiguration },
      );
    }

    vscodeApi.window.showInformationMessage(
      `HitSimple 调试已启动：${buildResult.outputPath}`,
    );
    return {
      ok: true,
      stage: "debug",
      build: buildResult,
      launchConfiguration,
    };
  }

  return {
    debugCurrentFile,
  };
}

module.exports = {
  createDebugWorkflow,
  errorText,
};
