"use strict";

const path = require("node:path");

const MODE_FLAGS = Object.freeze({
  unchecked: "--unchecked",
  "static-checked": "--static-checked",
  checked: "--checked",
});

const CONTROLLED_ARGUMENTS = new Set([
  "--",
  "-h",
  "--help",
  "--version",
  "--target-info",
  "--unchecked",
  "--static-checked",
  "--checked",
  "-o",
  "--output",
  "--dump-tokens",
  "--dump-ast",
  "--dump-hir",
  "--emit-llvm",
  "-E",
  "--preprocess-only",
]);

class BuildPlanError extends Error {
  constructor(code, message) {
    super(message);
    this.name = "BuildPlanError";
    this.code = code;
  }
}

function pathApiForPlatform(platform) {
  return platform === "win32" ? path.win32 : path.posix;
}

function isOutside(rootPath, candidatePath, pathApi) {
  const relative = pathApi.relative(rootPath, candidatePath);
  return relative === ".." ||
    relative.startsWith(`..${pathApi.sep}`) ||
    pathApi.isAbsolute(relative);
}

function validateAdditionalArgs(additionalArgs) {
  if (!Array.isArray(additionalArgs)) {
    throw new BuildPlanError(
      "invalid-additional-args",
      "hitsimple.additionalArgs 必须是字符串数组。",
    );
  }

  return additionalArgs.map((argument, index) => {
    if (typeof argument !== "string") {
      throw new BuildPlanError(
        "invalid-additional-arg",
        `hitsimple.additionalArgs[${index}] 必须是字符串。`,
      );
    }
    if (argument.length === 0) {
      throw new BuildPlanError(
        "empty-additional-arg",
        `hitsimple.additionalArgs[${index}] 不能为空。`,
      );
    }
    if (argument.includes("\0")) {
      throw new BuildPlanError(
        "nul-additional-arg",
        `hitsimple.additionalArgs[${index}] 包含无效的 NUL 字符。`,
      );
    }
    if (CONTROLLED_ARGUMENTS.has(argument) ||
        argument.startsWith("--output=") ||
        argument.startsWith("-o=")) {
      throw new BuildPlanError(
        "controlled-additional-arg",
        `参数 ${JSON.stringify(argument)} 由扩展控制，不能放入 hitsimple.additionalArgs。`,
      );
    }
    return argument;
  });
}

function createBuildPlan(options) {
  const {
    sourcePath,
    workspacePath,
    compilerPath = "hsc",
    mode = "unchecked",
    outputDirectory = ".hitsimple/build",
    additionalArgs = [],
    platform = process.platform,
  } = options || {};
  const pathApi = pathApiForPlatform(platform);

  if (typeof sourcePath !== "string" || !pathApi.isAbsolute(sourcePath)) {
    throw new BuildPlanError(
      "invalid-source-path",
      "当前 HitSimple 文件必须具有可访问的绝对文件路径。",
    );
  }
  if (typeof workspacePath !== "string" || !pathApi.isAbsolute(workspacePath)) {
    throw new BuildPlanError(
      "invalid-workspace-path",
      "当前文件必须位于一个具有绝对路径的工作区目录中。",
    );
  }

  const normalizedSourcePath = pathApi.normalize(sourcePath);
  const normalizedWorkspacePath = pathApi.normalize(workspacePath);
  if (isOutside(normalizedWorkspacePath, normalizedSourcePath, pathApi)) {
    throw new BuildPlanError(
      "source-outside-workspace",
      "当前 HitSimple 文件不在选定的工作区目录中。",
    );
  }

  if (typeof compilerPath !== "string" || compilerPath.trim().length === 0) {
    throw new BuildPlanError(
      "invalid-compiler-path",
      "hitsimple.compilerPath 不能为空；请填写 hsc 或编译器的绝对路径。",
    );
  }

  const modeFlag = MODE_FLAGS[mode];
  if (!modeFlag) {
    throw new BuildPlanError(
      "invalid-mode",
      `不支持的 HitSimple 执行模式：${JSON.stringify(mode)}。`,
    );
  }

  if (typeof outputDirectory !== "string" || outputDirectory.trim().length === 0) {
    throw new BuildPlanError(
      "invalid-output-directory",
      "hitsimple.outputDirectory 不能为空。",
    );
  }

  let outputRoot;
  if (pathApi.isAbsolute(outputDirectory)) {
    outputRoot = pathApi.normalize(outputDirectory);
  } else {
    outputRoot = pathApi.resolve(normalizedWorkspacePath, outputDirectory);
    if (isOutside(normalizedWorkspacePath, outputRoot, pathApi)) {
      throw new BuildPlanError(
        "output-outside-workspace",
        "相对 hitsimple.outputDirectory 不能通过 .. 跳出工作区；如需外部目录，请配置绝对路径。",
      );
    }
  }

  const relativeSourcePath = pathApi.relative(
    normalizedWorkspacePath,
    normalizedSourcePath,
  );
  const parsedSource = pathApi.parse(relativeSourcePath);
  const executableSuffix = platform === "win32" ? ".exe" : "";
  const relativeOutputPath = pathApi.join(
    parsedSource.dir,
    `${parsedSource.name}${executableSuffix}`,
  );
  const outputPath = pathApi.join(outputRoot, relativeOutputPath);
  const outputParent = pathApi.dirname(outputPath);

  if (pathApi.normalize(outputPath) === normalizedSourcePath) {
    throw new BuildPlanError(
      "output-overwrites-source",
      "计算出的构建产物路径会覆盖源文件，请修改 hitsimple.outputDirectory。",
    );
  }

  const validatedAdditionalArgs = validateAdditionalArgs(additionalArgs);
  const args = [
    modeFlag,
    ...validatedAdditionalArgs,
    normalizedSourcePath,
    "-o",
    outputPath,
  ];

  return {
    compilerPath,
    mode,
    modeFlag,
    additionalArgs: validatedAdditionalArgs,
    args,
    sourcePath: normalizedSourcePath,
    workspacePath: normalizedWorkspacePath,
    relativeSourcePath,
    outputRoot,
    outputParent,
    outputPath,
    cwd: normalizedWorkspacePath,
    platform,
  };
}

module.exports = {
  BuildPlanError,
  CONTROLLED_ARGUMENTS,
  MODE_FLAGS,
  createBuildPlan,
  isOutside,
  pathApiForPlatform,
  validateAdditionalArgs,
};
