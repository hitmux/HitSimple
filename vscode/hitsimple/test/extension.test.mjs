import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { access, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import test from "node:test";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const { INITIAL, Registry } = require("vscode-textmate");
const oniguruma = require("vscode-oniguruma");

const extensionRoot = path.resolve(fileURLToPath(new URL("..", import.meta.url)));
const repoRoot = path.resolve(extensionRoot, "../..");

async function readJson(relativePath) {
  return JSON.parse(await readFile(path.join(extensionRoot, relativePath), "utf8"));
}

function collectIncludes(value, includes = []) {
  if (Array.isArray(value)) {
    for (const item of value) {
      collectIncludes(item, includes);
    }
    return includes;
  }
  if (!value || typeof value !== "object") {
    return includes;
  }
  if (typeof value.include === "string") {
    includes.push(value.include);
  }
  for (const child of Object.values(value)) {
    collectIncludes(child, includes);
  }
  return includes;
}

let grammarPromise;

async function loadGrammar() {
  if (!grammarPromise) {
    grammarPromise = (async () => {
      const wasm = await readFile(require.resolve("vscode-oniguruma/release/onig.wasm"));
      await oniguruma.loadWASM(wasm);
      const registry = new Registry({
        onigLib: Promise.resolve({
          createOnigScanner(patterns) {
            return new oniguruma.OnigScanner(patterns);
          },
          createOnigString(source) {
            return new oniguruma.OnigString(source);
          },
        }),
        async loadGrammar(scopeName) {
          if (scopeName !== "source.hitsimple") {
            return null;
          }
          return readJson("syntaxes/hitsimple.tmLanguage.json");
        },
      });
      return registry.loadGrammar("source.hitsimple");
    })();
  }
  return grammarPromise;
}

async function tokenizeFixture() {
  return tokenizeFile(path.join(extensionRoot, "test/fixtures/syntax-surface.hs"));
}

async function tokenizeFile(filePath) {
  const grammar = await loadGrammar();
  assert.ok(grammar, "HitSimple TextMate grammar must load");

  const source = await readFile(filePath, "utf8");
  return tokenizeSource(grammar, source);
}

function tokenizeSource(grammar, source) {
  const lines = source.split(/\r?\n/);
  const tokenLines = [];
  let ruleStack = INITIAL;

  for (const line of lines) {
    const result = grammar.tokenizeLine(line, ruleStack);
    tokenLines.push({ line, tokens: result.tokens });
    ruleStack = result.ruleStack;
  }

  return tokenLines;
}

function scopesFor(tokenLines, lineNeedle, lexeme) {
  const lineEntry = tokenLines.find(({ line }) => line.includes(lineNeedle));
  assert.ok(lineEntry, `fixture line containing ${JSON.stringify(lineNeedle)} must exist`);

  const start = lineEntry.line.indexOf(lexeme);
  assert.notEqual(start, -1, `${JSON.stringify(lexeme)} must exist on fixture line`);
  const end = start + lexeme.length;
  const scopes = new Set();

  for (const token of lineEntry.tokens) {
    if (token.startIndex < end && token.endIndex > start) {
      for (const scope of token.scopes) {
        scopes.add(scope);
      }
    }
  }

  return scopes;
}

function expectScope(tokenLines, lineNeedle, lexeme, scope) {
  assert.ok(
    scopesFor(tokenLines, lineNeedle, lexeme).has(scope),
    `${JSON.stringify(lexeme)} must include scope ${scope}`,
  );
}

function snippetPrefix(snippet) {
  return Array.isArray(snippet.prefix) ? snippet.prefix[0] : snippet.prefix;
}

function expandSnippetBody(snippet) {
  const body = Array.isArray(snippet.body) ? snippet.body.join("\n") : snippet.body;
  return body
    .replace(/\$\{\d+:([^}]*)\}/g, "$1")
    .replace(/\$\{\d+\}/g, "")
    .replace(/\$\d+/g, "");
}

