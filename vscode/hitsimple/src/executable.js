"use strict";

const fs = require("node:fs");
const fsPromises = require("node:fs/promises");
const path = require("node:path");

function pathApiForPlatform(platform) {
  return platform === "win32" ? path.win32 : path.posix;
}

function hasPathSeparator(command, platform) {
  return platform === "win32" ? /[\\/]/.test(command) : command.includes("/");
}

function windowsExtensions(command, environment) {
  if (path.win32.extname(command)) {
    return [""];
  }
  const pathExt = environment.PATHEXT || ".COM;.EXE;.BAT;.CMD";
  return ["", ...pathExt.split(";").filter(Boolean)];
}

async function isExecutable(filePath, fileSystem, platform) {
  try {
    const stat = await fileSystem.stat(filePath);
    if (!stat.isFile()) {
      return false;
    }
    await fileSystem.access(
      filePath,
      platform === "win32" ? fs.constants.F_OK : fs.constants.X_OK,
    );
    return true;
  } catch {
    return false;
  }
}

async function findExecutable(command, options = {}) {
  const {
    cwd = process.cwd(),
    environment = process.env,
    fileSystem = fsPromises,
    platform = process.platform,
  } = options;
  const pathApi = pathApiForPlatform(platform);
  const extensions = platform === "win32"
    ? windowsExtensions(command, environment)
    : [""];
  const candidates = [];

  if (pathApi.isAbsolute(command) || hasPathSeparator(command, platform)) {
    const base = pathApi.isAbsolute(command)
      ? pathApi.normalize(command)
      : pathApi.resolve(cwd, command);
    candidates.push(...extensions.map((extension) => `${base}${extension}`));
  } else {
    const pathValue = platform === "win32"
      ? (environment.Path || environment.PATH || "")
      : (environment.PATH || "");
    for (const entry of pathValue.split(pathApi.delimiter)) {
      const directory = entry.replace(/^"|"$/g, "") || cwd;
      candidates.push(...extensions.map(
        (extension) => pathApi.join(directory, `${command}${extension}`),
      ));
    }
  }

  for (const candidate of candidates) {
    if (await isExecutable(candidate, fileSystem, platform)) {
      return candidate;
    }
  }
  return undefined;
}

module.exports = {
  findExecutable,
  hasPathSeparator,
  isExecutable,
  pathApiForPlatform,
  windowsExtensions,
};
