"use strict";

const { createWorkflows } = require("./workflows");

function registerExtension(vscodeApi, context, workflows = createWorkflows(vscodeApi)) {
  const build = vscodeApi.commands.registerCommand(
    "hitsimple.buildCurrentFile",
    () => workflows.buildCurrentFile(),
  );
  const run = vscodeApi.commands.registerCommand(
    "hitsimple.runCurrentFile",
    () => workflows.runCurrentFile(),
  );
  context.subscriptions.push(build, run);
  return workflows;
}

function activate(context) {
  return registerExtension(require("vscode"), context);
}

module.exports = {
  activate,
  registerExtension,
};
