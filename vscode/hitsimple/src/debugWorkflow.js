"use strict";

const { findExecutable } = require("./executable");
const {
  createCppdbgLaunchConfiguration,
  createCppvsdbgLaunchConfiguration,
  hasDebugger,
  resolveDebugPlatform,
  SUPPORTED_PLATFORM_TEXT,
  validateDebugArguments,
  validateGdbPath,
  validateLldbPath,
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

    const debugPlatform = resolveDebugPlatform(platform, architecture);
    if (!debugPlatform) {
      return failed(
        "unsupported-platform",
        `Debug Current File 不支持当前平台 ${platform} ${architecture}。支持矩阵：${SUPPORTED_PLATFORM_TEXT}。`,
        { build: buildResult },
      );
    }

    const configuration = buildResult.configuration ||
      vscodeApi.workspace.getConfiguration("hitsimple", buildResult.document.uri);
    let debugArguments;
    try {
      debugArguments = validateDebugArguments(
        configuration.get("debugArguments", []),
      );
    } catch (error) {
      return failed(
        "debug-validation",
        errorText(error),
        { error, code: error && error.code, build: buildResult },
      );
    }

    let resolvedDebuggerPath;
    if (debugPlatform.adapterType === "cppdbg") {
      const settingName = debugPlatform.debuggerPathSetting;
      const configuredDebuggerPath = configuration.get(
        settingName,
        debugPlatform.defaultDebuggerPath,
      );
      const validatePath = debugPlatform.debuggerKind === "gdb"
        ? validateGdbPath
        : validateLldbPath;
      try {
        validatePath(configuredDebuggerPath);
      } catch (error) {
        return failed(
          "debug-validation",
          errorText(error),
          { error, code: error && error.code, build: buildResult },
        );
      }
      try {
        resolvedDebuggerPath = await locateExecutable(configuredDebuggerPath, {
          cwd: buildResult.plan.cwd,
          platform,
        });
      } catch (error) {
        return failed(
          `${debugPlatform.debuggerKind}-check`,
          `无法检查 ${debugPlatform.debuggerKind.toUpperCase()} ${configuredDebuggerPath}：${errorText(error)}。请检查 hitsimple.${settingName} 和 VS Code 的环境变量。`,
          { error, build: buildResult },
        );
      }
      if (!resolvedDebuggerPath) {
        return failed(
          `${debugPlatform.debuggerKind}-check`,
          `找不到可执行的 ${debugPlatform.debuggerKind.toUpperCase()} ${configuredDebuggerPath}。请安装 ${debugPlatform.debuggerKind.toUpperCase()} 或检查 hitsimple.${settingName}。`,
          { build: buildResult },
        );
      }
    }

    const cppToolsExtension = getCppToolsExtension();
    if (!hasDebugger(cppToolsExtension, debugPlatform.adapterType)) {
      return failed(
        `${debugPlatform.adapterType}-check`,
        `缺少 Microsoft C/C++ 扩展（ms-vscode.cpptools）的 ${debugPlatform.adapterType} 调试器；请安装并启用该扩展后重试。`,
        { build: buildResult },
      );
    }

    let launchConfiguration;
    try {
      launchConfiguration = debugPlatform.adapterType === "cppvsdbg"
        ? createCppvsdbgLaunchConfiguration({
          program: buildResult.outputPath,
          args: debugArguments,
          cwd: buildResult.plan.cwd,
        })
        : createCppdbgLaunchConfiguration({
          program: buildResult.outputPath,
          args: debugArguments,
          cwd: buildResult.plan.cwd,
          debuggerPath: resolvedDebuggerPath,
          miMode: debugPlatform.miMode,
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
          `VS Code 未能启动 ${debugPlatform.adapterType} 调试会话。请检查 C/C++ 扩展输出和调试配置。`,
          { build: buildResult, launchConfiguration },
        );
      }
    } catch (error) {
      return failed(
        "start-debug",
        `无法启动 ${debugPlatform.adapterType} 调试会话：${errorText(error)}`,
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
