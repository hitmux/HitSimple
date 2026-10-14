"use strict";

// cppvsdbg includes the source line in the displayed stack-frame name. Keep
// function identity exact and require that decoration to match DAP frame.line.
function frameHasFunctionName(frame, expected, adapterType) {
  const name = String(frame.name || "").split("!").at(-1);
  if (name === expected || name === `${expected}()`) {
    return true;
  }
  return adapterType === "cppvsdbg" && Number.isInteger(frame.line) && frame.line > 0 &&
    name === `${expected}() Line ${frame.line}`;
}

module.exports = { frameHasFunctionName };
