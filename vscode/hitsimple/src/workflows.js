"use strict";

const fs = require("node:fs");
const fsPromises = require("node:fs/promises");

const { createBuildPlan } = require("./buildPlan");
const { normalizeDebugBuildArgs } = require("./debugPlan");
const { findExecutable } = require("./executable");
const { createTaskRunner } = require("./taskRunner");

class WorkflowError extends Error {
  constructor(code, message) {
    super(message);
    this.name = "WorkflowError";
    this.code = code;
  }
}

function errorText(error) {
  return error instanceof Error ? error.message : String(error);
}

function isSupportedWorkspaceFolder(vscodeApi, workspaceFolder) {
  const scheme = workspaceFolder && workspaceFolder.uri && workspaceFolder.uri.scheme;
  if (scheme === "file") {
    return true;
  }
  return scheme === "vscode-remote" && Boolean(vscodeApi.env.remoteName);
}

function isBuildableSourcePath(filePath) {
  return typeof filePath === "string" && /\.hs$/i.test(filePath);
}

function createWorkflows(vscodeApi, dependencies = {}) {
  const fileSystem = dependencies.fileSystem || fsPromises;
  const executableAccessMode = dependencies.executableAccessMode ?? fs.constants.X_OK;
  const platform = dependencies.platform || process.platform;
  const makeBuildPlan = dependencies.createBuildPlan || createBuildPlan;
  const locateExecutable = dependencies.findExecutable || findExecutable;
  const runner = dependencies.taskRunner || createTaskRunner(vscodeApi);

  async function resolveActiveContext() {
    const editor = vscodeApi.window.activeTextEditor;
    if (!editor) {
      throw new WorkflowError(
        "no-active-editor",
        "请先打开要构建的 HitSimple 文件。",
      );
    }

    const document = editor.document;
    if (document.isUntitled) {
      throw new WorkflowError(
        "untitled-document",
        "当前文件尚未保存；请先保存到工作区后再构建。",
      );
    }
    if (document.languageId !== "hitsimple") {
      throw new WorkflowError(
        "wrong-language",
        `当前编辑器语言模式是 ${document.languageId}；请切换为 HitSimple 后重试。`,
      );
    }
    if (vscodeApi.workspace.isTrusted !== true) {
      throw new WorkflowError(
        "untrusted-workspace",
        "Build 和 Run 会执行编译器，请先信任当前工作区。",
      );
    }

    const workspaceFolder = vscodeApi.workspace.getWorkspaceFolder(document.uri);
    if (!workspaceFolder) {
      throw new WorkflowError(
        "workspace-required",
        "当前文件不属于任何工作区；请打开文件所在目录后再构建。",
      );
    }
    if (!isSupportedWorkspaceFolder(vscodeApi, workspaceFolder)) {
      throw new WorkflowError(
        "virtual-workspace",
        "Build 和 Run 需要本地或远程文件系统工作区，当前虚拟工作区不受支持。",
      );
    }
    if (!document.uri.fsPath) {
      throw new WorkflowError(
        "missing-file-path",
        "当前文档没有可供 hsc 读取的文件系统路径。",
      );
    }
    if (!isBuildableSourcePath(document.uri.fsPath)) {
      throw new WorkflowError(
        "non-buildable-file",
        "Build 和 Run 只支持可编译的 .hs 源文件；.hsh 和 .hsi 用于声明或 include，请打开对应的 .hs 入口文件后重试。",
      );
    }

    if (document.isDirty) {
      const saved = await document.save();
      if (!saved) {
        throw new WorkflowError(
          "save-failed",
          "保存当前 HitSimple 文件失败，构建已取消。",
        );
      }
    }

    const configuration = vscodeApi.workspace.getConfiguration(
      "hitsimple",
      document.uri,
    );
    return {
      document,
      workspaceFolder,
      configuration,
    };
  }

  function planForContext(context, options = {}) {
    const plan = makeBuildPlan({
      sourcePath: context.document.uri.fsPath,
      workspacePath: context.workspaceFolder.uri.fsPath,
      compilerPath: context.configuration.get("compilerPath", "hsc"),
      mode: context.configuration.get("mode", "unchecked"),
      outputDirectory: context.configuration.get(
        "outputDirectory",
        ".hitsimple/build",
      ),
      additionalArgs: context.configuration.get("additionalArgs", []),
      platform,
    });
    if (options.debugInfo === true) {
      plan.args = normalizeDebugBuildArgs(plan.args);
      plan.debugInfo = true;
    }
    return plan;
  }

  function failed(stage, message, extra = {}) {
    vscodeApi.window.showErrorMessage(message);
    return {
      ok: false,
      stage,
      message,
      ...extra,
    };
  }

  async function buildCurrentFile(options = {}) {
    let context;
    let plan;
    try {
      context = await resolveActiveContext();
      plan = planForContext(context, options);
    } catch (error) {
      return failed("validation", errorText(error), {
        error,
        code: error && error.code,
      });
    }

    let resolvedCompilerPath;
    try {
      resolvedCompilerPath = await locateExecutable(plan.compilerPath, {
        cwd: plan.cwd,
        fileSystem,
        platform,
      });
    } catch (error) {
      return failed(
        "start-build",
        `无法检查编译器 ${plan.compilerPath}：${errorText(error)}。请检查 hitsimple.compilerPath 和 VS Code 的环境变量。`,
        { error, plan },
      );
    }
    if (!resolvedCompilerPath) {
      return failed(
        "start-build",
        `找不到可执行的编译器 ${plan.compilerPath}。请检查 hitsimple.compilerPath 和 VS Code 的环境变量。`,
        { plan },
      );
    }
    plan.resolvedCompilerPath = resolvedCompilerPath;

    try {
      await fileSystem.rm(plan.outputPath, { force: true });
      await fileSystem.mkdir(plan.outputParent, { recursive: true });
    } catch (error) {
      return failed(
        "prepare-output",
        `无法准备构建目录 ${plan.outputParent}：${errorText(error)}`,
        { error, plan },
      );
    }

    const task = runner.createBuildTask(plan, context.workspaceFolder);
    let processResult;
    try {
      processResult = await runner.executeProcessTask(task);
    } catch (error) {
      return failed(
        "start-build",
        `无法启动编译器 ${plan.compilerPath}：${errorText(error)}。请检查 hitsimple.compilerPath 和 VS Code 的环境变量。`,
        { error, plan, task },
      );
    }

    if (processResult.processStarted === false) {
      return failed(
        "start-build",
        `编译器进程 ${plan.compilerPath} 未能启动。请检查 hitsimple.compilerPath 和 VS Code 的环境变量。`,
        { plan, task, execution: processResult.execution },
      );
    }
    if (processResult.exitCode === undefined) {
      return failed(
        "build-cancelled",
        "HitSimple 构建已终止，未生成可运行产物。",
        { plan, task, execution: processResult.execution },
      );
    }
    if (processResult.exitCode !== 0) {
      return failed(
        "compile",
        `HitSimple 构建失败，退出码为 ${processResult.exitCode}。请查看任务终端和 Problems。`,
        {
          exitCode: processResult.exitCode,
          plan,
          task,
          execution: processResult.execution,
        },
      );
    }

    try {
      const outputStat = await fileSystem.stat(plan.outputPath);
      if (!outputStat.isFile()) {
        throw new WorkflowError(
          "output-not-file",
          "编译器返回成功，但目标路径不是普通文件。",
        );
      }
      if (platform !== "win32") {
        await fileSystem.access(plan.outputPath, executableAccessMode);
      }
    } catch (error) {
      return failed(
        "missing-output",
        `编译器返回成功，但没有生成可执行文件 ${plan.outputPath}：${errorText(error)}`,
        {
          error,
          exitCode: processResult.exitCode,
          plan,
          task,
          execution: processResult.execution,
        },
      );
    }

    const result = {
      ok: true,
      stage: "build",
      exitCode: processResult.exitCode,
      outputPath: plan.outputPath,
      plan,
      task,
      execution: processResult.execution,
      workspaceFolder: context.workspaceFolder,
      document: context.document,
      configuration: context.configuration,
    };
    if (options.announceSuccess !== false) {
      vscodeApi.window.showInformationMessage(
        `HitSimple 构建完成：${plan.outputPath}`,
      );
    }
    return result;
  }

  async function runCurrentFile() {
    const buildResult = await buildCurrentFile({ announceSuccess: false });
    if (!buildResult.ok) {
      return buildResult;
    }

    const task = runner.createRunTask(buildResult);
    let processResult;
    try {
      processResult = await runner.executeProcessTask(task);
    } catch (error) {
      return failed(
        "start-run",
        `无法启动 ${buildResult.outputPath}：${errorText(error)}`,
        { error, build: buildResult, task },
      );
    }

    if (processResult.processStarted === false) {
      return failed(
        "start-run",
        `程序进程 ${buildResult.outputPath} 未能启动。请检查构建产物权限和当前平台。`,
        { build: buildResult, task, execution: processResult.execution },
      );
    }
    if (processResult.exitCode === undefined) {
      vscodeApi.window.showWarningMessage("HitSimple 程序运行已终止。");
      return {
        ok: false,
        stage: "run-cancelled",
        exitCode: undefined,
        build: buildResult,
        task,
        execution: processResult.execution,
      };
    }
    if (processResult.exitCode !== 0) {
      vscodeApi.window.showWarningMessage(
        `HitSimple 程序已退出，退出码为 ${processResult.exitCode}。`,
      );
    }
    return {
      ok: processResult.exitCode === 0,
      stage: "run",
      exitCode: processResult.exitCode,
      build: buildResult,
      task,
      execution: processResult.execution,
    };
  }

  return {
    buildCurrentFile,
    planForContext,
    resolveActiveContext,
    runCurrentFile,
  };
}

module.exports = {
  WorkflowError,
  createWorkflows,
  errorText,
  isBuildableSourcePath,
  isSupportedWorkspaceFolder,
};
