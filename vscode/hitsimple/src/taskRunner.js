"use strict";

function executeProcessTask(tasksApi, task) {
  let execution;
  const earlyProcessEvents = [];
  const earlyTaskEvents = [];
  let resolveCompletion;
  let taskEndTimer;
  let completed = false;
  const completion = new Promise((resolve) => {
    resolveCompletion = resolve;
  });

  function finish(kind, event) {
    if (completed) {
      return;
    }
    completed = true;
    if (taskEndTimer) {
      clearTimeout(taskEndTimer);
      taskEndTimer = undefined;
    }
    resolveCompletion({ kind, event });
  }

  function handleProcessEnd(event) {
    if (!execution) {
      earlyProcessEvents.push(event);
      return;
    }
    if (event.execution === execution) {
      finish("process", event);
    }
  }

  function handleTaskEnd(event) {
    if (!execution) {
      earlyTaskEvents.push(event);
      return;
    }
    if (event.execution !== execution || completed || taskEndTimer) {
      return;
    }
    taskEndTimer = setTimeout(() => finish("task", event), 50);
  }

  const processSubscription = tasksApi.onDidEndTaskProcess(handleProcessEnd);
  const taskSubscription = tasksApi.onDidEndTask(handleTaskEnd);

  return tasksApi.executeTask(task).then(async (startedExecution) => {
    execution = startedExecution;
    const earlyProcessEvent = earlyProcessEvents.find(
      (event) => event.execution === execution,
    );
    if (earlyProcessEvent) {
      finish("process", earlyProcessEvent);
    } else {
      const earlyTaskEvent = earlyTaskEvents.find(
        (event) => event.execution === execution,
      );
      if (earlyTaskEvent) {
        handleTaskEnd(earlyTaskEvent);
      }
    }
    const result = await completion;
    return {
      execution,
      exitCode: result.kind === "process" ? result.event.exitCode : undefined,
      processStarted: result.kind === "process",
      event: result.event,
    };
  }).finally(() => {
    if (taskEndTimer) {
      clearTimeout(taskEndTimer);
    }
    processSubscription.dispose();
    taskSubscription.dispose();
  });
}

function configurePresentation(vscodeApi, task) {
  task.presentationOptions = {
    reveal: vscodeApi.TaskRevealKind.Always,
    panel: vscodeApi.TaskPanelKind.Dedicated,
    clear: true,
    focus: false,
  };
  return task;
}

function createTaskRunner(vscodeApi) {
  function createBuildTask(plan, workspaceFolder) {
    const execution = new vscodeApi.ProcessExecution(
      plan.resolvedCompilerPath || plan.compilerPath,
      plan.args,
      { cwd: plan.cwd },
    );
    const task = new vscodeApi.Task(
      {
        type: "hitsimple",
        command: "build",
        file: plan.sourcePath,
      },
      workspaceFolder,
      `Build ${plan.relativeSourcePath}`,
      "HitSimple",
      execution,
      "$hsc",
    );
    task.group = vscodeApi.TaskGroup.Build;
    task.detail = plan.outputPath;
    return configurePresentation(vscodeApi, task);
  }

  function createRunTask(buildResult) {
    const { plan, workspaceFolder } = buildResult;
    const execution = new vscodeApi.ProcessExecution(
      plan.outputPath,
      [],
      { cwd: plan.cwd },
    );
    const task = new vscodeApi.Task(
      {
        type: "hitsimple",
        command: "run",
        file: plan.sourcePath,
      },
      workspaceFolder,
      `Run ${plan.relativeSourcePath}`,
      "HitSimple",
      execution,
    );
    task.detail = plan.outputPath;
    return configurePresentation(vscodeApi, task);
  }

  return {
    createBuildTask,
    createRunTask,
    executeProcessTask(task) {
      return executeProcessTask(vscodeApi.tasks, task);
    },
  };
}

module.exports = {
  configurePresentation,
  createTaskRunner,
  executeProcessTask,
};
