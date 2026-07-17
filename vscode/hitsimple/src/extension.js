"use strict";

const { createWorkflows } = require("./workflows");
const { createDebugWorkflow } = require("./debugWorkflow");

function registerExtension(
  vscodeApi,
  context,
  workflows = createWorkflows(vscodeApi),
  debugWorkflow = createDebugWorkflow(vscodeApi, workflows),
) {
  const build = vscodeApi.commands.registerCommand(
    "hitsimple.buildCurrentFile",
    () => workflows.buildCurrentFile(),
  );
  const run = vscodeApi.commands.registerCommand(
    "hitsimple.runCurrentFile",
    () => workflows.runCurrentFile(),
  );
  const debug = vscodeApi.commands.registerCommand(
    "hitsimple.debugCurrentFile",
    () => debugWorkflow.debugCurrentFile(),
  );
  context.subscriptions.push(build, run, debug);
  return workflows;
}

function activate(context) {
  return registerExtension(require("vscode"), context);
}

module.exports = {
  activate,
  registerExtension,
};