async function parseSnippetSource(source, name) {
  const directory = await mkdtemp(path.join(tmpdir(), "hitsimple-vscode-snippet-"));
  const sourcePath = path.join(directory, `${name}.hs`);
  try {
    await writeFile(sourcePath, source, "utf8");
    const result = runHsc(["--dump-ast", sourcePath]);
    assert.equal(result.status, 0, result.stderr);
    assert.match(result.stdout, /^TranslationUnit\b/m);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
}

function hscPath() {
  return process.env.HSC_PATH
    ? path.resolve(process.env.HSC_PATH)
    : path.join(repoRoot, "build/hsc");
}

function runHsc(args) {
  return spawnSync(hscPath(), args, {
    cwd: repoRoot,
    encoding: "utf8",
  });
}

test("contribution JSON files parse and all referenced paths exist", async () => {
  const manifest = await readJson("package.json");
  const language = await readJson("language-configuration.json");
  const grammar = await readJson("syntaxes/hitsimple.tmLanguage.json");
  const snippets = await readJson("snippets/hitsimple.json");

  assert.equal(manifest.name, "hitsimple-vscode");
  assert.equal(manifest.main, "./src/extension.js");
  assert.deepEqual(manifest.extensionKind, ["workspace"]);
  assert.equal(manifest.contributes.languages.length, 1);
  assert.equal(manifest.contributes.grammars.length, 1);
  assert.equal(manifest.contributes.snippets.length, 1);
  assert.equal(manifest.contributes.problemMatchers.length, 1);
  assert.deepEqual(
    manifest.contributes.commands.map(({ command }) => command),
    ["hitsimple.buildCurrentFile", "hitsimple.runCurrentFile"],
  );

  const settings = manifest.contributes.configuration.properties;
  assert.equal(settings["hitsimple.compilerPath"].default, "hsc");
  assert.equal(settings["hitsimple.compilerPath"].scope, "machine-overridable");
  assert.deepEqual(settings["hitsimple.mode"].enum, [
    "unchecked",
    "static-checked",
    "checked",
  ]);
  assert.equal(settings["hitsimple.outputDirectory"].default, ".hitsimple/build");
  assert.equal(settings["hitsimple.additionalArgs"].type, "array");
  assert.equal(manifest.capabilities.untrustedWorkspaces.supported, "limited");
  assert.equal(manifest.capabilities.virtualWorkspaces, false);

  const referencedPaths = [
    manifest.contributes.languages[0].configuration,
    manifest.contributes.grammars[0].path,
    manifest.contributes.snippets[0].path,
  ];
  for (const relativePath of referencedPaths) {
    await access(path.resolve(extensionRoot, relativePath));
  }

  assert.equal(grammar.scopeName, manifest.contributes.grammars[0].scopeName);
  for (const include of collectIncludes(grammar)) {
    if (include.startsWith("#")) {
      assert.ok(grammar.repository[include.slice(1)], `missing grammar repository entry ${include}`);
    }
  }

  assert.ok(Array.isArray(language.brackets) && language.brackets.length > 0);
  assert.doesNotThrow(() => new RegExp(language.wordPattern));

  assert.ok(Object.keys(snippets).length > 0);
  const prefixes = new Set();
  for (const [name, snippet] of Object.entries(snippets)) {
    assert.ok(typeof snippet.prefix === "string" || Array.isArray(snippet.prefix), name);
    assert.ok(typeof snippet.body === "string" || Array.isArray(snippet.body), name);
    assert.equal(typeof snippet.description, "string", name);
    const prefix = snippetPrefix(snippet);
    assert.ok(prefix, `${name} must define a non-empty primary prefix`);
    assert.ok(!prefixes.has(prefix), `duplicate snippet prefix ${prefix}`);
    prefixes.add(prefix);
  }
});

test("language configuration covers structural and preprocessor indentation", async () => {
  const language = await readJson("language-configuration.json");
  const { increaseIndentPattern, decreaseIndentPattern } = language.indentationRules;
  const increase = new RegExp(increaseIndentPattern);
  const decrease = new RegExp(decreaseIndentPattern);

  for (const line of ["func main() {", "$if ENABLED", "$ ifdef ENABLED", "$ifndef ENABLED", "$ elif OTHER", "$else"]) {
    assert.match(line, increase);
  }
  for (const line of ["}", "    }", "$elif OTHER", "$ else", "$endif", "$ endif"]) {
    assert.match(line, decrease);
  }
  for (const line of ["func main()", "$define ENABLED 1", "new value[4]"]) {
    assert.doesNotMatch(line, increase);
    assert.doesNotMatch(line, decrease);
  }

  for (const open of ["{", "[", "("]) {
    const pair = language.autoClosingPairs.find((candidate) => candidate.open === open);
    assert.ok(pair, `missing auto-closing pair for ${open}`);
    assert.deepEqual(pair.notIn, ["string", "comment"]);
  }
});

test("TextMate grammar tokenizes the Phase 1 syntax surface", async () => {
  const tokenLines = await tokenizeFixture();

  expectScope(
    tokenLines,
    "0b1010_0101",
    "0b1010_0101",
    "constant.numeric.integer.binary.hitsimple",
  );
  expectScope(
    tokenLines,
    "0o755",
    "0o755",
    "constant.numeric.integer.octal.hitsimple",
  );
  expectScope(
    tokenLines,
    "0xFF_00",
    "0xFF_00",
    "constant.numeric.integer.hexadecimal.hitsimple",
  );
  expectScope(
    tokenLines,
    "new decimal",
    "1_000",
    "constant.numeric.integer.decimal.hitsimple",
  );
  expectScope(
    tokenLines,
    "new fraction",
    ".5",
    "constant.numeric.float.hitsimple",
  );
  expectScope(
    tokenLines,
    "1.5e+2",
    "1.5e+2",
    "constant.numeric.float.hitsimple",
  );
  expectScope(
    tokenLines,
    "decimal %d+=",
    "%d+=",
    "keyword.operator.assignment.typed.hitsimple",
  );
  expectScope(
    tokenLines,
    "fraction %4f=",
    "%4f=",
    "keyword.operator.assignment.typed.hitsimple",
  );
  expectScope(
    tokenLines,
    "%100d+",
    "%100d+",
    "keyword.operator.arithmetic.typed.hitsimple",
  );
  expectScope(
    tokenLines,
    "decimal %d<<",
    "%d<<",
    "keyword.operator.bitwise.shift.typed.hitsimple",
  );
  expectScope(
    tokenLines,
    "decimal %d&",
    "%d&",
    "keyword.operator.bitwise.typed.hitsimple",
  );
  expectScope(
    tokenLines,
    "fraction %f**",
    "%f**",
    "keyword.operator.arithmetic.power.typed.hitsimple",
  );
  expectScope(
    tokenLines,
    "$ define",
    "define",
    "keyword.control.directive.hitsimple",
  );
  expectScope(
    tokenLines,
    "new text",
    "\\n",
    "constant.character.escape.hitsimple",
  );
  expectScope(
    tokenLines,
    "template Vec2",
    "template",
    "keyword.declaration.template.hitsimple",
  );
  expectScope(
    tokenLines,
    "impl Vec2",
    "impl",
    "keyword.declaration.impl.hitsimple",
  );
  expectScope(
    tokenLines,
    "op +",
    "op",
    "keyword.declaration.operator.hitsimple",
  );
  expectScope(
    tokenLines,
    "x[8] as f64",
    "as",
    "keyword.operator.interpretation.hitsimple",
  );
  expectScope(
    tokenLines,
    "op + (self",
    "self",
    "variable.language.self.hitsimple",
  );
  expectScope(
    tokenLines,
    "func scale",
    "mut",
    "storage.modifier.mut.hitsimple",
  );
  expectScope(
    tokenLines,
    "struct Pair",
    "Pair",
    "entity.name.type.struct.hitsimple",
  );
  expectScope(
    tokenLines,
    "template Vec2",
    "Vec2",
    "entity.name.type.template.hitsimple",
  );
  expectScope(
    tokenLines,
    "impl Vec2",
    "Vec2",
    "entity.name.type.impl.hitsimple",
  );
});

test("TextMate grammar follows the lexer typed-operator matrix", async () => {
  const grammar = await loadGrammar();
  assert.ok(grammar);
  const groups = [
    {
      scope: "keyword.operator.assignment.typed.hitsimple",
      operators: [
        "%d=", "%32d+=", "%d**=", "%d<<=", "%d&=",
        "%4f=", "%f+=", "%f**=", "%s=", "%b=",
      ],
    },
    {
      scope: "keyword.operator.arithmetic.additive.typed.hitsimple",
      operators: ["%d+", "%100d-", "%f+", "%4f-"],
    },
    {
      scope: "keyword.operator.arithmetic.multiplicative.typed.hitsimple",
      operators: ["%d*", "%d/", "%d%", "%f*", "%16f/"],
    },
    {
      scope: "keyword.operator.arithmetic.power.typed.hitsimple",
      operators: ["%d**", "%8f**"],
    },
    {
      scope: "keyword.operator.bitwise.shift.typed.hitsimple",
      operators: ["%d<<", "%64d>>"],
    },
    {
      scope: "keyword.operator.bitwise.typed.hitsimple",
      operators: ["%d&", "%4d|", "%d^"],
    },
  ];
  const source = groups.flatMap(({ operators }) => operators)
    .map((operator) => `lhs ${operator} rhs`)
    .join("\n");
  const tokenLines = tokenizeSource(grammar, source);

  for (const { scope, operators } of groups) {
    for (const operator of operators) {
      expectScope(tokenLines, `lhs ${operator} rhs`, operator, scope);
    }
  }
});

test("TextMate grammar covers comprehensive and template regression sources", async () => {
  const comprehensive = await tokenizeFile(
    path.join(repoRoot, "examples/comprehensive_project.hs"),
  );
  expectScope(
    comprehensive,
    "struct Metric",
    "Metric",
    "entity.name.type.struct.hitsimple",
  );
  expectScope(
    comprehensive,
    "template Vec2",
    "Vec2",
    "entity.name.type.template.hitsimple",
  );
  expectScope(
    comprehensive,
    "impl Vec2",
    "Vec2",
    "entity.name.type.impl.hitsimple",
  );
  expectScope(
    comprehensive,
    "op format",
    "op",
    "keyword.declaration.operator.hitsimple",
  );

  const templateOps = await tokenizeFile(
    path.join(repoRoot, "tests/cases/run/user_template_ops.hs"),
  );
  expectScope(
    templateOps,
    "result.x %f= lhs.x %f+ rhs.x",
    "%f+",
    "keyword.operator.arithmetic.additive.typed.hitsimple",
  );
  expectScope(
    templateOps,
    "op ==",
    "op",
    "keyword.declaration.operator.hitsimple",
  );
});

test("Phase 1 snippet expansions parse through the real hsc parser", async () => {
  await access(hscPath());
  const snippets = await readJson("snippets/hitsimple.json");
  const byPrefix = new Map(
    Object.values(snippets).map((snippet) => [snippetPrefix(snippet), snippet]),
  );
  const expand = (prefix) => {
    const snippet = byPrefix.get(prefix);
    assert.ok(snippet, `missing snippet prefix ${prefix}`);
    return expandSnippetBody(snippet);
  };
  const main = "func main() {\n    return 0\n}";
  const template = "template Name {\n    value[4] as i32\n}";
  const cases = new Map([
    ["template", `${expand("template")}\n${main}\n`],
    ["impl", `${template}\n${expand("impl")}\n${main}\n`],
    ["op", `${template}\nimpl Name {\n${expand("op")}\n}\n${main}\n`],
    ["method", `${template}\nimpl Name {\n${expand("method")}\n}\n${main}\n`],
    ["externv", `${expand("externv")}\n${main}\n`],
    ["set", `func main() {\n    new name as i32\n    ${expand("set")}\n    return 0\n}\n`],
    ["newas", `func main() {\n    ${expand("newas")}\n    return 0\n}\n`],
    ["newbytes", `func main() {\n    ${expand("newbytes")}\n    return 0\n}\n`],
    ["newcstr", `func main() {\n    ${expand("newcstr")}\n    return 0\n}\n`],
    [
      "newaddr",
      `func main() {\n    new target[4]\n    ${expand("newaddr")}\n    return 0\n}\n`,
    ],
    ["staticas", `func main() {\n    ${expand("staticas")}\n    return 0\n}\n`],
  ]);

  for (const [prefix, source] of cases) {
    await parseSnippetSource(source, `snippet-${prefix}`);
  }
});

test("$hsc matcher captures located samples and ignores unlocated output", async () => {
  const manifest = await readJson("package.json");
  const matcher = manifest.contributes.problemMatchers[0];
  const regexp = new RegExp(matcher.pattern.regexp);

  assert.equal(matcher.name, "hsc");
  assert.equal(matcher.applyTo, "allDocuments");
  assert.equal(matcher.fileLocation, "autoDetect");

  const relative = regexp.exec(
    "hsc: tests/cases/sample.hs:7:11: parser: error: unexpected token",
  );
  assert.deepEqual(relative?.slice(1), [
    "tests/cases/sample.hs",
    "7",
    "11",
    "error",
    "unexpected token",
  ]);

  const absolute = regexp.exec(
    "hsc: /tmp/sample.hs:2:4: lexer: warning: sample warning",
  );
  assert.deepEqual(absolute?.slice(1), [
    "/tmp/sample.hs",
    "2",
    "4",
    "warning",
    "sample warning",
  ]);
  assert.equal(regexp.test("hsc: sema: error: missing main"), false);
});

test("$hsc matcher covers real relative, absolute, include, sema, and unlocated diagnostics", async () => {
  await access(hscPath());
  const manifest = await readJson("package.json");
  const regexp = new RegExp(manifest.contributes.problemMatchers[0].pattern.regexp);

  const located = runHsc([
    "--c-compat",
    "--dump-ast",
    "tests/cases/compat/c_variadic_declaration.c",
  ]);
  assert.equal(located.status, 1, located.stderr);
  assert.equal(located.stdout, "");
  const locatedLine = located.stderr.trim().split(/\r?\n/).find((line) => regexp.test(line));
  assert.ok(locatedLine, located.stderr);
  assert.deepEqual(regexp.exec(locatedLine)?.slice(1), [
    "tests/cases/compat/c_variadic_declaration.c",
    "1",
    "36",
    "error",
    "expected C type",
  ]);

  const sema = runHsc([
    "--dump-hir",
    "tests/cases/run/try_catch_integer_to_float_rejected.hs",
  ]);
  assert.equal(sema.status, 1, sema.stderr);
  assert.equal(sema.stdout, "");
  const semaMatch = regexp.exec(sema.stderr.trim());
  assert.deepEqual(semaMatch?.slice(1), [
    path.join(repoRoot, "tests/cases/run/try_catch_integer_to_float_rejected.hs"),
    "3",
    "15",
    "error",
    "float operand is not a float expression",
  ]);

  const directory = await mkdtemp(path.join(tmpdir(), "hitsimple-vscode-diagnostics-"));
  try {
    const directPath = path.join(directory, "direct-error.hs");
    await writeFile(directPath, "func main(]\n", "utf8");
    const direct = runHsc(["--dump-ast", directPath]);
    assert.equal(direct.status, 1, direct.stderr);
    const directMatch = regexp.exec(direct.stderr.trim());
    assert.deepEqual(directMatch?.slice(1, 5), [
      directPath,
      "1",
      "11",
      "error",
    ]);
    assert.match(directMatch?.[5] ?? "", /^syntax error, unexpected \]/);

    const lexerPath = path.join(directory, "direct-lexer-error.hs");
    await writeFile(lexerPath, "@\n", "utf8");
    const directLexer = runHsc(["--dump-tokens", lexerPath]);
    assert.equal(directLexer.status, 1, directLexer.stderr);
    assert.deepEqual(regexp.exec(directLexer.stderr.trim())?.slice(1), [
      lexerPath,
      "1",
      "1",
      "error",
      "invalid token `@`",
    ]);

    const includePath = path.join(directory, "broken.hsi");
    const mainPath = path.join(directory, "include-error.hs");
    await writeFile(includePath, "func main(]\n", "utf8");
    await writeFile(mainPath, '$ include "broken.hsi"\n', "utf8");
    const included = runHsc(["--dump-ast", mainPath]);
    assert.equal(included.status, 1, included.stderr);
    const includedMatch = regexp.exec(included.stderr.trim());
    assert.deepEqual(includedMatch?.slice(1, 5), [
      includePath,
      "1",
      "11",
      "error",
    ]);
    assert.match(includedMatch?.[5] ?? "", /^syntax error, unexpected \]/);

    const lexerIncludePath = path.join(directory, "broken-lexer.hsi");
    const lexerMainPath = path.join(directory, "include-lexer-error.hs");
    await writeFile(lexerIncludePath, "#\n", "utf8");
    await writeFile(lexerMainPath, '$ include "broken-lexer.hsi"\n', "utf8");
    const includedLexer = runHsc(["--dump-ast", lexerMainPath]);
    assert.equal(includedLexer.status, 1, includedLexer.stderr);
    assert.deepEqual(regexp.exec(includedLexer.stderr.trim())?.slice(1), [
      lexerIncludePath,
      "1",
      "1",
      "error",
      "bare # is not valid HitSimple source",
    ]);

    const noMainPath = path.join(directory, "no-main.hs");
    await writeFile(noMainPath, "func helper() {\n    return 0\n}\n", "utf8");
    const unlocated = runHsc([noMainPath, "-o", path.join(directory, "no-main")]);
    assert.equal(unlocated.status, 1, unlocated.stderr);
    assert.equal(unlocated.stdout, "");
    assert.match(
      unlocated.stderr,
      /^hsc: sema: error: program must define a main function\s*$/,
    );
    assert.equal(regexp.test(unlocated.stderr.trim()), false);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});
