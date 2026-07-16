# HitSimple Standard 1.0.0-Beta.21

## 1. Introduction, Normative Terminology, and Design Boundaries

HitSimple (abbreviated **hs**) is a programming language centered on in-memory data. The language specifies only contiguous byte sequences, addresses, lengths, lifetimes, interpretation Views, and operation resolution; integers, floating-point numbers, strings, addresses, and structured layouts acquire semantics through explicit interpretation.

The fundamental philosophy of HitSimple is: **data itself is only memory; meaning comes from explicit interpretation**.

Interpretation templates are the default units that carry semantics. The standard template library provides fundamental templates. A compiler MAY use built-in fast paths for standard templates, but observable behavior MUST remain consistent with the semantics of the standard template library.

### 1.1 Normative Terminology

- **MUST**: a mandatory requirement that both implementations and programs are required to satisfy.
- **MUST NOT**: prohibited behavior.
- **SHOULD**: recommended behavior; an implementation or program that deviates SHOULD have a clear reason.
- **MAY**: permitted behavior.
- **Undefined behavior**: behavior for which this Standard specifies no result; a program MUST NOT depend on its manifestation.
- **Implementation-defined behavior**: behavior that an implementation MUST document.
- **Diagnostic**: an error or warning that a compiler, interpreter, or runtime MUST report.
- **Observable behavior**: compile-time diagnostics, runtime errors, expression-result lengths, expression-result templates, byte sequences written to memory, formatted output, return values, and process termination status that a program can observe.

### 1.2 Design Boundaries

HitSimple does not introduce a traditional object-oriented type system. Template default operations and template methods are only static semantic bindings on interpretation templates.

This Standard does not define the following mechanisms:

1. Implicit cross-template conversion.
2. Inheritance.
3. Virtual functions.
4. Dynamic dispatch.
5. Implicit constructors.
6. Implicit destructors.
7. Implicit `self`.
8. Implicit lifetime management.
9. Automatic boxing or unboxing.
10. Operation resolution that depends on runtime type information.

### 1.3 Standard Layering

This Standard is organized according to the following model:

```text
storage object -> lvalue View/rvalue View -> interpretation template -> operation resolution -> observable behavior
```

- A storage object defines a memory location, length, lifetime, and definition address.
- A View defines how one expression evaluation locates or copies bytes and carries an interpretation template.
- An interpretation template defines default assignment, default operations, comparison, formatting, and method resolution.
- The standard template library provides the normative semantics of fundamental templates.
- The C compatibility layer serves only as a syntax-translation layer and MUST be translated into core syntax before semantic analysis.

### 1.4 Normative Processing Pipeline

A conforming implementation MUST produce externally observable results equivalent to the following processing pipeline:

```text
source bytes and UTF-8 validation
-> preprocessing-directive scan, macro expansion, and file inclusion
-> core/compatibility-layer lexical analysis
-> C compatibility-layer syntax recognition
-> translation of the C compatibility layer into core syntax
-> core-syntax parsing
-> top-level name collection and impl merging
-> semantic analysis, template matching, and return-signature validation
-> execution-mode safety checks
-> lowering / execution
```

After preprocessing, the resulting text MUST satisfy the lexical rules of either the core language or the C compatibility layer. Translation of the C compatibility layer MUST occur before core semantic analysis. Legacy syntax, C declarators, C expressions, and linkage metadata in the translation result MUST NOT cross into the core semantic layer.

---

## 2. Platform Model, Execution Modes, and Minimum ABI Requirements

The semantics of HitSimple depend on an abstract execution platform. A conforming implementation MUST document the platform parameters listed in this chapter.

### 2.1 Platform Parameters

- **Byte size**: 1 byte MUST be 8 bits.
- **Pointer length `P`**: the system pointer length, typically 4 or 8 bytes. Return values from `&`, `alloc()`, `fopen()`, and similar operations all use `P` bytes. `[P]` MAY be used as a length specification, measured in bytes.
- **Byte order**: integers, floating-point values, and address values are represented in memory using the native byte order of the system. An implementation MUST document the byte order of the target platform.
- **Null address**: an all-zero `P`-byte address value represents the null address. Dereferencing a null address is undefined behavior. In static-checked and checked modes, a compile-time diagnostic MUST be produced when the condition can be proven statically. In checked mode, a runtime error MUST be reported when the condition can be detected at runtime.
- **File handle**: a file handle is an opaque `P`-byte value passed at the language level as a View whose length is `P` and whose template is `handle`. An all-zero `P`-byte `handle` represents a null handle. The `handle` behavior defined by the standard language layer includes passing and returning values through file-I/O functions, same-template default assignment, equality comparison, and formatting. An implementation MAY provide additional behavior when it documents that behavior explicitly.
- **Alignment**: language-level memory access is byte-addressed. Any additional alignment guarantees for addresses returned by `new`, `static`, and `alloc()` are implementation-defined.

### 2.2 External-Linkage ABI

An `extern` declaration declares a HitSimple external symbol. Symbol-name encoding, calling convention, register usage, stack layout, argument passing, return-value storage, multiple-return ABI, vararg ABI, and compatibility with the C ABI are implementation-defined and MUST be documented.

### 2.3 Execution Modes

HitSimple standardizes three execution modes. Execution modes modify only the safety-checking policy. Static-semantic diagnostics listed in Section 18.2, including syntax, name resolution, template matching, return signatures, and overload conflicts, MUST be performed in every mode.

1. **unchecked mode**: an implementation is not required to insert safety checks. Out-of-bounds access, division by zero, invalid dereference, double free, use after free, and similar cases are handled according to their respective clauses. Where the relevant clause provides no additional rule, the behavior is undefined.
2. **static-checked mode**: an implementation MUST perform every safety check that can be proven statically and MUST issue a compile-time diagnostic. An implementation MUST NOT insert runtime-checking code for safety checks. Dynamic behavior whose safety cannot be proven statically follows unchecked-mode behavior when this Standard imposes no other constraint. Static-checked mode MUST introduce no additional runtime overhead.
3. **checked mode**: an implementation MUST perform checks that can be proven statically and MUST report runtime errors for safety violations that can be detected at runtime.

For purposes of this section, safety checks include the memory- and runtime-value safety errors identified by this Standard, including out-of-bounds access, division by zero, null-address dereference, invalid dereference, invalid free, double free, use after free, dynamic-length mismatch, dynamic-View boundary errors, insufficient destination capacity in the standard library, `size * count` overflow, and a missing terminating byte in a `cstr`.

Template-operation resolution is part of static semantic analysis. A missing match, duplicate match, or conflicting match involving ordinary operators, template methods, or `impl op` MUST be handled as a compile-time diagnostic. The runtime handles only errors caused by dynamic lengths, dynamic boundaries, or runtime values. Where a later clause states that “a runtime error MUST be reported when detectable in checked mode,” static-checked mode applies only the statically provable subset and reports it as a compile-time diagnostic.

A conforming implementation MUST support unchecked mode. An implementation that supports static-checked or checked mode MUST document how that mode is enabled. An implementation that supports checked mode MUST document its error codes, its policy for converting errors into `throw`, and the extent to which boundaries can be detected for raw addresses, external addresses, file handles, and FFI addresses.

---

## 3. Lexical Specification

### 3.1 Character Set

Source files use UTF-8 encoding. An invalid UTF-8 byte sequence MUST produce a compile-time diagnostic. Comments MAY contain arbitrary Unicode characters. String and character literals MAY contain any Unicode scalar value and are ultimately stored as UTF-8 byte sequences. Out-of-range Unicode scalar values and surrogate code points MUST produce a compile-time diagnostic.

Identifiers MAY contain only `A-Z`, `a-z`, `0-9`, and `_`, and MUST NOT begin with a digit. Identifiers are case-sensitive. A standalone `_` is a discard target on the left-hand side of multiple assignment and MUST NOT be used as a declaration name, parameter name, function name, template name, member name, or ordinary reference name. Names consisting of `t` followed by one or more digits, such as `t0` and `t10`, are reserved for template-instance count literals and MUST NOT be used as ordinary identifiers. Identifiers that begin with an underscore and contain more than one character SHOULD be reserved for the implementation.

Spaces and horizontal tabs separate lexical tokens. Outside string and character literals, a line break produces a `NEWLINE` token. CRLF is treated as one `NEWLINE`; a standalone LF or CR is also treated as one `NEWLINE`. The semicolon `;` is an ordinary punctuation token. At positions specified in Chapter 4, both `;` and `NEWLINE` MAY terminate a statement or declaration.

### 3.2 Comments

A line comment begins with `//` and ends at the end of the line. A block comment begins with `/*` and ends with `*/`; nesting is not supported. An unterminated block comment MUST produce a compile-time diagnostic.

Comments are recognized only outside string and character literals. Comment removal occurs before core-language token recognition. The line break that terminates a line comment still produces a `NEWLINE`; line breaks inside a block comment produce an equal number of `NEWLINE` tokens according to the actual line count. Inside string and character literals, `//`, `/*`, and `*/` are treated as literal content.

### 3.3 Keywords

The supported keywords are as follows:

| Keyword | Purpose |
|---|---|
| `new` | Declare a local or global storage object |
| `static` | Declare a static storage object |
| `extern` | Declare an external symbol |
| `func` | Define a function or template method |
| `return` | Return from a function |
| `if` / `else` | Conditional statement |
| `for` / `while` | Loop statement |
| `continue` / `break` | Loop control |
| `goto` | Unconditional jump |
| `try` / `catch` / `throw` | Error handling |
| `true` / `false` | Boolean literal |
| `template` | Define a user template |
| `impl` | Define template default operations and template methods |
| `op` | Define a template default operation |
| `set` | Set a persistent default interpretation template |
| `none` | Clear an interpretation template |
| `as` | Specify an interpretation template |
| `sizeof` | Query template size at compile time |
| `self` | Reserved name for the first parameter of a method |

The reserved keywords are as follows:

| Keyword | Intended purpose |
|---|---|
| `mut` | Reserved for writable method parameters; its appearance in standard mode is handled as a diagnostic item |
| `switch` / `case` / `default` | Multi-way selection |
| `do` | do-while loop |
| `typedef` | Type alias |
| `enum` | Enumeration |
| `struct` | Compatibility-layer spelling equivalent to `template` |
| `union` | Union layout |
| `const` / `volatile` | Compatibility qualifiers |

`self` is a reserved name in method declarations. The first parameter of a method MAY be written as `self as Template`. This Standard provides no implicit `self`. Writable behavior is expressed through assignment targets, target borrowing by `op =`, or explicit address dereference.

### 3.4 Lexical Rules for Preprocessor Directives

Preprocessor directives that begin with `$` have special meaning during preprocessing. Standard directives include `$include`, `$define`, `$undef`, `$if`, `$ifdef`, `$ifndef`, `$elif`, `$else`, `$endif`, `$error`, `$warning`, and `$pragma`.

A macro name MUST be a valid identifier and MUST NOT be a supported keyword, reserved keyword, or standalone `_`. Macro expansion occurs before language-level lexical and syntactic analysis. Source text produced by macro expansion MUST satisfy the lexical rules of this chapter. `#` and `##` are valid as stringification and token-concatenation markers only in a preprocessor macro-replacement context. After macro expansion, any unconsumed `#` or `##` appearing in core-language lexical input MUST produce a compile-time diagnostic unless it occurs within a preprocessor directive.

### 3.5 Literals

#### 3.5.1 Integer Literals

Decimal, hexadecimal, octal, and binary integer literals are supported. The hexadecimal prefix is `0x`/`0X`, the binary prefix is `0b`/`0B`, and the octal prefix is `0o`/`0O`; prefix matching is case-insensitive. The digit separator `_` MAY appear only between two digits.

An integer-literal token itself represents a nonnegative integer magnitude. The standard range is `[0, 2^64-1]`; a value outside this range MUST produce a compile-time diagnostic unless the implementation documents it as an extension. The minus sign `-` is a unary operator. In a constant-expression context, the compiler MUST support folding `-M` into a negative integer constant if and only if `M <= 2^63`; accordingly, `-9223372036854775808` is a valid minimum signed 8-byte value.

The result of an integer literal is an rvalue View whose template is `none` and which carries an integer-interpretation attribute. A nonnegative magnitude preferentially uses the smallest signed width among 1, 2, 4, and 8 bytes that can represent its mathematical value. A magnitude in `[2^63, 2^64-1]` produces an 8-byte result carrying an unsigned-interpretation attribute. A negative constant uses the smallest signed width among 1, 2, 4, and 8 bytes that can represent its mathematical value.

#### 3.5.2 Floating-Point Literals

Conventional decimal notation and scientific notation are supported. Valid forms include `DIGITS '.' [DIGITS]`, `'.' DIGITS`, `DIGITS EXP`, `DIGITS '.' [DIGITS] EXP`, and `'.' DIGITS EXP`, where `EXP` consists of `e` or `E`, an optional sign, and at least one digit. The exponent marker is case-insensitive. Floating-point literals support `_` as a digit separator, and each separator MUST occur between two digits. `NaN`, `Inf`, and `Infinity` are handled as standard-library formatting results or implementation extensions and are not core floating-point literals.

The length of a floating-point literal is inferred from context. In the absence of context, it defaults to the 8-byte `f64` template.

#### 3.5.3 Character and String Literals

Character literals are enclosed in single quotation marks, and string literals are enclosed in double quotation marks. A character literal MAY contain multiple characters. After escape processing, the UTF-8 bytes are stored in written order. An empty character literal `''` MUST produce a compile-time diagnostic. A character literal forms an rvalue View whose template is `none` and whose length is the number of bytes after escape processing. A one-byte character literal carries an unsigned-integer interpretation attribute whose value equals that byte. A character literal longer than one byte carries only the raw byte sequence and no integer-interpretation attribute. The length of a string literal is the total number of character bytes plus a trailing `0x00`; it forms an rvalue View whose template is `cstr`.

The following escape sequences are supported: `\n`, `\t`, `\r`, `\\`, `\'`, `\"`, `\0`, `\xHH`, `\ooo`, and `\uXXXX`. Invalid escapes, out-of-range Unicode scalar values, and surrogate code points MUST produce a compile-time diagnostic.

HitSimple does not automatically concatenate adjacent string literals. `"A" "B"` is a syntax error.

#### 3.5.4 Boolean Literals

`true` and `false` have a length of 1 byte. The byte value of `true` is `0x01`, the byte value of `false` is `0x00`, and the interpretation template is `bool`.

### 3.6 Operators and Punctuation

Arithmetic operators: `+`, `-`, `*`, `/`, `%`, `**`.  
Relational operators: `<`, `>`, `<=`, `>=`, `==`, `!=`.  
Logical operators: `!`, `&&`, `||`.  
Bitwise operators: `&`, `|`, `^`, `~`, `<<`, `>>`.  
Assignment operators: `=`, `%d=`, `%f=`, `%s=`, `%b=`, `&=`.  
Increment and decrement: `++`, `--`.  
Other operators: address-of `&`, dereference `[N]*` / `[P]*` / `[sizeof(T)]*`, unsigned-interpretation suffix `?`, temporary interpretation View `as Template`, and `sizeof(Name)`.  
Punctuation: `(`, `)`, `[`, `]`, `{`, `}`, `,`, `;`, `:`, `.`.

Typed operators include `%d+`, `%8d+`, `%f+`, `%4f+`, and similar forms. A typed operator forms one lexical token in its entirety; `N` denotes the result width in bytes. At the lexical level, an integer width `N` is a decimal digit sequence, and at the semantic level its value is required to be greater than 0. At the lexical level, a floating-point width `N` is a decimal digit sequence, and at the semantic level it is restricted to 2, 4, 8, or 16. A template-instance count literal such as `t10` forms one dedicated lexical token when it occurs in `[t10]`. A token shape consisting of `t` followed by digits is reserved for this purpose and is not an ordinary `IDENT`. Lexical analysis MUST apply the longest-match rule.

A statement terminator is represented by a line break or semicolon. A simple declaration or simple statement MAY end with `NEWLINE` or `;`; a line break after a semicolon is treated only as an empty terminator. Consecutive blank lines and consecutive semicolons represent empty terminators. The two semicolons in a `for (init; condition; post)` header are loop-header separators and are not statement terminators. Preprocessor directives continue to end at a line break. Any amount of whitespace and any number of line breaks MAY appear between a preceding block and `else` or `catch`; a semicolon between the preceding block and `else`/`catch` terminates the preceding compound statement.

### 3.7 Operator Precedence

| Precedence | Operators | Associativity | Description |
|---:|---|---|---|
| 1 | postfix `?`, `[]`, `()`, `.`, method call | L | Postfix, indexing, call, member |
| 2 | unary `&`, `[N]*`, `!`, `~`, unary `-`, `sizeof` | R | Unary |
| 3 | `as Template` | - | Temporary interpretation View |
| 4 | `**` | R | Exponentiation |
| 5 | `*`, `/`, `%` | L | Multiplication, division, remainder |
| 6 | `+`, `-` | L | Addition and subtraction |
| 7 | `<<`, `>>` | L | Shift |
| 8 | `<`, `<=`, `>`, `>=` | L | Relational |
| 9 | `==`, `!=` | L | Equality comparison |
| 10 | binary `&` | L | Bitwise AND |
| 11 | `^` | L | Bitwise XOR |
| 12 | `|` | L | Bitwise OR |
| 13 | `&&` | L | Logical AND, short-circuiting |
| 14 | `||` | L | Logical OR, short-circuiting |
| 15 | `? :` | R | Ternary conditional |
| 16 | `=`, `%d=`, `%f=`, `%s=`, `%b=`, `&=` | R | Assignment |

A typed operator has the same precedence as its corresponding ordinary operator. `++` and `--` are statements and do not produce expression values.

---

## 4. Core Syntax

This chapter presents the core syntax skeleton. The complete EBNF appears in Appendix 20. An implementation MAY use any parser technology; every accepted program MUST conform to the semantics of this Standard.

### 4.1 Top-Level Declarations

Top-level declarations include function definitions, external declarations, global declarations, user-template definitions, and `impl` definitions.

```ebnf
program       = { external_decl | preprocessor_directive | terminator } ;
external_decl = function_def | extern_decl | global_decl | template_def | impl_def ;
terminator    = (NEWLINE | ";") { NEWLINE | ";" } ;
separator     = { NEWLINE | ";" } ;
newline_gap   = { NEWLINE } ;
```

Top-level names are collected across the entire translation unit before function bodies, method bodies, and expressions are analyzed. Standard templates, user templates, functions, external symbols, and global storage objects share one top-level namespace; a duplicate name MUST produce a compile-time diagnostic. Functions, external symbols, user templates, global storage objects, and `impl` definitions MAY be forward-referenced. Local names continue to be resolved according to lexical scope and declaration order. `terminator` denotes one or more line breaks or semicolons; `separator` denotes zero or more line breaks or semicolons; `newline_gap` denotes zero or more line breaks. `sizeof(Template)` dependencies in template layouts MUST form an acyclic static size graph; a cyclic dependency MUST produce a compile-time diagnostic.

### 4.1.1 Statement Terminators

Core syntax treats `NEWLINE` and `;` uniformly as statement terminators. At any position written as `terminator` in this Standard, a program MAY use a line break, a semicolon, or a combination of both to end the current declaration or statement. Consecutive terminators denote empty statements or empty separators and are ignored semantically.

```hs
new a as i32 = 1
new b as i32 = 2; new c as i32 = a + b;
if (c > 0) { print(c); }
```

The two semicolons in a `for (init; condition; post)` header serve only as `for` syntax separators and are not processed as statement terminators. Any amount of whitespace and any number of line breaks MAY appear between a preceding block and `else` or `catch`; inserting a semicolon terminates the preceding compound statement. A preprocessor directive continues to end at a physical line boundary and is terminated only by `NEWLINE`.

### 4.2 Declarations and Template Annotations

```ebnf
global_decl   = "new" decl_list terminator ;
local_decl    = ("new" | "static") decl_list terminator ;
decl_item     = IDENT [ length_spec ] [ template_mark ] [ init_assign_op expression ] ;
template_mark = "as" template_name ;
template_name = IDENT | "none" ;
```

Declaration-level `as` sets the persistent default interpretation template. Expression-level `as` produces a temporary interpretation View. A declaration MAY end with a line break or a semicolon. The form `new { a as i32, b as i32 }` denotes a declaration list under one declaration keyword and is semantically equivalent to declaring each `decl_item` in written order.

### 4.3 User Templates and `impl`

```ebnf
template_def    = "template" IDENT "{" separator { template_member terminator } "}" [ terminator ] ;
template_member = IDENT length_spec [ template_mark ] ;

impl_def        = "impl" IDENT "{" separator { impl_item [ terminator ] } "}" [ terminator ] ;
impl_item       = op_def | method_def ;
op_def          = "op" overloadable_operator "(" op_param_list ")" return_sig block ;
method_def      = "func" IDENT "(" [ method_param_list ] ")" [ return_sig ] block ;
```

After top-level name collection, `Name` in `impl Name` MUST resolve to a known template. In an ordinary user translation unit, an `impl` MAY refer only to a user template; a standard-template-library unit MAY provide normative `impl` definitions for standard templates. Both `op` resolution and method resolution are static.

### 4.4 Return Signatures

```ebnf
return_sig             = "->" "()"
                       | "->" return_item
                       | "->" "(" return_item { "," return_item } ")" ;
return_item            = IDENT [ return_item_ident_tail ]
                       | "none"
                       | return_template_mark
                       | length_spec [ template_mark ] ;
return_item_ident_tail = return_template_mark
                       | length_spec [ template_mark ] ;
return_template_mark   = "as" return_template_name ;
return_template_name   = IDENT | "none" ;
```

Both `-> f64` and `-> as f64` denote an anonymous return value whose template is `f64`. `-> ok as bool` denotes a named return value `ok` whose template is `bool`. When a return item begins with `IDENT` followed by `as` or a length specification, it MUST be parsed as a named return item. Only a standalone `IDENT` is parsed as template shorthand. Omitting a function or method return signature is equivalent to `-> ()`.

The length of every return item MUST be statically determinable. Template shorthand MAY refer only to a fixed-length standard template or a user template. `bytes`, `cstr`, and `none` MUST be accompanied by an explicit length, for example `-> [16] as none`. When a return item consists only of `length_spec` and omits `as`, its return template is `none`; therefore, `-> [16]` is equivalent to `-> [16] as none`, and `-> name[16]` is equivalent to `-> name[16] as none`. A parser MAY initially accept a return-template shape involving `none` and subsequently issue the semantic diagnostic required by this section. Signatures that omit the return length, including `-> none`, `-> as none`, `-> bytes`, `-> cstr`, and `-> name as none`, MUST produce a compile-time diagnostic. Dynamic-length results of standard-library built-ins are handled only through the meta-signatures in Chapter 14.

### 4.5 Expressions and Method Calls

`expr.method(arg0, arg1)` is the syntax for a template-method call. A method call is treated as static syntactic sugar: the expression on the left MUST carry a template, a matching method MUST exist in that template's `impl`, and the left-hand expression is passed as the first argument.

```hs
new len as f64 = v.length()
```

Semantic steps:

1. Evaluate `v` to obtain a View.
2. Read the interpretation template from that View, for example `Vec2`.
3. Find the `length` method in `impl Vec2`.
4. Pass the View of `v` as the first argument and evaluate the remaining arguments according to ordinary function-call rules.

When `.` is followed by `IDENT` and the next non-whitespace token is `(`, the parser MUST reduce the suffix as a method-call suffix, `method_suffix`. A member suffix followed by an ordinary call suffix does not form a template-method call. Core semantics also define no member-function pointer or member-function value. Core syntax provides no template-namespace function-call form such as `Vec2.length(v)`; that notation MAY appear only as explanatory pseudocode. Ordinary standard-library functions are parsed only as function calls. `length(x)` is a standard-library function call, while `x.length()` is valid only when the template `impl` of `x` explicitly defines `func length(self as Template)`.

### 4.6 Lvalue Restrictions

An `lvalue` MUST denote a writable memory region. Literals, function-call results, expressions with the `?` suffix, and `sizeof(...)` results are rvalues and MUST NOT be used as assignment, increment/decrement, or address-of targets. The left-hand side of `&=` MUST be a single `IDENT`.

### 4.7 Length Specifications

- `[N]` denotes a byte length. `N` MUST be a positive integer literal or a statically evaluable `sizeof(TemplateName)`.
- `[P]` denotes the platform pointer length.
- In a `new` or `static` declaration, `[tN]` denotes a template-instance declaration count and MAY be used only in a declaration with a valid user template. `N` MUST be a positive integer literal.
- In suffix position, `[tK]` denotes a template-instance access index and MAY be used only on a template-instance-array View. `K` is zero-based and MUST be less than the number of instances in the array.
- An occurrence of `[tN]` in a parameter, return item, `extern` declaration, template member, or ordinary dereference prefix MUST produce a compile-time diagnostic.
- `[0]`, the template-instance declaration count `[t0]`, and `[0]*expr` MUST produce a compile-time diagnostic. The template-instance access index `[t0]` denotes the first instance.
- `[P]*expr` and `[sizeof(TemplateName)]*expr` are standard dereference syntax; their lengths are the platform pointer length and the fixed template size, respectively. `*expr` and `[]*expr` are not standard dereference syntax. An implementation that accepts either form MUST document it as an extension.

### 4.8 `?` Parsing Rules

The parser recognizes ternary `? :` at the `conditional_expr` level. When the next non-whitespace token after `?` can begin an expression and a `:` occurs later at the same nesting level, that `?` is the ternary conditional operator. At every other position, `?` is the postfix unsigned-interpretation operator.

```hs
a? < b        // (a?) < b
a?b:c         // a ? b : c
(a?) ? b : c  // use the unsigned-interpretation result as the condition
```

The postfix `?` and ternary `? :` productions in the Appendix EBNF describe only their syntactic shapes. An implementation MUST disambiguate them at the parser level according to this section. `a?b:c` MUST be parsed as a ternary expression.

The semantics of the ternary conditional expression `cond ? then_expr : else_expr` are as follows:

1. Evaluate `cond` first and read its condition value according to the Boolean-test rules.
2. Completion of `cond` evaluation establishes a sequence point. Only the selected branch is then evaluated; the unselected branch produces no runtime side effects.
3. Both branches MUST still pass static semantic analysis. The result Views of the two branches MUST have the same template, and their lengths MUST be statically provable as equal. Failure to merge them statically MUST produce a compile-time diagnostic.
4. To merge different lengths or different templates, the program MUST explicitly use `as`, `resize_bytes()`, or a standard conversion function.
5. The result of the ternary expression is an rvalue copy of the selected branch. Its length and template follow the common View rules of the two branches, and the result does not retain the lvalue property of either branch.

---

## 5. Storage Objects, Addresses, and Lifetimes

This chapter defines only where memory resides, how long it lives, and how it is located.

### 5.1 Storage Objects

A storage object is a contiguous byte sequence with a length, a definition address, and a lifetime. A storage object itself does not retain traditional type state such as “integer,” “floating-point,” “string,” or “address.” Interpretation templates and Views affect only semantic interpretation; they do not alter the object's byte contents, definition address, or lifetime.

### 5.2 Names, Definition Addresses, and Binding Addresses

A `new` or `static` declaration creates a name binding. A name has:

- **Definition address**: the original address allocated at declaration time; it remains unchanged for the lifetime of the name.
- **Binding address**: the current address used when content is accessed through the name. At declaration time, the binding address equals the definition address; `&=` can modify the binding address.
- **Declared length**: the number of bytes used when reading or writing through the name.
- **Persistent default interpretation template**: a standard template, user template, or `none`.

Automatic scope-based release always applies to the name's definition address and is unaffected by `&=`. An explicit `free(ptr)` always releases a dynamic object according to the supplied address value. That value MUST equal the base address of a dynamic object returned by `alloc()`, `calloc()`, or a successful `realloc()`. Passing to `free()` the address of an internal `new` object, a `static` object, an external object, a temporary-region object on the stack, an interior address of a dynamic object, a slice address, or an offset address constitutes an invalid free.

### 5.3 `new` and `static`

Named storage objects internal to the core language MUST be declared using `new` or `static`; an `extern` variable declaration refers to an external storage object. In core syntax, only `new` can declare a global storage object outside a function. Inside a function or block, either `new` or `static` MAY be used. A file-scope `static` declaration in the C compatibility layer is translated into a `new` declaration before entering core semantics and carries implementation-documented internal-linkage metadata. That metadata is not part of core syntax.

```hs
new x as i32 = 100
new counter as u64 = 0
new buf[64] as bytes
new p as Point
new arr[t10] as Point

func demo_static() -> () {
    static local_counter as u64 = 0
}
```

When a declaration omits its length, the length is inferred according to the following rules:

1. A fixed-length standard template supplies the length.
2. A user template supplies `sizeof(Template)`.
3. When the result length of the initializer expression is statically determinable, that length is used.
4. A declaration using `cstr` or `bytes` with neither an explicit length nor an initializer expression MUST produce a compile-time diagnostic.

Bytes that are not initialized at declaration time are in an uninitialized state. Reading an uninitialized byte is undefined behavior. In static-checked and checked modes, a compile-time diagnostic MUST be produced when the read can be proven statically. In checked mode, a runtime error MUST be reported when the implementation can detect the read. Global `new` initializers execute in the order in which global declarations appear in the source file. When an initializer expression references a global object whose initialization has not completed, the rules for reading that object's uninitialized bytes apply normally.

A `static` object inside a function or block has program lifetime and block-scoped name visibility. Its storage is reserved before program startup or at an equivalent time. Its initializer expression executes exactly once, the first time control reaches the declaration, and the object remains initialized until program termination. A function-local `static` object without an initializer expression enters the created state the first time control reaches the declaration; the initialization state of its bytes continues to follow the declaration rules. Recursive entry into the same `static` declaration during initialization is undefined behavior. In static-checked and checked modes, a compile-time diagnostic MUST be produced when the condition can be proven statically; in checked mode, a runtime error MUST be reported when the condition can be detected. An implementation that provides a concurrent-execution extension MUST document its synchronization policy for function-local `static` initialization.

### 5.4 Dynamic-Allocation Lifetime

`alloc()`, `calloc()`, `realloc()`, and `free()` manage dynamic storage objects. A dynamic object has no language-level name; a program accesses it through address values. In checked mode, the runtime SHOULD track boundary metadata for dynamic storage objects. Static-checked mode MUST NOT introduce runtime metadata maintenance for dynamic-object boundary safety.

A non-null `ptr` argument to `free(ptr)` or `realloc(ptr, size)` MUST be the base address of a currently valid dynamic object. Passing an interior address, slice address, offset address, already-freed base address, an address of a `new`/`static`/`extern` object, or a non-dynamic FFI address to either function constitutes an invalid free or invalid reallocation.

Memory declared by `new` or `static` MUST NOT be passed to `free()` for manual release. Double free, use after free, and invalid free are undefined behavior. In static-checked and checked modes, a compile-time diagnostic MUST be produced when the condition can be proven statically; in checked mode, a runtime error MUST be reported when the condition can be detected.

### 5.5 Scope-Exit Release Rules

- A global `new` object exists for the entire program lifetime.
- A function-local `new` object has its definition address automatically released when execution leaves the scope containing its declaration.
- A function-local `static` object is released at program termination; visibility of its name remains limited by the block containing its declaration.
- When `return`, `throw`, `break`, `continue`, or `goto` leaves a block scope, already-created local `new` objects are released according to the ordinary scope rules.
- An inner scope MAY declare a name that is identical to a name in an outer scope. The inner name shadows the outer name within that scope.
- Re-declaring an identical local name, parameter name, label name, or static name in the same lexical scope MUST produce a compile-time diagnostic.

### 5.6 Address-Of

`&lvalue` returns the start address of a locatable lvalue. The result length is `P`, and its template is `addr`.

- `&IDENT` returns the name's current binding address.
- `&slice`, `&member`, `&index`, and `&deref_lvalue` return the start address of the corresponding region.
- Taking the address of an rvalue MUST produce a compile-time diagnostic.

Address arithmetic SHOULD use explicit unsigned interpretation:

```hs
new arr[64] as bytes
new offset as u64 = 8
new base as addr = &arr
new ptr as addr
ptr = base? + offset?
```

### 5.7 Dereference

`[L]*expr` interprets the byte result of `expr` as an address of system-pointer length and locates an `L`-byte region beginning at that address. `L` MAY be a positive integer literal, `P`, or a statically evaluable `sizeof(TemplateName)` whose result MUST be greater than 0.

- When read as an rvalue, the operation copies `L` bytes from the target address.
- When written as an lvalue, the operation writes the lowest `L` bytes of the source View to the target address.
- Dereferencing a null address, dangling address, or out-of-bounds address is undefined behavior. In static-checked and checked modes, a compile-time diagnostic MUST be produced when the condition can be proven statically; in checked mode, a runtime error MUST be reported when the condition can be detected.

### 5.8 Indexing and Slicing

`arr[index]` produces a one-byte lvalue View that locates the single byte at the start address of `arr` plus the `index` offset. `index` is first read as a signed integer; a negative value or a value greater than or equal to the object length is out of bounds.

Slice syntax:

```hs
arr[start:end]   // half-open range [start, end)
arr[start:+len]  // len bytes beginning at start
```

A slice produces an lvalue View into the original storage object. The length of a slice with static boundaries is statically determinable; the length of a slice with dynamic boundaries is determined at runtime. A dynamic-length slice MAY be assigned to an already-declared target, passed to a standard-library function, or used with `length()`. It MUST NOT be used in a declaration that omits its length.

### 5.9 Out-of-Bounds Access, Dangling Addresses, and Release Errors

In unchecked mode, out-of-bounds access, dereference of a dangling address, double free, use after free, and invalid free are undefined behavior. In static-checked mode, each such error that can be proven statically MUST produce a compile-time diagnostic; dynamic behavior that cannot be proven statically follows unchecked-mode behavior. In checked mode, a runtime error MUST be reported for cases detectable from object metadata. Boundary-detection capability for raw addresses, external addresses, and FFI addresses is implementation-defined.

---

## 6. Expression Results, Lvalue Views, and Interpretation Templates

This chapter defines the View model. Assignment, operations, template members, method calls, and standard-library functions in subsequent chapters all use Views as their input and output model.

### 6.1 View Model

Evaluation of an expression produces a View. A View has the following properties:

1. A byte sequence or locatable memory region.
2. A length.
3. An lvalue or rvalue property.
4. An optional interpretation template.
5. For an lvalue View, a start address and write permission.

A View does not create a traditional static type. An interpretation template affects only default assignment, default operations, comparison, formatting, member access, diagnostics, and length inference.

### 6.2 Lvalue Views

An lvalue View locates a writable memory region. Names, member access, indexing, slicing, and dereference can all produce lvalue Views. An lvalue View MAY be assigned to, have its address taken, or be passed to an operation that requires a writable target.

### 6.3 Rvalue Views

An rvalue View is the byte result obtained after expression evaluation. Literals, function return values, ordinary operation results, `sizeof()`, and results of the postfix `?` operator are rvalue Views. An rvalue View MUST NOT be assigned to, have its address taken, or be incremented or decremented.

### 6.4 View Length

A View's length comes from a declaration length, template length, literal length, slice range, dereference length, return signature, or operation definition. A View whose length is not statically determinable MAY still be used at runtime with an already-declared target or a standard-library function. Any position that requires a static length MUST produce a compile-time diagnostic when the length cannot be determined statically.

### 6.5 Interpretation Template Carried by a View

A View MAY carry a standard template, a user template, or `none`. Template sources include:

- Declaration-level `as`.
- A persistent template set by `set`.
- A template in a member declaration.
- Expression-level `as`.
- A function return signature.
- The return signature of a template `op`.
- A result of a standard-template-library operation.

### 6.6 Declaration-Level `as`

Declaration-level `as` sets the persistent default interpretation template of a storage object, function parameter, return item, template member, or external symbol.

```hs
new count as i32 = 0
new price as f64 = 19.99
new ptr[P] as addr = &price
```

A fixed-length template requires the declared length to match. A user template requires the declared length to equal `sizeof(Template)`. The variable-length templates `bytes` and `cstr` accept any positive length.

### 6.7 Expression-Level `as`

`expr as Template` produces a temporary interpretation View. When the operand is an lvalue, the result is a temporary lvalue View. When the operand is an rvalue, the result is a temporary rvalue View. The resulting View does not modify the persistent default interpretation template of the source object or source View.

`as` changes only the interpretation template carried by a View. It performs no numeric conversion, sign extension, zero extension, truncation, zero padding, byte reordering, address rebinding, or lifetime change.

A fixed-length template requires the result length of `expr` to match the template length. A user template requires the result length of `expr` to equal `sizeof(Template)`. `bytes` and `cstr` accept any positive length. `expr as none` clears the current View's temporary template.

```hs
new bits as u32 = 0x3F800000
new f as f32 = bits as f32

new raw[16] as bytes
new len as f64 = (raw as Vec2).length()
```

### 6.8 Persistent Templates Set by `set`

`set target as Template` changes the persistent default interpretation template of the target name or member chain. `set target as none` sets the persistent default interpretation template of the target name or member chain to `none`. `target` MUST be a statically locatable name or member chain:

```ebnf
set_target = IDENT { "." IDENT } ;
```

Examples:

```hs
new raw[4] as bytes
set raw as i32
raw = 100
set raw as none

new u as User
set u.id as u32
```

When setting a fixed-length template, the target length MUST match the template length. When setting a user template, the target length MUST equal `sizeof(Template)`. Using `set` on an index, slice, dereference, dynamic-length View, or temporary expression MUST produce a compile-time diagnostic. A dynamic View that requires temporary interpretation SHOULD use expression-level `as`.

A member-chain `set` applies only to metadata for the specified member path on that name binding; it is not bound to the address current at the time `set` executes. Even if the name is later rebound through `&=`, Views produced through that name and member chain continue to use the same override. Other names and raw-address aliases do not inherit the override. When a member View is read, resolution precedence is: expression-level `as`, member-chain `set` override, member-declaration template, then `none`. The persistent template of an object or name is used only to select the member layout and does not become the default View template for an unannotated member. Therefore, `set u.id as i32` overrides the template declared for `User.id`, while `set u.id as none` creates an explicit `none` override.

### 6.9 Template Propagation Rules

- Reading a name carries that name's persistent template; when a name-level `set` override exists, the override is used.
- An indexing result has length 1 and template `none`.
- An ordinary slice has template `none` by default.
- Template-member access first checks for a `set` override on the storage object's member chain. When present, the override is used; otherwise, the member-declaration template is used. An unannotated member produces template `none`.
- Expression-level `as` overrides the current View template.
- The postfix `?` operator produces an rvalue View whose template is `none` and which carries an unsigned-integer interpretation attribute.
- The template of an ordinary operation result is determined by the standard-template-library operation or the return signature of a user `op`.
- An explicit typed operator bypasses template default-operation resolution. Its result template is `none`, unless an assignment target or subsequent `as` specifies a template.

### 6.10 Temporary-View Lifetime

A temporary rvalue View expires at the end of the full expression. The locating capability of a temporary lvalue View does not extend the lifetime of the underlying storage object. Reading or writing through a temporary View formed from an expired storage object is undefined behavior. In static-checked and checked modes, a compile-time diagnostic MUST be produced when the condition can be proven statically; in checked mode, a runtime error MUST be reported when the condition can be detected.

---
## 7. Assignment, Conversion, and Byte Representation

This chapter addresses only how a destination View receives a source View.

### 7.1 Assignment Input Model

Assignment is represented as:

```text
dst View <- src View
```

The destination MUST be an lvalue View. The source MAY be an lvalue View or an rvalue View. An assignment operation MAY read source bytes, interpret the source value according to a template or explicit operator, and then write destination bytes.

### 7.2 Default Assignment `=`

Default assignment is determined by the template of the destination View:

1. When the destination carries a standard template, use the `op =` and acceptance rules defined by the standard template library.
2. When the destination carries a user template and a matching `op =` exists, use that operation.
3. When a user template defines no `op =`, assignment between identical templates performs a byte-for-byte copy.
4. Default assignment between different user templates MUST produce a compile-time diagnostic.
5. When the destination template is `none`, use raw-integer assignment semantics.

When the destination template is `none`, source acceptance for raw-integer assignment is as follows: integer literals, `none` Views carrying an integer-interpretation attribute, and Views with templates `iN`, `uN`, `bool`, or `addr` MAY be accepted under integer-assignment semantics. An `fN`, `cstr`, `bytes`, user-template View, or `none` View without an interpretation attribute MUST first be given an acceptable integer interpretation through a standard conversion function, the postfix `?` operator, or an explicit assignment operation. A source outside this matrix MUST produce a compile-time diagnostic. A length mismatch caused by a dynamic source is checked only for the statically provable subset in static-checked mode and is handled as a runtime error in checked mode.

When a user-template `op =` executes, its first parameter is passed by borrowing the writable lvalue View, and writes act directly on the assignment target. Source parameters are passed by View value. A standard-template `op =` MAY accept corresponding literals and Views whose template is `none`; its acceptance matrix is defined by Chapter 11 and Appendix 21.

An explicit assignment operator always takes precedence over template default assignment. When ordinary assignment or an explicit assignment operator is used as an expression, the write is completed first, after which the current bytes of the assignment target are read to form an rvalue copy. The length and template of that expression result are fixed by the destination View. The return signature of `op =` constrains only `return` expressions inside the operation body and does not alter the observable return View of the assignment expression.

### 7.3 Explicit Integer Assignment `%d=`

`%d=` writes to the destination using integer semantics. The source is read according to its current integer-interpretation attribute. Memory in a floating-point format is not automatically converted numerically from floating point to integer; use `to_i8()`, `to_i16()`, `to_i32()`, `to_i64()`, or the corresponding `to_uN()` function.

- When the source length is greater than the destination length, the value is reduced modulo `2^(8 * destination length)`, preserving the numerically least-significant bytes. This rule operates on integer-value significance and differs from the low-address byte copy in Section 7.8.
- When the source length is less than the destination length, a signed source is sign-extended and an unsigned source is zero-extended.
- When the source length equals the destination length, the source bytes are copied.

### 7.4 Explicit Floating-Point Assignment `%f=`

`%f=` converts to the destination precision according to IEEE 754 numeric semantics. The destination length MUST be 2, 4, 8, or 16 bytes. A floating-point literal is parsed at the destination precision. A nonliteral source MUST have a length of 2, 4, 8, or 16 bytes and is read in the floating-point format corresponding to its length before conversion.

Integer-to-floating-point numeric conversion uses `to_f16()`, `to_f32()`, `to_f64()`, or `to_f128()`.

### 7.5 Explicit String Assignment `%s=`

`%s=` processes data as a C-style string and guarantees that destination memory contains at least one trailing `0x00` byte. When the destination is longer, the string is copied and the remaining bytes are filled with `0x00`. When the destination is equal in length or shorter, the string is truncated and the final destination byte is forcibly set to `0x00`. A destination length of 0 MUST produce a compile-time diagnostic.

### 7.6 Explicit Boolean Assignment `%b=`

`%b=` reads the source View according to the Boolean-test rules: all bits zero means false, and any other bit pattern means true. It writes `0x00` or `0x01` to the destination. When the destination length is greater than 1, the remaining higher-address bytes are filled with `0x00`.

### 7.7 Address Rebinding `&=`

`IDENT &= expr` changes the binding address of a name. The result of the right-hand expression is interpreted as an address of system-pointer length. When its length differs from `P`, it is adjusted to `P` bytes according to integer-assignment rules. `&=` does not change the declared length, definition address, persistent template, or lifetime.

As an expression, `&=` returns an rvalue View whose content is a byte copy of the left-hand name's content after rebinding, whose length equals the declared length of the left-hand name, and whose template is the persistent template of the left-hand name.

### 7.8 `resize_bytes(expr, length)`

`resize_bytes()` changes only the length of an expression result and does not set an interpretation template. `length` MAY be obtained from a runtime value. When the result is used at a position that requires a static length, such as a declaration with an omitted length, a return signature, or an `op` return signature, `length` MUST be a compile-time constant.

Semantics:

1. The result length is `length`.
2. When the source length equals `length`, copy byte for byte.
3. When the source length is less than `length`, copy the source bytes and fill the remaining higher-address bytes with `0x00`.
4. When the source length is greater than `length`, copy the first `length` bytes at the low-address end of the source.
5. The result template is `none`.
6. No numeric conversion is performed.
7. No byte-order adjustment is performed.

`resize_bytes()` copies bytes in address order. On a big-endian platform, the low-address prefix is not the same as the least-significant bytes of an integer. Numeric extension or truncation SHOULD use integer-assignment semantics, explicit integer assignment `%d=`, or `to_iN()` / `to_uN()`.

When both the byte length and the byte-sequence template must be changed, the operations MUST be composed explicitly:

```hs
new a[4] as bytes
memset(a, 0, length(a))
new b[8] as bytes = resize_bytes(a, 8) as bytes
```

### 7.9 Numeric Conversion and Byte Reinterpretation

- `as` changes only the View template.
- `resize_bytes()` changes only the byte length.
- `byte_swap()` changes only byte order.
- `to_fN()`, `to_iN()`, and `to_uN()` perform numeric conversion.

`reinterpret()`, `to_float()`, and `to_int()` have been removed. Core source files, system headers, and the C compatibility layer MUST NOT accept these names. Use `resize_bytes()`, `to_fN()`, `to_iN()`, or `to_uN()` as appropriate.

### 7.10 Multiple Assignment

Multiple assignment first evaluates the right-hand expressions to form a sequence of source Views, then assigns them in left-hand target order. The number of targets MUST equal the number of sources unless the called function or standard-library function explicitly defines left-context multiple-return semantics.

At a target position on the left-hand side of multiple assignment, `_` is a contextual discard marker that discards the corresponding source View. At every other syntactic position, ordinary identifier rules apply. The left-hand side MAY use `(lvalue %op=)` to specify the assignment interpretation.

---

## 8. Operators, Comparisons, and Evaluation Order

This chapter defines how operators consume Views and produce new Views.

### 8.1 Operator Input Model

The input to an ordinary operator is an operator name and operand Views. Semantic analysis collects the template, length, lvalue/rvalue property, and interpretation attributes of every operand View and then resolves the target operation statically.

```text
input operator + operand Views
-> construct candidate set
-> filter applicable candidates
-> check conflicts
-> form return View
-> generate byte-level observable behavior
```

### 8.2 Ordinary Operator Resolution

Ordinary operator resolution is performed in the following four stages.

**Stage 1: Candidate-set construction**

1. Let the operator be `opname` and the number of operands be `arity`.
2. For every operand carrying a standard template, add candidate operations from the standard template library whose name is `opname` and whose parameter count is `arity`.
3. For every operand carrying a user template, add candidate operations from that template's `impl` whose name is `opname` and whose parameter count is `arity`.
4. Deduplicate the candidate set by definition entity; when the same candidate is discovered through multiple operands, retain it only once.
5. When every operand template is `none`, the candidate set is the built-in untemplated integer rule.
6. Explicit typed operators do not enter this stage.

**Stage 2: Applicability filtering**

For a candidate operation to be applicable, all of the following conditions MUST hold:

1. The operator names match.
2. The parameter counts match.
3. Every formal parameter explicitly carries a template, or the candidate is a normative standard-library meta-candidate.
4. An ordinary `impl op` candidate requires the template of every argument View to equal the template of the corresponding formal parameter.
5. When a formal-parameter template is fixed-length, the argument length equals the template length.
6. When a formal-parameter template is a user template, the argument length equals `sizeof(Template)`.
7. The return signature determines both length and template statically.

The standard-template assignment matrix, standard-library meta-signatures, and normative standard-template candidates are standard-library acceptance rules and are applied before ordinary template-equality filtering. They MAY use meta-parameters such as `view`, `lview`, `mem_view`, `mem_lview`, `cstr_view`, `bytes_view`, `same_len_view`, and `none[len]` to describe accepted sets. These meta-parameters belong only to the standard-library specification and do not enter user `impl op` syntax. When a standard-template candidate has an integer-template formal parameter, it MAY accept an integer literal or a View whose template is `none` and which carries an integer-interpretation attribute. When its formal parameter is a floating-point template, it MAY accept a floating-point literal. Before candidate matching, the corresponding standard assignment semantics of the formal-parameter template are applied for acceptance. Formal parameters using the variable-length standard templates `bytes` and `cstr` accept any positive-length View; the terminating-byte constraint of `cstr` is checked according to Section 11.7 and the standard-library function rules. User-template candidates, user functions, and template methods continue to use exact matching of explicit templates.

**Stage 3: Conflict determination**

When the number of applicable candidates is 0, a compile-time diagnostic MUST be produced. When it is 1, that candidate is selected. When it is greater than 1, an overload-conflict diagnostic MUST be produced.

When operation definitions are merged, the unique key of an `op` is `(operator_name, arity, parameter_template_sequence)`. `impl_template` records only definition ownership and candidate origin. Any two `op` definitions that can enter the same candidate set and have the same operator name, parameter count, and parameter-template sequence MUST produce a compile-time diagnostic during definition merging, even when their return signatures differ. Parameter names, return templates, return lengths, runtime values, object origins, dynamic template information, and return-value context do not participate in overload distinction or conflict resolution.

**Stage 4: Return-View formation**

Execution of the selected ordinary candidate produces an rvalue View. The length and template of the return View come from the candidate's return signature. A comparison operation MUST return template `bool` with length 1. During execution of an `op =` candidate, the write effect is determined by the actual writes through the first parameter. Under Section 7.2, the assignment expression's return View is formed from the current bytes of the assignment target; the return signature constrains only `return` expressions inside the `op =` body.

### 8.3 Explicit Typed Operators

An explicit typed operator always takes precedence over template default operations.

```hs
new a as i32 = 1
new b as i32 = 2
new c as i64 = a %8d+ b
```

`%8d+` uses the explicit 8-byte integer-addition path. The result View has template `none` and is subsequently accepted by the default assignment of the `c as i64` destination.

Integer typed operators include `%d+`, `%Nd+`, `%d-`, `%Nd-`, `%d*`, `%Nd*`, `%d/`, `%Nd/`, `%d%`, `%Nd%`, `%d**`, `%Nd**`, `%d<<`, `%Nd<<`, `%d>>`, `%Nd>>`, `%d&`, `%Nd&`, `%d|`, `%Nd|`, `%d^`, and `%Nd^`. In `%Nd`, `N` denotes the width in bytes. The lexical layer accepts a decimal digit sequence, and the semantic layer requires its value to be greater than 0. When `N` is omitted, `%d` uses `max(operand View lengths)` as the computation width.

Floating-point typed operators include `%f+`, `%Nf+`, `%f-`, `%Nf-`, `%f*`, `%Nf*`, `%f/`, `%Nf/`, `%f**`, and `%Nf**`. In `%Nf`, `N` denotes the width in bytes and MUST be 2, 4, 8, or 16. When `N` is omitted, `%f` uses the greatest floating-point width among the participating nonliteral floating-point Views. When only floating-point literals participate and no other width context exists, it uses 8 bytes. A nonliteral operand whose length is not 2, 4, 8, or 16 MUST produce a compile-time diagnostic. The result template of an explicit typed operator is always `none`.

### 8.4 Untemplated Integer Rules

The untemplated integer rules apply to ordinary arithmetic, comparison, bitwise, and shift operations when the templates are `none` and no explicit floating-point path is used.

- The computation width is `C = max(L1, L2)`.
- The signed range is `[-2^(8C-1), 2^(8C-1)-1]`.
- The unsigned range is `[0, 2^(8C)-1]`.
- A memory View whose template is `none` is interpreted by default as a two's-complement signed integer of its own length. Integer literals use the interpretation attributes carried under Section 3.5.1. When any operand is marked unsigned by `?`, the current integer operation uses unsigned semantics.
- An integer-operation result is reduced modulo `2^(8C)` and stored in a `C`-byte space.

Unary `-` and unary `~` on a View whose template is `none` use built-in integer semantics at the operand View length `L`. Unary `-` stores the two's-complement negation result modulo `2^(8L)`. Unary `~` inverts every bit across the `L` bytes. The result length is `L`, the template is `none`, and the result carries an integer-interpretation attribute consistent with the current unary operation.

Integer division by zero, a negative shift count, and a negative exponent are undefined behavior. In static-checked and checked modes, a compile-time diagnostic MUST be produced when the condition can be proven statically; in checked mode, a runtime error MUST be reported when the condition can be detected.

### 8.5 Standard-Template Default Operations

The standard template library defines default operations for templates including `iN`, `uN`, `fN`, `bool`, `addr`, `cstr`, and `bytes`. Ordinary operators such as `+`, `-`, and `==` resolve to standard-template-library operations when their operands carry standard templates. The standard template library also defines the built-in semantics of unary `-` and `~` for standard templates. Logical `!`, `&&`, and `||` always use the Boolean-test path in Section 8.8. These unary operations are not open to user-template overloading.

The standard library MAY declare mixed standard-template operations, but every such combination MUST be listed in the standard template library with a normative signature. The portable core guarantees only same-template binary operations. Cross-width, cross-signedness, and mixed integer/floating-point operations are implementation-listed standard-library extensions. An unlisted combination MUST produce a compile-time diagnostic.

### 8.6 User-Template `op`

A user template MAY define default operations in an `impl`. Matching follows Section 8.2. A mixed operation between user templates, or between a user template and a standard template, MUST be defined explicitly. This Standard performs no implicit cross-template conversion.

A user `op` MUST NOT have the return signature `-> ()`. `op format` uses the dedicated ABI defined in Section 10.6 and MUST provide a static return View.

### 8.7 Comparisons and `bool` Results

Comparison operations are `==`, `!=`, `<`, `<=`, `>`, and `>=`. A comparison provided by a standard template or user `op` MUST return template `bool` with length 1.

An untemplated comparison follows the untemplated integer rules and produces a one-byte rvalue View whose template is `bool`.

### 8.8 Logical Operations and Short-Circuiting

The Boolean-test rule is: all bits zero means false; any other bit pattern means true. The results of `!`, `&&`, and `||` are one-byte `bool` Views.

Logical operations use the built-in Boolean-test path. They do not enter the ordinary `op` candidate set in Section 8.2 and do not invoke a user-template `op`. Any View carrying a standard template, user template, or `none` is read according to the Boolean-test rule in this section. `&&` evaluates from left to right and does not evaluate its right operand when the left operand is false. `||` evaluates from left to right and does not evaluate its right operand when the left operand is true.

### 8.9 Unsigned-Interpretation Suffix `?`

The postfix `?` operator interprets the bytes of the View on its left as an unsigned integer and produces an rvalue View. The result has the same length as the left operand, template `none`, and an unsigned-integer interpretation attribute. It affects only the expression to which it is applied.

```hs
new x[4] = -1
new y[4] = x? + 1
```

### 8.10 Evaluation Order and Sequence Points

Operator precedence and associativity determine expression grouping. In the absence of a sequence-point constraint, the implementation chooses the evaluation order of peer subexpressions, and a program MUST NOT depend on that order.

Sequence points occur:

- After evaluation of the left operand of `&&`.
- After evaluation of the left operand of `||`.
- After evaluation of the first operand of ternary `? :`.
- After evaluation of all function-call arguments and before execution of the function body.
- Between the initialization, condition, and iteration components of a `for` loop header.
- Between comma-separated `expr_stmt` entries in `for_post`.
- At the end of a full expression.

When two unsequenced side effects write the same overlapping memory region, or when one unsequenced side effect writes a region while another unsequenced evaluation reads that region, the behavior is undefined. In static-checked and checked modes, a compile-time diagnostic MUST be produced when the condition can be proven statically; in checked mode, an error MUST be reported when the condition can be detected.

---

## 9. User-Defined Templates, Member Layout, and Template Instances

### 9.1 Template Definitions

`template` defines a user template. It describes the member layout of a contiguous memory region and the default interpretation template of each member.

```hs
template Point {
    x[8] as f64
    y[8] as f64
}
```

Rules:

- Members are packed in declaration order with no implicit alignment.
- A member length MUST be a positive integer literal, `[P]`, or a statically evaluable `sizeof(TemplateName)`.
- A member MUST NOT use `[tN]`.
- A member template does not change the member size and introduces no additional alignment.
- A user template MAY be defined only at global scope.
- Standard templates, user templates, functions, external symbols, and global storage objects share the global namespace; a duplicate name MUST produce a compile-time diagnostic.
- A user-template name MUST NOT redefine a standard-template-library name.

### 9.2 `sizeof(Template)`

`sizeof(Name)` is a compile-time expression. For a fixed-length standard template or user template, it returns the template's length in bytes. `sizeof(addr)` returns `P`. Using `sizeof` on the variable-length standard templates `bytes` or `cstr` MUST produce a compile-time diagnostic.

The size of a user template is the sum of the lengths of all its members.

### 9.3 Instance Declarations

```hs
new p as Point
new q[sizeof(Point)] as Point
new arr[t10] as Point
```

- `new p as Point` allocates `sizeof(Point)` bytes.
- An explicit length MUST equal the template size.
- `[tN]` allocates `N * sizeof(Template)` bytes, where `N` MUST be a positive integer literal.

### 9.4 Member Access

Member access is fundamentally a compile-time offset calculation.

- `obj.member` accesses a member using the object's current user template.
- `(obj as Template).member` accesses a member through a temporary template View.
- `arr[tK]` accesses template instance `K`, where `K` is zero-based and MUST be less than the number of instances declared. The result is an lvalue View whose length is `sizeof(Template)` and whose template is the user template declared for the array.
- `arr[tK].member` accesses a member of template instance `K`.
- Member access produces an lvalue View whose length comes from the member declaration. Its template comes from the corresponding member-chain `set` override or the member-declaration template; an unannotated member has template `none`.
- `&obj.member` returns the member's start address with template `addr`.

```hs
template User {
    id[4] as u32
    age[1] as u8
    name[32] as cstr
}

new u as User
u.id = 1001
u.name = "Kai"
```

### 9.5 Templates and Operations

A user template defines layout. Default behavior on a user template is defined by `impl`. When no `op =` is defined, default assignment between identical user templates performs a byte-for-byte copy. Every other ordinary operation MUST have a matching `op`.

---

## 10. `impl`, Default Operations, and Template Methods

### 10.1 `impl` Definitions

`impl TemplateName { ... }` binds default operations and methods to a template. In an ordinary user translation unit, `TemplateName` MUST resolve to a user template. A standard-template-library unit MAY provide normative `impl` definitions for standard templates. A user program that redefines or extends an `impl` for a standard template MUST produce a compile-time diagnostic unless the implementation documents an extension mode. The same template MAY appear in multiple `impl` blocks. After top-level name collection, the implementation MUST merge those blocks and check duplicate operation keys, duplicate method keys, and candidate conflicts according to Sections 10.3, 10.7, and 10.10.

```hs
impl Vec2 {
    op + (lhs as Vec2, rhs as Vec2) -> Vec2 {
        new out as Vec2
        out.x = lhs.x + rhs.x
        out.y = lhs.y + rhs.y
        return out
    }

    func length(self as Vec2) -> f64 {
        return f_sqrt(self.x * self.x + self.y * self.y)
    }
}
```

### 10.2 Overloadable Operations

A user template MAY define the following operations:

```text
= == != < <= > >= + - * / % ** << >> & | ^ format
```

All of these operations are resolved through static template matching. Except for `format`, every user-template-overloadable operation is binary and MUST have exactly two formal parameters. The two parameters of `op =` are the assignment target and source View, respectively. `op format` has the fixed form `op format (value as Template, out[P] as addr) -> i32`. `format` is a contextual operation name within an `op` declaration and is parsed as an ordinary identifier elsewhere. A user template MUST NOT overload unary `!`, unary `~`, unary `-`, or `++`/`--`; the standard template library MAY still define normative built-in semantics for those operations. Every operation parameter MUST explicitly carry a template. The return signature MUST determine both length and template statically.

### 10.3 Candidate Signatures and Duplicate Definitions

The overload key of an `op` consists of the following three components:

```text
(operator_name, arity, parameter_template_sequence)
```

`impl_template` denotes only the candidate origin. Any two `op` definitions that can enter the same candidate set and have the same operator name, parameter count, and parameter-template sequence MUST produce a compile-time diagnostic during definition merging, even when their return signatures differ. Parameter names do not distinguish overloads. Identical parameter-template sequences cannot be overloaded solely by return template or return length.

### 10.4 Default Assignment Operation `op =`

The first parameter of `op =` represents the assignment target and MUST be passed by borrowing a writable lvalue View. The operation body MAY write target members. The return signature is used to validate `return` expressions in the operation body; it MUST be statically determinable and MUST match the template and length of the first parameter. The observable result of the assignment expression is fixed as an rvalue copy formed from the current bytes of the assignment target.

```hs
impl Vec2 {
    op = (dst as Vec2, src as Vec2) -> Vec2 {
        dst.x = src.x
        dst.y = src.y
        return dst
    }
}
```

Resolution of `op =` requires:

1. The first argument MUST be the writable lvalue View formed from the left-hand target of the assignment statement.
2. The template of the first formal parameter MUST equal the template of the destination View.
3. Remaining formal parameters are passed according to ordinary View-value copy rules.
4. The return signature MUST be statically determinable and MUST match the template and length of the first parameter.
5. A `return` expression in the operation body MUST satisfy the return signature. Under Section 7.2, the assignment expression returns a copy of the target, and that return value does not retain the lvalue property of the assignment target.
6. A user template without an `op =` uses the fallback rule of byte-for-byte copy between identical templates.

### 10.5 Arithmetic, Bitwise, and Comparison Operations

The return template of an arithmetic, bitwise, or shift operation MUST be declared explicitly. The return value MAY use the same template or a different template. A comparison operation MUST return template `bool` with length 1.

A mixed-template operation MUST be defined explicitly:

```hs
impl Vec2 {
    op * (lhs as Vec2, k as f64) -> Vec2 {
        new out as Vec2
        out.x = lhs.x * k
        out.y = lhs.y * k
        return out
    }
}
```

### 10.6 Formatting Operation

`op format` is used for standard output, string formatting, and diagnostic display. Its standard form is:

```hs
op format (value as Template, out[P] as addr) -> i32
```

`out` points to a formatting output target supplied by the implementation. The return value is the number of bytes written; a negative value denotes an implementation-defined error code. In standard mode, a user `op format` MUST have exactly these two parameters, its second parameter MUST be `[P] as addr`, and its return template MUST be `i32`. A more complex formatting ABI is a standard-library extension and MUST be documented.

### 10.7 Method Calls

A template method is a function in an `impl` namespace. A method call has the form:

```hs
expr.method(arg0, arg1)
```

Resolution rules:

1. Evaluate the View of `expr`.
2. Read the template carried by that View.
3. Find a `func` with the same name in that template's `impl`.
4. Pass `expr` as the first argument.
5. Pass the remaining arguments in written order.
6. Copy parameter Views according to the function-call rules.
7. A resolution failure MUST produce a compile-time diagnostic.

The first parameter of a method MUST be declared explicitly, and its template MUST equal the template of the enclosing `impl TemplateName`. A zero-parameter method MUST produce a compile-time diagnostic. A method has no additional hidden privileges; field access continues to follow ordinary member-access rules.

The method-overload key is:

```text
(method_name, arity, parameter_template_sequence)
```

`arity` includes the first parameter. Parameter names, return templates, return lengths, and runtime values do not participate in method-overload distinction. Definitions with an identical method-overload key MUST produce a compile-time diagnostic during definition merging. At a method call, an applicable-candidate count of 0 or greater than 1 MUST produce a compile-time diagnostic.

### 10.8 `mut self` and Explicit Write Mechanisms

`mut` is a reserved keyword. In standard mode, encountering a `mut` method parameter MUST produce a compile-time diagnostic. When an implementation extension mode accepts that syntax, the implementation MUST document how the mode is enabled and its complete semantics.

A template method receives `self` by View-value copy. To write an external object, a program MUST use one of the following explicit mechanisms:

1. Use an assignment statement to trigger `op =`, allowing the first parameter of `op =` to write the target.
2. Pass an `addr` and write through explicit dereference using `[N]*addr`, `[P]*addr`, or `[sizeof(Template)]*addr`.
3. Return a new View and have the caller assign it to the target.

Example:

```hs
new v as Vec2
v = v.normalized()
```

### 10.9 Methods on Temporary Template Views

Expression-level `as` MAY be used for a method call:

```hs
new raw[16] as bytes
new len as f64 = (raw as Vec2).length()
```

A fixed-length template requires the length of `raw` to equal the length of `Vec2`.

### 10.10 Operation-Resolution Diagnostics

The following conditions MUST produce a diagnostic:

- An `impl` refers to a nonexistent template.
- An `op` parameter has no template.
- The length of an `op` return signature cannot be determined statically.
- An `op` return signature is `-> ()`.
- The number of parameters of a user `op` does not match the standard arity of that operator.
- `op format` does not use the standard-form signature.
- The return signature of `op =` does not match the template or length of its first parameter.
- `op` definitions have identical overload keys and can enter the same candidate set.
- An ordinary operator has multiple applicable candidates.
- An ordinary operator has no applicable candidate.
- A method declaration lacks an explicit first parameter, or the first-parameter template does not equal the enclosing `impl` template.
- Definitions have an identical method-overload key.
- The View on the left of a method call has no template.
- The template on the left of a method call contains no target method, or multiple method candidates are applicable.
- A user-template operation attempts to resolve its target operation using runtime type information.
- An `op` uses an implicit cross-template conversion.
- The first argument of `op =` is not the writable lvalue View of the assignment target.

---

## 11. Standard Template Library

The standard template library is the set of templates that a conforming implementation MUST provide. Standard templates and user templates share the same template mechanism. An implementation MAY provide standard templates through a source library, compiler built-ins, or a combination of both.

### 11.1 Minimum Template Set

| Template | Length | Default interpretation |
|---|---:|---|
| `i8` | 1 | 8-bit signed integer |
| `i16` | 2 | 16-bit signed integer |
| `i32` | 4 | 32-bit signed integer |
| `i64` | 8 | 64-bit signed integer |
| `u8` | 1 | 8-bit unsigned integer |
| `u16` | 2 | 16-bit unsigned integer |
| `u32` | 4 | 32-bit unsigned integer |
| `u64` | 8 | 64-bit unsigned integer |
| `f16` | 2 | IEEE 754 binary16 |
| `f32` | 4 | IEEE 754 binary32 |
| `f64` | 8 | IEEE 754 binary64 |
| `f128` | 16 | IEEE 754 binary128 or an equivalent software implementation |
| `bool` | 1 | Boolean value |
| `addr` | `P` | Platform address value |
| `handle` | `P` | Opaque file-handle value |
| `cstr` | Any positive length | `0x00`-terminated byte string |
| `bytes` | Any positive length | Raw byte sequence |

The digits in the names `i8/i16/i32/i64` and `u8/u16/u32/u64` denote bit width; their storage lengths are 1, 2, 4, and 8 bytes, respectively. In the normative text, `iN` and `uN` denote templates from the corresponding family, where `N` is the bit width.

### 11.2 Semantics Required for Every Standard Template

Every standard template MUST define:

1. Its name.
2. Its length or variable-length rule.
3. Default-assignment semantics.
4. Default-operation semantics.
5. Comparison semantics.
6. Formatting semantics.
7. Diagnostic rules.
8. Length-consistency rules for expression-level `as`.

### 11.3 Integer Templates

`iN` uses N-bit two's-complement signed-integer semantics. `uN` uses N-bit unsigned-integer semantics. Integer templates support default assignment, arithmetic, comparison, bitwise operations, shifts, unary `-`, unary `~`, and formatting.

Integer-operation results are reduced modulo the template width. Unary `-` returns the two's-complement negation result at the same template width. Unary `~` returns the bitwise inversion result at the same template width. Signed integer division truncates toward zero, and the sign of the remainder matches the dividend. Signed right shift is arithmetic. In unchecked mode, integer division by zero, a negative shift count, and a negative exponent are undefined behavior. In static-checked and checked modes, a compile-time diagnostic MUST be produced when the condition can be proven statically; in checked mode, a runtime error MUST be reported when the condition can be detected at runtime.

The result template of a mixed integer operation within the integer families is determined by the standard-library signature. The minimum requirements are:

1. A same-template binary integer operation returns the same template.
2. A same-template comparison returns `bool`.
3. A cross-width integer operation MUST be listed explicitly by the standard library; an unlisted combination MUST produce a diagnostic.
4. A mixed signed/unsigned operation MUST be listed explicitly by the standard library; an unlisted combination MUST produce a diagnostic.

A portable program SHOULD depend only on the same-template integer operations listed in this section and on mixed operations that the implementation explicitly includes in its standard-library signature table.

### 11.4 Floating-Point Templates

`f16`, `f32`, `f64`, and `f128` follow IEEE 754 semantics and use the system's native byte order. The default rounding mode is `roundTiesToEven`. Preservation and propagation of NaN payloads are implementation-defined.

Floating-point templates support default assignment, arithmetic, comparison, unary `-`, and formatting. Unary `-` returns the IEEE 754 sign-negated result at the same template width. Floating-point division by zero, overflow, underflow, subnormals, `Inf`, and `NaN` are handled according to IEEE 754.

Same-template floating-point arithmetic returns the same template. Same-template comparison returns `bool`. Cross-width floating-point assignment and operations MUST be listed explicitly by the standard library; an unlisted combination MUST produce a compile-time diagnostic.

### 11.5 `bool`

`bool` has a length of 1 byte. Default assignment normalizes any source View to `0x00` or `0x01`. Logical operations execute through the built-in Boolean-test path in Section 8.8 and produce template `bool`.

`bool` supports `= == != format`. Arithmetic, bitwise, and shift operations MUST produce a compile-time diagnostic.

### 11.6 `addr` and `handle`

`addr` has length `P`. Default assignment MAY accept `addr`, an integer literal, a `none` View carrying an integer-interpretation attribute, `iN`, `uN`, or `bool`, and either copies the address value or adjusts it to `P` bytes according to the integer rules. `addr` supports equality comparison and formatting. Address arithmetic is not performed implicitly through ordinary `addr + integer`; a program SHOULD use `?` to obtain an unsigned-integer View and then assign the result to an `addr` destination.

`handle` has length `P` and represents an opaque file-handle value. An all-zero `P`-byte `handle` is a null handle. `handle` supports same-template default assignment, equality comparison, and formatting. Ordinary arithmetic, bitwise operations, shifts, dereference, `free()`, and construction of a handle from an ordinary integer MUST produce a diagnostic or runtime error. File-I/O functions accept and return file handles through the `handle` template.

### 11.7 `cstr` and `bytes`

`cstr` is a byte string terminated by `0x00`. Default assignment uses `%s=` semantics. Comparison and formatting use C-style string semantics. When no terminating `0x00` exists within the visible extent of a `cstr` View, a compile-time diagnostic MUST be produced in static-checked and checked modes when the condition can be proven statically; in checked mode, a runtime error MUST be reported when the condition can be detected at runtime; in unchecked mode, the behavior is undefined.

`bytes` is a raw byte sequence. Default assignment copies byte for byte, and the source View length MUST equal the destination length. Comparison is lexicographic: compare the first differing byte in the common prefix; when the common prefix is identical, the shorter length compares as less; when the lengths are equal and every byte is identical, the values compare as equal. Formatting uses an implementation-defined byte-display format by default.

### 11.8 Compiler Built-In Optimization

An implementation MAY recognize standard templates as compiler-built-in fast paths.

```hs
new a as i32 = 1
new b as i32 = 2
new c as i32 = a + b
```

From the perspective of standard semantics, the implementation resolves `op =` and `op +` for `i32`. From the implementation perspective, it MAY directly generate 32-bit signed-integer addition IR.

### 11.9 Observable-Behavior Equivalence Requirement

Regardless of whether standard templates are implemented through a source library, compiler built-ins, or a combination of both, observable behavior MUST remain consistent with the definitions of the standard template library. The required equivalence includes compile-time diagnostics, expression-result lengths, result templates, written byte sequences, runtime-error behavior, formatted output, overflow and truncation, floating-point rounding, and checks in static-checked and checked modes.

### 11.10 Standard-Template Default-Assignment Matrix

| Destination template | Accepted source View | Write semantics |
|---|---|---|
| `iN` | Integer literal, integer View with template `none`, `iM`, `uM`, `bool` | Write the integer value as N-bit two's complement; width changes follow Section 7.3 |
| `uN` | Integer literal, integer View with template `none`, `iM`, `uM`, `bool` | Write the value as an N-bit unsigned integer; width changes follow Section 7.3 |
| `fN` | Floating-point literal, `fM` | Convert numerically to N-bit floating point according to IEEE 754; integer-to-floating conversion requires `to_f16()`, `to_f32()`, `to_f64()`, or `to_f128()` |
| `bool` | Any View | All zero is `false`; any other bit pattern is `true`; write `0x00` or `0x01` |
| `addr` | `addr`, integer literal, `none` View carrying an integer-interpretation attribute, `iN`, `uN`, `bool` | Copy the address value or adjust it to `P` bytes according to the integer rules |
| `none` | Integer literal, `none` View carrying an integer-interpretation attribute, `iN`, `uN`, `bool`, `addr` | Write using raw-integer assignment semantics at the destination length; width changes follow Section 7.3; every other source MUST be given an explicit conversion or interpretation path |
| `handle` | `handle` | Copy the opaque handle value |
| `cstr` | String literal, `cstr` | Copy or truncate according to `%s=` and guarantee a terminating byte |
| `bytes` | Any equal-length View | Copy byte for byte; a differing length MUST be diagnosed or handled through explicit `resize_bytes()` |
| User template | View of the same template | Execute `op =` when defined; otherwise copy byte for byte |

The standard-template assignment matrix is a normative set of acceptance rules and takes precedence over the ordinary `impl op` formal-parameter template-equality filter in Section 8.2. When ordinary operations, comparisons, and standard-function parameters for standard integer and floating-point templates accept literals, the literal or numeric `none` View is interpreted according to the corresponding acceptance semantics in this matrix. “Any View” for `bool` and “any equal-length View” for `bytes` are standard-library meta-acceptance sets and MAY be represented internally using `view`, `same_len_view`, or an equivalent implementation form. The pseudocode in Appendix 21 shows only representative same-template primary paths. A complete implementation MUST expand every accepted combination in this matrix while preserving identical observable behavior.

`as` does not trigger numeric conversion. Cross-category numeric conversion MUST use `to_fN()`, `to_iN()`, or `to_uN()`. Pure byte-length adjustment uses `resize_bytes()`. An explicit typed assignment operation is used when a specific assignment-interpretation path is required.

---

## 12. Functions, Return Values, and Calling Conventions

### 12.1 Function Definitions

```hs
func name(param0 as i32, param1[P] as addr) -> result as i32 {
    result = param0
    return result
}
```

Function parameters are local storage objects. Parameters of ordinary functions and template methods are passed by View-value copy. A parameter template affects only default interpretation and diagnostics within the function body. An implementation MAY optimize the copy mechanism, but observable behavior MUST remain equivalent.

By-value parameters of ordinary functions, template methods, and core `extern` declarations MUST have statically determinable lengths. When the length is omitted, it can be inferred only from a fixed-length standard template or a user template. A parameter using `bytes`, `cstr`, `none`, or an omitted template MUST state an explicit static length, for example `buf[256] as bytes`. The `view`, `lview`, `mem_view`, `mem_lview`, `cstr_view`, and `bytes_view` forms used in standard-library signatures are meta-notation from Section 14.0 and are not ordinary function-parameter syntax.

The first parameter of `op =` is the dedicated entry point for assignment semantics and is passed by borrowing a writable lvalue View according to Section 10.4. This rule does not extend to ordinary function parameters.

### 12.2 Return Values

A return signature MAY specify no return value, one return value, or multiple return values. A return item MAY be named or anonymous, MAY use length and template annotations, or MAY use template shorthand.

```hs
func f() -> i32 { ... }
func g() -> (ok as bool, value as f64) { ... }
func h() -> () { ... }
```

Omitting the return signature is equivalent to `-> ()`. A named return item is created at function entry as a local return storage object, and its name is visible in the function-body scope. It remains uninitialized until explicitly written. `return` expressions are assigned to return Views in return-item order. A `return` statement with no expression returns the current contents of named return storage when named return items exist; in a function with anonymous return items, an expressionless `return` is permitted only for `-> ()`. Return items of ordinary functions, template methods, and user `op` definitions MUST have statically determinable lengths. When `none`, `bytes`, or `cstr` is used as a return template, an explicit length MUST be stated. A return-value-count mismatch MUST produce a compile-time diagnostic. An extension involving a condition that cannot be decided statically is handled according to the execution mode. Dynamic-length results of standard-library built-ins are used only through the meta-signatures in Chapter 14.

### 12.3 Function Calls

A function call first evaluates every argument to form a sequence of argument Views, then enters the function body. The implementation chooses and documents the evaluation order among arguments. Arguments do not establish sequence points with one another. A sequence point is established after all arguments have been evaluated and before execution of the function body. Argument count, length, and template constraints MUST satisfy the function signature.

### 12.4 `main`

An implementation MUST document its `main` function convention. A portable program SHOULD use the following form:

```hs
func main() -> i32 { ... }
```

The return value is treated as the process exit code by the host environment. A `main` with no return value is treated as returning 0 unless the implementation documents other behavior.

---

## 13. Control Flow and Error Handling

### 13.1 Conditional Statements

An `if` condition is evaluated according to the Boolean-test rules. `else if` branches are tested in written order.

```hs
if (condition) {
    ...
} else {
    ...
}
```

### 13.2 Loop Statements

`for` syntax:

```hs
for (init; condition; post) {
    ...
}
```

Sequence points occur between `init`, `condition`, and `post`. When `continue` executes inside a `for` body, local objects in scopes being exited are processed according to the scope rules, then `post` executes, and the next evaluation of `condition` begins. A `while` loop evaluates its condition before every iteration. When `continue` executes inside a `while` body, control proceeds directly to the next condition evaluation.

### 13.3 Jump Statements

`break` terminates the nearest enclosing loop. `continue` begins the next iteration of the nearest enclosing loop. `goto label` jumps to a label within the same function. When a jump leaves a scope, local `new` objects are released according to the scope rules.

A `break` or `continue` outside a loop MUST produce a compile-time diagnostic. The target label of a `goto` MUST be in the same function. An undefined label, duplicate label, or cross-function jump MUST produce a compile-time diagnostic. A `goto` that enters the scope of a local storage object that has not yet been initialized MUST produce a compile-time diagnostic, or, when the condition cannot be decided statically, be handled according to the execution mode.

### 13.4 Increment and Decrement

`lvalue++` and `lvalue--` are statements. An lvalue carrying an integer standard template uses the corresponding template's integer semantics. An lvalue whose template is `none` performs integer increment or decrement at its current length. Using `++` or `--` on an lvalue with template `fN`, `bool`, `addr`, `cstr`, `bytes`, or a user template MUST produce a compile-time diagnostic.

### 13.5 Error Handling

`try-catch-throw` provides explicit error handling. An error value is fundamentally memory data.

```hs
try {
    ...
} catch (err as i32) {
    ...
}
```

`throw expression` assigns the expression result to the dynamically nearest `catch` parameter. A `catch` parameter is a local storage object and MAY carry a template. Its length is determined statically according to ordinary local-declaration rules: a fixed-length standard template or user template MAY infer the length; `bytes`, `cstr`, `none`, or an omitted template requires an explicit static length. Failure to determine the length statically MUST produce a compile-time diagnostic. Assignment failure is handled through ordinary assignment diagnostics or runtime errors. Each `try` can have only one `catch`; nested `try` statements match according to dynamic nearest-enclosing order. During `throw` propagation, local `new` objects in scopes being exited are released according to Section 5.5. When no matching `catch` exists, the program terminates with an implementation-defined error status and performs runtime resource release according to implementation documentation. Runtime undefined behavior in unchecked and static-checked modes is not automatically converted into `throw`. In checked mode, a detected runtime error MAY be converted into `throw` according to implementation documentation.

---

## 14. Standard-Library Functions

Standard-library functions define their parameters and return values using the View model. An implementation MAY provide built-in implementations, but MUST preserve observable behavior. A standard function is not a language primitive: its owning system header MUST be explicitly included with `$include` before each use. A missing header, inclusion of the wrong header, a handwritten `extern` declaration for a standard function, or a declaration that conflicts with the official declaration MUST produce a compile-time diagnostic. Declarations in headers form the user interface. Built-in identity and optimization are implementation details and MUST NOT change the observable semantics in this chapter.

| System header | Facilities provided |
|---|---|
| `stdlib.hsh` | Allocation, length and byte conversion, numeric conversion, integer mathematics, random numbers, process control |
| `string.hsh` | Memory copy and comparison, C strings |
| `stdio.hsh` | Standard input/output, formatted I/O, file I/O |
| `math.hsh` | Mathematical functions for `f16`, `f32`, `f64`, and `f128` |
| `ctype.hsh` | ASCII character classification and case conversion |
| `time.hsh` | `time_ms()`, `clock_ms()` |
| `assert.hsh` | `assert()`, `panic()` |

### 14.0 Signature Notation

This chapter uses the following meta-notation to describe standard-library signatures. `view`, `lview`, `mem_view`, `mem_lview`, `cstr_view`, `bytes_view`, `T`, `...`, `left-context`, and `none[len]` are not template names in core syntax. `addr`, `handle`, `iN`, `uN`, `fN`, `u64`, `i32`, and `bool` denote the corresponding standard templates or signature patterns.

| Notation | Meaning |
|---|---|
| `view` | Any lvalue or rvalue View |
| `lview` | Writable lvalue View |
| `mem_view` | Memory operand; when `len > 0`, equivalent to a readable View; when `len == 0`, MAY be formed from an evaluable address without dereference |
| `mem_lview` | Writable memory operand; when `len > 0`, equivalent to a writable lvalue View; when `len == 0`, MAY be formed from an evaluable address without dereference |
| `same_len_view` | Standard-library meta-parameter for which the source View length MUST equal the corresponding destination View length |
| `cstr_view` | A View carrying template `cstr` and containing a terminating `0x00` |
| `bytes_view` | Any byte-sequence View; the name denotes byte-reading capability and does not require the `bytes` template |
| `addr` | A View of length `P` and template `addr` |
| `handle` | A file-handle View of length `P` and template `handle` |
| `iN` / `uN` | One concrete template among `i8/i16/i32/i64` or `u8/u16/u32/u64`, where `N` denotes bit width |
| `fN` | One concrete template among `f16/f32/f64/f128`, where `N` denotes bit width |
| `T` | Repeated occurrences within one signature denote the same expanded concrete standard template |
| `u64` | 8-byte unsigned-integer template |
| `i32` | 4-byte signed-integer template |
| `bool` | 1-byte Boolean template |
| `...` | Vararg sequence; argument count and interpretation are determined by the format string or implementation ABI |
| `left-context` | Built-in result form that MAY be used only as the entire right-hand side of a multiple-assignment statement; see Section 14.4 |
| `none[len]` | Dynamic- or static-length `none` rvalue View in a standard-library meta-signature, where `len` is supplied by a parameter, source-View length, or fixed constant in the signature |

A standard-library signature containing `iN`, `uN`, `fN`, or `T` is a signature pattern. An implementation MUST expand a signature pattern into concrete combinations of standard templates. For example, `f_sqrt(x as fN) -> fN` expands into same-template overloads for `f16/f32/f64/f128`, and `min(a as T, b as T) -> T` expands into overloads in which both operands use the same integer or floating-point template. The return template is determined by the expanded concrete signature and MUST NOT be inferred backward from return-value context.

`none[len]` is a standard-library-specific dynamic-length result. When such a result enters a position that requires a static length, including a declaration with omitted length, a return signature, or an `op` return signature, its length source MUST be statically evaluable. Every `len`, `size`, and `count` parameter is interpreted as `u64`. A function involving `size * count` handles multiplication overflow according to the execution-mode rules. Ordinary standard-library functions are parsed only as function calls and do not automatically participate in template-method calls.

Where a table in this chapter states that a standard-library safety condition “MUST report an error in checked mode,” static-checked mode applies only the statically provable subset and produces a compile-time diagnostic.

In standard-library signatures, `lview` and `mem_lview` denote borrowing of a caller-writable View; standard-library writes act directly on the storage region located by the original View. This borrowing rule applies only to the standard-library meta-signatures in this chapter and to the target parameter of `op =`. Parameters of ordinary user functions and template methods continue to be passed by View-value copy according to Section 12.1. When `len == 0`, `mem_view` and `mem_lview` MAY be formed from an address value as zero-length memory operands; their formation MUST NOT perform any dereference.

For example, `memcpy(dst_addr as addr, src_addr as addr, 0)` is valid, and the function neither reads nor writes the content pointed to by either address. When runtime `len > 0` in the same call, `dst` MUST be a writable memory View and `src` MUST be a readable memory View, with capacity checked according to this chapter. A raw `addr` argument used in a positive-length memory operation MUST be associable with boundary metadata for a dynamic object, static object, global object, slice, or external object. When capacity cannot be proven or detected, unchecked-mode behavior is undefined, static-checked mode applies its statically provable subset, and checked mode MUST report a runtime error.

### 14.1 Memory Management and Byte Operations

| Function | Signature | Boundary and failure rules |
|---|---|---|
| `length` | `length(x as view) -> u64` | Return the View length of `x`. |
| `alloc` | `alloc(size as u64) -> addr` | Allocate a dynamic object of `size` bytes. The return value for `size == 0` is implementation-defined. On allocation failure, return the null address or throw according to implementation documentation. |
| `calloc` | `calloc(count as u64, size as u64) -> addr` | Allocate `count * size` bytes and fill them with `0x00`. In checked mode, multiplication overflow MUST be reported as an error. |
| `realloc` | `realloc(ptr as addr, size as u64) -> addr` | A null `ptr` is equivalent to `alloc(size)`; a non-null `ptr` MUST be the base address of a currently valid dynamic object. When `size == 0`, release the object and return the null address or an implementation-defined address. On failure, the original object remains valid. In checked mode, an error MUST be reported when an interior address, slice address, offset address, non-dynamic address, already-freed base address, or dangling address to a freed object is supplied and the condition is detectable. |
| `free` | `free(ptr as addr) -> ()` | A null address has no effect. A non-null address MUST be the base address of a currently valid dynamic object. In checked mode, an error MUST be reported when an interior address, slice address, offset address, non-dynamic address, already-freed base address, or dangling address to a freed object is supplied and the condition is detectable. |
| `memset` | `memset(dst as mem_lview, value as view, len as u64) -> addr` | Write `len` bytes; use the lowest 1 byte of `value`. When `len > length(dst)`, checked mode MUST report an error. Return the destination start address. |
| `memcpy` | `memcpy(dst as mem_lview, src as mem_view, len as u64) -> addr` | Copy `len` bytes. When `len > length(dst)` or `len > length(src)`, checked mode MUST report an error. Overlap between source and destination is undefined behavior; checked mode MUST report an error when overlap can be detected. Return the destination start address. |
| `memmove` | `memmove(dst as mem_lview, src as mem_view, len as u64) -> addr` | Copy `len` bytes with overlap supported. When `len > length(dst)` or `len > length(src)`, checked mode MUST report an error. Return the destination start address. |
| `memcmp` | `memcmp(a as mem_view, b as mem_view, len as u64) -> i32` | Compare the first `len` bytes. When `len > length(a)` or `len > length(b)`, checked mode MUST report an error. Return a negative value, 0, or a positive value; the absolute magnitude of a nonzero result is implementation-defined. |

A `len` value of 0 is valid for `memset`, `memcpy`, `memmove`, and `memcmp`. In that case, source and destination content is not accessed, and the addresses need not be dereferenceable. Parameter expressions MUST still be evaluable as addresses or Views. An implementation MUST NOT dereference a null address merely to form a zero-length memory operand.

Example: `memcpy(dst_addr as addr, src_addr as addr, 0)` is valid. `dst_addr` and `src_addr` need only be evaluable as address Views. When runtime `len > 0`, the source argument MUST form a readable View and the destination argument MUST form a writable View. A raw `addr` argument satisfies the positive-length `mem_view`/`mem_lview` requirement only when the implementation can obtain the corresponding object boundaries, after which boundary checks are performed according to this section.

### 14.2 Conversion Functions

| Function | Signature | Rules |
|---|---|---|
| `to_f16` / `to_f32` / `to_f64` / `to_f128` | `(data as view) -> f16/f32/f64/f128` | Convert an integer or floating-point View numerically to the corresponding floating-point template. |
| `to_i8` / `to_i16` / `to_i32` / `to_i64` | `(data as view) -> i8/i16/i32/i64` | Convert an integer or floating-point View numerically to the corresponding signed-integer template; out-of-range handling follows implementation documentation or checked-mode error rules. |
| `to_u8` / `to_u16` / `to_u32` / `to_u64` | `(data as view) -> u8/u16/u32/u64` | Convert an integer or floating-point View numerically to the corresponding unsigned-integer template; out-of-range handling follows implementation documentation or checked-mode error rules. |
| `resize_bytes` | `resize_bytes(expr as view, length as u64) -> none[length]` | Adjust the byte length according to Section 7.8. The result template is `none`, and the result length equals `length`. At a position requiring a static length, `length` MUST be statically evaluable. |
| `byte_swap` | `byte_swap(expr as view) -> none[length(expr)]` | Return an rvalue View with reversed byte order, the same length as the source, and template `none`. |

Conversion-function read rules are as follows: a View with template `iN`, `uN`, or `none` carrying an integer-interpretation attribute is read as an integer value; a View with template `fN` is read as an IEEE 754 value; a View with template `bool` is read as Boolean value 0 or 1; a View with template `addr` MAY be converted to an unsigned integer. A `bytes`, `cstr`, user-template, or uninterpreted `none` View used as conversion input MUST first be given an interpretation through `as`, `?`, or an explicit standard function.

### 14.3 String and Buffer Functions

| Function | Signature | Boundary and failure rules |
|---|---|---|
| `strlen` | `strlen(s as cstr_view) -> u64` | Return the number of bytes before the terminating `0x00`. When no terminating byte exists within the View extent, checked mode MUST report an error. |
| `strcmp` | `strcmp(a as cstr_view, b as cstr_view) -> i32` | Compare according to C-string byte order. |
| `strcpy` | `strcpy(dst as lview, src as cstr_view) -> addr` | Copy the string including its terminating byte. When `length(dst)` is insufficient, checked mode MUST report an error. Return `&dst`. |
| `strncpy` | `strncpy(dst as lview, src as cstr_view, n as u64) -> addr` | When `n == 0`, write nothing. When `n > length(dst)`, checked mode MUST report an error. When `n > 0`, write exactly `n` bytes: when the terminating byte of `src` occurs among the first `n` bytes, copy the terminator and fill the remaining range with `0x00`; when no terminator occurs among the first `n` bytes, append no additional terminating byte. Return `&dst`. |
| `strcat` | `strcat(dst as lview, src as cstr_view) -> addr` | Search the visible extent of `dst` for its existing terminator and append `src` at that position. When no terminator is found or capacity is insufficient, checked mode MUST report an error. Return `&dst`. |
| `strchr` | `strchr(s as cstr_view, ch as view) -> addr` | Search for the lowest byte of `ch`. Return the address of the matching byte when found; otherwise return the null address. The returned address remains valid no longer than the lifetime of the storage located by `s`. When `s` is a temporary rvalue, the address is valid only within the current full expression. |

### 14.4 Standard Input and Output

| Function | Signature | Rules |
|---|---|---|
| `get` | `get() -> i32` | Read one byte from standard input and return a value in `[0,255]`; return an implementation-defined negative value at end of file or on error. |
| `put` | `put(x as view) -> i32` | Write the raw bytes of `x` to standard output and return the number of bytes written or a negative error code. |
| `print` | `print(x as view) -> i32` | A View whose template is `none` uses an implementation-defined byte format. A View carrying a standard or user template MUST have a matching `op format`; its absence MUST produce a compile-time diagnostic. |
| `printf` | `printf(fmt as cstr_view, ...) -> i32` | Write according to the format string and return the number of bytes written or a negative error code. |
| `scanf` | `scanf(fmt as cstr_view) -> left-context` | Built-in left-context input function that MAY appear only as the sole right-hand expression of a multiple-assignment statement. |

Template formatting for `print()` uses the ordinary `op format` candidate rules. The standard template library provides `format` for standard templates. A user template requires an explicit standard-signature `op format` declaration in the corresponding `impl`. A View whose template is `none` does not enter `op format` resolution.

When the format string is a literal, the number of conversion specifications, the number of left-context targets, target templates, and `%s` capacities MUST be checked statically. The capacity of a `%s` input target is determined by the length of the corresponding left-hand target; at most `length(target)-1` UTF-8 bytes are written, followed by `0x00`.

The `left-context` protocol of `scanf` and `fscanf` permits only the following multiple-assignment forms:

```hs
count, target0, target1 = scanf(fmt)
count, target0, target1 = fscanf(handle, fmt)
```

When a format string contains `K` conversion specifications that perform assignment, `K` MUST be greater than 0, and the left-hand side MUST provide `K + 1` targets. The first left-hand target receives the `i32` conversion count and MAY be written as `_` to discard it. Remaining targets are passed as writable scan targets in format-specification order and MUST be ordinary writable lvalues. `_` can be used only to discard the conversion count. A successfully converted target is written immediately. On matching failure, insufficient capacity, end of input, or an I/O error, the failed target and all subsequent targets retain their original values. The return count is the number of scan targets written successfully. When end of input or an I/O error occurs before the first conversion, return `-1`. When matching failure occurs before the first conversion, return `0`. A literal format string containing no assigning conversion specification MUST produce a compile-time diagnostic. For a dynamic format string, checked mode MUST validate at runtime the number of targets, target templates, target capacities, and `K > 0`.

### 14.5 File I/O

| Function | Signature | Rules |
|---|---|---|
| `fopen` | `fopen(name as cstr_view, mode as cstr_view) -> handle` | Open a file. On failure, return a null handle, represented by an all-zero `P`-byte `handle`, or throw according to implementation documentation. |
| `fclose` | `fclose(fh as handle) -> i32` | Close a file. Return 0 on success and a negative or implementation-defined error code on failure. |
| `fget` | `fget(fh as handle) -> i32` | Read one byte. Return an implementation-defined negative value at end of file or on error. |
| `fput` | `fput(fh as handle, x as view) -> i32` | Write the raw bytes of `x` and return the number of bytes written or a negative error code. |
| `fread` | `fread(dst as lview, size as u64, count as u64, fh as handle) -> u64` | Write at most `size * count` bytes and return the number of elements read successfully. In checked mode, multiplication overflow or insufficient destination capacity MUST be reported as an error. |
| `fwrite` | `fwrite(src as view, size as u64, count as u64, fh as handle) -> u64` | Read at most `size * count` bytes and return the number of elements written successfully. In checked mode, multiplication overflow or `size * count > length(src)` MUST be reported as an error. |
| `fprintf` | `fprintf(fh as handle, fmt as cstr_view, ...) -> i32` | Formatted file output. |
| `fscanf` | `fscanf(fh as handle, fmt as cstr_view) -> left-context` | Formatted file input. Left-context, partial-success, and failure rules are the same as for `scanf`. |
| `fflush` | `fflush(fh as handle) -> i32` | Flush the stream. |
| `fseek` | `fseek(fh as handle, offset as i64, whence as i32) -> i32` | Reposition the file. Values of `whence` are defined by implementation documentation; an implementation SHOULD be compatible with C `SEEK_SET/SEEK_CUR/SEEK_END`. |
| `ftell` | `ftell(fh as handle) -> i64` | Return the current position; return a negative error code on failure. |
| `feof` | `feof(fh as handle) -> bool` | Return the end-of-file status. |
| `ferror` | `ferror(fh as handle) -> i32` | Return the error status or error code. |

### 14.6 Mathematical Functions

| Function | Signature | Rules |
|---|---|---|
| `abs` | `abs(x as iN) -> iN` | Return the signed absolute value; in checked mode, overflow of the minimum negative value MUST be reported as an error. |
| `min` | `min(a as T, b as T) -> T` | `T` is the same integer or floating-point standard template. |
| `max` | `max(a as T, b as T) -> T` | `T` is the same integer or floating-point standard template. |
| `f_abs` | `f_abs(x as fN) -> fN` | Floating-point absolute value. |
| `f_sqrt` | `f_sqrt(x as fN) -> fN` | Floating-point square root. |
| `f_pow` | `f_pow(x as fN, y as fN) -> fN` | Floating-point exponentiation. |
| `f_sin` / `f_cos` / `f_tan` | `(x as fN) -> fN` | Trigonometric functions. |
| `f_log` / `f_exp` | `(x as fN) -> fN` | Logarithmic and exponential functions. |
| `f_floor` / `f_ceil` / `f_round` | `(x as fN) -> fN` | Rounding functions. |

Floating-point functions select 2-, 4-, 8-, or 16-byte floating-point lowering according to the input template. `f16` and `f128` in the minimum template set MUST have observable behavior. An implementation MAY use software emulation when hardware support is absent. Precision limits, performance limits, and exception-flag support of platform lowering are implementation-defined and MUST be documented.

### 14.7 Character-Classification Functions

| Function | Signature | Rules |
|---|---|---|
| `is_digit` | `is_digit(ch as view) -> bool` | Use the lowest byte and test for an ASCII digit. |
| `is_alpha` | `is_alpha(ch as view) -> bool` | Use the lowest byte and test for an ASCII letter. |
| `is_alnum` | `is_alnum(ch as view) -> bool` | Use the lowest byte and test for an ASCII letter or digit. |
| `is_space` | `is_space(ch as view) -> bool` | Use the lowest byte and test for ASCII whitespace. |
| `to_upper` | `to_upper(ch as view) -> u8` | Convert an ASCII lowercase letter to uppercase; return every other byte unchanged. |
| `to_lower` | `to_lower(ch as view) -> u8` | Convert an ASCII uppercase letter to lowercase; return every other byte unchanged. |

### 14.8 Random Numbers, Time, and Process Control

| Function | Signature | Rules |
|---|---|---|
| `srand` | `srand(seed as u32) -> ()` | Set the pseudorandom-number seed. |
| `rand` | `rand() -> u32` | Return a 4-byte unsigned pseudorandom integer. |
| `time_ms` | `time_ms() -> u64` | Return milliseconds since the Unix epoch. |
| `clock_ms` | `clock_ms() -> u64` | Return milliseconds from a monotonic clock. |
| `exit` | `exit(code as i32) -> ()` | Terminate the program normally. |
| `abort` | `abort() -> ()` | Terminate the program abnormally. |

### 14.9 Diagnostic Assistance

| Function | Signature | Rules |
|---|---|---|
| `assert` | `assert(condition as view, error_code as i32) -> ()` | Read the condition according to the Boolean-test rules. When false, throw, report an error, or terminate according to implementation documentation. |
| `panic` | `panic(error_code as i32) -> ()` | Immediately throw, report an error, or terminate according to implementation documentation. |

---

## 15. Preprocessor

Non-normative implementation recommendation: an implementation MAY use an mcpp-based preprocessor. Preprocessor directives use the `$` prefix. Macro expansion, conditional compilation, and file-inclusion semantics are based on the C99/C11 preprocessor; additional implementation extensions MUST be documented. Preprocessing completes before core-language parsing. A preprocessor directive MAY appear on any physical line. After preprocessing, the core parser receives only expanded core-language lexical tokens.

### 15.1 Directives

| Directive | Description |
|---|---|
| `$include` | Include a file |
| `$define` | Define a macro |
| `$undef` | Undefine a macro |
| `$if` / `$elif` / `$else` / `$endif` | Conditional compilation |
| `$ifdef` / `$ifndef` | Condition on macro existence |
| `$error` | Produce an error diagnostic |
| `$warning` | Produce a warning diagnostic |
| `$pragma` | Implementation-defined directive |

Non-line-breaking whitespace MAY appear between `$` and the directive name. Therefore, `$ include <stdio.hsh>` and `$include <stdio.hsh>` are equivalent.

### 15.2 Macro Expansion

Object-like macros, function-like macros, variadic macros, stringification `#`, and token concatenation `##` are supported. `#` and `##` are consumed according to preprocessing rules only within macro replacement lists. A macro MUST NOT expand directly or indirectly to itself. Macros are not expanded inside string literals, character literals, or comments.

### 15.3 File Inclusion

```hs
$include "utils.hsh"
$include <stdlib.hsh>
$include <string.hsh>
$include <stdio.hsh>
$include <math.hsh>
$include <ctype.hsh>
$include <time.hsh>
$include <assert.hsh>
```

The quoted form searches the current directory first. The angle-bracket form searches system include paths. Official system headers are grouped as shown above. Search paths and maximum inclusion depth are implementation-defined. An implementation SHOULD support an inclusion depth of at least 200 levels.

### 15.4 Predefined Macros

An implementation SHOULD provide predefined macros including `__HS_VERSION__`, `__HS_POINTER_SIZE__`, `__HS_BYTE_ORDER__`, `__HS_EXECUTION_MODE__`, `__HS_STATIC_CHECKED__`, and `__HS_CHECKED__`, and SHOULD document their concrete values.

---

## 16. C Compatibility Layer

The C compatibility layer is an equivalent syntax layer. Compatibility syntax MUST be translated into HitSimple core syntax before semantic analysis; all subsequent behavior is defined by reference to core semantics. This Standard defines only the minimum compatibility subset. An implementation that accepts additional C syntax MUST document it as an extension.

### 16.1 Minimum Compatibility EBNF

```ebnf
c_compat_decl      = c_func_decl | c_func_proto | c_extern_var_decl | c_var_decl | c_struct_decl | c_typedef_decl ;
c_storage          = "static" | "extern" ;
c_qualifier        = "const" | "volatile" ;
c_type             = { c_qualifier } c_base_type ;
c_base_type        = "char" | "signed" "char" | "unsigned" "char"
                   | "short" | "unsigned" "short"
                   | "int" | "unsigned" "int"
                   | "long" | "unsigned" "long"
                   | "float" | "double" | "void"
                   | "struct" IDENT | IDENT ;
c_declarator       = { "*" } IDENT [ "[" INTEGER_LITERAL "]" ] ;
c_func_declarator  = { "*" } IDENT "(" [ c_param_list ] ")" ;
c_var_decl         = [ "static" ] c_type c_declarator [ "=" c_expression ] terminator ;
c_typedef_decl     = "typedef" c_type c_declarator terminator ;
c_func_decl        = [ c_storage ] c_type c_func_declarator block [ terminator ] ;
c_func_proto       = [ c_storage ] c_type c_func_declarator terminator ;
c_extern_var_decl  = "extern" c_type c_declarator terminator ;
c_param_list       = c_param { "," c_param } ;
c_param            = c_type c_declarator | "void" ;
c_struct_decl      = "struct" IDENT "{" separator { c_field_decl } "}" [ terminator ] ;
c_field_decl       = c_type c_declarator terminator ;

c_expression       = c_assignment_expr ;
c_assignment_expr  = c_conditional_expr
                    | c_unary_expr c_assign_op c_assignment_expr ;
c_assign_op        = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "<<=" | ">>=" | "&=" | "^=" | "|=" ;
c_conditional_expr = c_logical_or_expr [ "?" c_expression ":" c_conditional_expr ] ;
c_logical_or_expr  = c_logical_and_expr { "||" c_logical_and_expr } ;
c_logical_and_expr = c_bit_or_expr { "&&" c_bit_or_expr } ;
c_bit_or_expr      = c_bit_xor_expr { "|" c_bit_xor_expr } ;
c_bit_xor_expr     = c_bit_and_expr { "^" c_bit_and_expr } ;
c_bit_and_expr     = c_equality_expr { "&" c_equality_expr } ;
c_equality_expr    = c_relational_expr { ("==" | "!=") c_relational_expr } ;
c_relational_expr  = c_shift_expr { ("<" | "<=" | ">" | ">=") c_shift_expr } ;
c_shift_expr       = c_additive_expr { ("<<" | ">>") c_additive_expr } ;
c_additive_expr    = c_multiplicative_expr { ("+" | "-") c_multiplicative_expr } ;
c_multiplicative_expr
                   = c_cast_expr { ("*" | "/" | "%") c_cast_expr } ;
c_cast_expr        = "(" c_type { "*" } ")" c_cast_expr
                    | c_unary_expr ;
c_unary_expr       = ("*" | "&" | "!" | "~" | "-" | "+") c_unary_expr
                    | c_postfix_expr ;
c_postfix_expr     = c_primary_expr { c_subscript | c_call | c_member | c_arrow } ;
c_subscript        = "[" c_expression "]" ;
c_call             = "(" [ c_expression { "," c_expression } ] ")" ;
c_member           = "." IDENT ;
c_arrow            = "->" IDENT ;
c_primary_expr     = IDENT | literal | "sizeof" "(" (c_type | IDENT) ")" | "(" c_expression ")" ;
```

`c_expression` is the compatibility-layer expression subset and is used only for translation before entering core semantics. A compatibility-layer function body is based on the core `block` and core statement skeleton. At every position in a core statement, declaration initializer, return statement, condition, loop header, function argument, or assignment right-hand side that requires an `expression`, the compatibility layer MAY first accept a `c_expression` and MUST translate it into a core `expression` before entering core semantics. An implementation that accepts increment/decrement, comma expressions, compound literals, function-pointer calls, or GNU extension expressions MUST document them as extensions.

A bare `IDENT` in `c_type` MUST resolve to a `typedef` alias registered by this compatibility layer. An unknown alias MUST produce a compile-time diagnostic. When a `c_declarator` contains both a `*` prefix and an array suffix, the minimum compatibility layer interprets it according to C declarator rules as “an array whose elements are pointer values.” For example, `int *a[4]` is translated into a contiguous byte array containing four `addr` elements; the pointer depth is retained as array-element type metadata. Multidimensional arrays, parenthesized complex declarators, function pointers, and pointers to arrays are outside the minimum compatibility subset. An implementation that accepts them MUST document them as extensions. In a `c_func_declarator`, the number of `*` tokens before `IDENT` denotes the pointer depth of the function return value. For example, `char *f(void)` is translated into a core function returning `[P] as addr`. During compatibility-layer parsing, function declarators MUST be distinguished from variable declarators before reduction. An `extern` variable declaration is reduced only as `c_extern_var_decl`. `static` in a definition with a function body or in a function prototype produces internal-linkage metadata, while `extern` produces external-linkage metadata. This metadata is not part of core syntax, and its representation is documented by the implementation. In the minimum compatibility layer, `const` and `volatile` are accepted as compatibility qualifiers. Before entering core semantics, they MUST either be translated into implementation-documented read-only/volatile metadata or be diagnosed as disabled extensions. C compatibility-layer declarations MAY use a semicolon or line break as a terminator; the semicolon in traditional C spelling is handled as the core `terminator`. `typedef` creates only a compatibility-layer translation-table entry and does not enter core syntax. `void` MAY be used only as a function return, an empty-parameter-list marker, or the base type of `void*`. A `void` parameter-list marker MUST be the sole parameter item. A `void` object, field, array, or by-value parameter MUST produce a compile-time diagnostic.

### 16.2 Type Translation Table

| C compatibility spelling | Core template/length | Description |
|---|---|---|
| `char` | Scalar: `as u8`; array: `as bytes`, or `as cstr` with string initialization | Byte character |
| `signed char` | `as i8` | 1 byte |
| `unsigned char` | `as u8` | 1 byte |
| `short` | `as i16` | 2 bytes |
| `unsigned short` | `as u16` | 2 bytes |
| `int` | `as i32` | 4 bytes |
| `unsigned int` | `as u32` | 4 bytes |
| `long` | `as i64` | 8 bytes; other ABI widths are extensions |
| `unsigned long` | `as u64` | 8 bytes |
| `float` | `as f32` | 4 bytes |
| `double` | `as f64` | 8 bytes |
| `void` return | `-> ()` | No return value |
| `T *` | `[P] as addr` | Pointer value; for a function return, MAY be written as return template `addr` or explicit `[P] as addr` |
| `T name[N]` | `name[<N*sizeof(T)>] as bytes` | `N` is the element count. Before entering core syntax, the translator MUST expand the total length into a static integer literal measured in bytes. A `char` array MAY use `bytes`, or `cstr` when initialized from a string. |
| `T *name[N]` | `name[<N*P>] as bytes` | Pointer array. Each element is a `[P] as addr` value. Element-type metadata is used for compatibility-layer subscripting and decay translation. |
| `struct S` | `as S` | Corresponds to `template S` |

### 16.3 Declaration Translation Table

| C compatibility syntax | Core syntax |
|---|---|
| `int x` | `new x as i32` |
| File-scope `static int x` | `new x as i32` + internal-linkage metadata |
| Block-scope `static int x` | `static x as i32` |
| `extern int errno` | `extern errno as i32` |
| `double y = 1.0` | `new y as f64 = 1.0` |
| `char buf[64]` | `new buf[64] as bytes` |
| `char name[32] = "Kai"` | `new name[32] as cstr %s= "Kai"` |
| `int arr[4]` | `new arr[16] as bytes` |
| `int *p` | `new p[P] as addr` |
| `int *a[4]` | `new a[<4*P>] as bytes` + element-template metadata `addr` |
| `int f(int a)` | `func f(a as i32) -> i32` |
| `static int helper(void)` | `func helper() -> i32` + internal-linkage metadata |
| `char *next(char *s)` | `func next(s[P] as addr) -> addr` |
| `extern char *getenv(char *name)` | `extern getenv(name[P] as addr) -> addr` |
| `extern int puts(char *s)` | `extern puts(s[P] as addr) -> i32` |

Array-to-address decay in the compatibility layer is permitted only during translation. In core semantics, a variable name does not implicitly convert to an address.

### 16.4 `struct` to `template`

```c
struct Point {
    int x
    int y
}
```

is translated into:

```hs
template Point {
    x[4] as i32
    y[4] as i32
}
```

C-style field declarations are translated into template members according to Section 16.2. Members are packed in declaration order. C ABI alignment and padding are implementation extensions and MUST be documented.

### 16.5 Pointer and Array Translation

Compatibility-layer pointer syntax is translated into the `addr` template, explicit unsigned address calculation, core slices, and `[N]*expr` dereference. A pointer expression MUST carry element-template metadata, element byte-size metadata, and address-origin metadata. The absence of a statically determinable element size MUST produce a diagnostic.

```hs
// C compatibility layer: p + i, where the element type of p is T
new __tmp_ptr as addr
__tmp_ptr = (p as addr)? + (i? * sizeof(T))
```

The minimum translation rules are as follows:

| C compatibility expression | Core translation requirement |
|---|---|
| `p + i` / `i + p` | Translate to address value `p? + i? * sizeof(T)`; the result template is `addr`, and element metadata remains `T` |
| `p - i` | Translate to address value `p? - i? * sizeof(T)`; the result template is `addr`, and element metadata remains `T` |
| `p - q` | Both operands MUST have identical element metadata. Translate to `(p? - q?) / sizeof(T)`. The result uses an implementation-selected signed-integer template; the implementation SHOULD select `i64`. |
| `*p` | Translate to `[sizeof(T)]*p as T`; when `T` is `void` or has unknown size, a diagnostic MUST be produced |
| `&x` | Translate to core `&x`; the result template is `addr`, and element metadata for the addressed object is recorded |
| `a[i]` | Translate an array object to `a[i * sizeof(T) : + sizeof(T)] as T`; when the array expression first decays to an address, translate according to `*(a + i)` |
| `p[i]` | Translate to `[sizeof(T)]*(p? + i? * sizeof(T)) as T` |
| `s.m` | Translate to core member access `s.m` |
| `p->m` | Translate to `([sizeof(S)]*p as S).m`, where the element metadata of `p` MUST be `struct S` |
| `(T*)expr` | Translate to `expr as addr` and set the element metadata to `T` |
| `(T)expr` | Translate a scalar numeric cast into the corresponding `to_iN()`, `to_uN()`, `to_fN()`, or explicit assignment path. Pure template reinterpretation MUST use `as` and satisfy length consistency. |

`sizeof(T)` MUST be expanded into a static integer literal measured in bytes during compatibility-layer translation. In the core layer, `a[i]` remains byte indexing. When the compatibility layer requires indexing by element size, it MUST translate the operation into a core slice or dereference after multiplying by the element size. Access to a pointer-array element MUST be translated into a slice or temporary View of length `P`, carrying template `addr` for subsequent assignment, comparison, or function-argument matching. Every translation of C pointer-plus/minus-integer and array subscripting MUST use the element size. Implicit array-to-address decay is permitted only during compatibility-layer translation.

### 16.6 Legacy-Syntax Compatibility Mode

Legacy forms `;Template`, `;none`, `[sN]`, `reinterpret()`, `to_float()`, and `to_int()` are outside both core syntax and the C compatibility layer and MUST produce a compile-time diagnostic in standard mode.

An implementation MAY provide a legacy-syntax compatibility mode that is disabled by default and requires explicit activation. Before entering core syntax, that mode MAY translate `;Template` and `;none` into `as Template` and `as none`, and MAY translate `[sN]` into `[tN]`. The implementation MUST document how the mode is enabled, the accepted legacy syntax set, and the translation rules. Even in legacy-syntax compatibility mode, `reinterpret()`, `to_float()`, and `to_int()` MUST be diagnosed according to Section 7.9, with the corresponding replacement names provided. A program that depends on this mode is not a standard-mode program.

### 16.7 Compatibility-Layer Diagnostics

The following conditions MUST produce a diagnostic:

- A C compatibility type cannot be mapped to a fixed core template or length.
- A `typedef` name is unknown, cyclically defined, or cannot be mapped to a fixed core template or length.
- A C function signature cannot determine parameter lengths or the return View statically.
- The return pointer depth in a C function declarator cannot be mapped to an `addr` return View.
- A multidimensional array, parenthesized complex declarator, function pointer, or pointer to an array appears in standard mode.
- Pointer arithmetic lacks an element size or element core template.
- A C compatibility expression cannot be translated into a core expression according to Section 16.5.
- Array decay crosses the compatibility layer into core semantics.
- An `IDENT` type name does not resolve to a registered `typedef` alias.
- `void` is used as an object, field, array, or by-value parameter.
- The compatibility-layer translation result violates core syntax or core semantics.

## 17. FFI and External Linkage

### 17.1 `extern` Functions

```hs
extern puts(s[P] as addr) -> i32
extern fread(dst[P] as addr, size as u64, count as u64, fh as handle) -> u64
```

External-function parameters and return signatures MUST have statically determinable lengths. The ABI of external symbols is implementation-defined. This Standard does not require binary compatibility with the host C ABI. An implementation that claims such compatibility MUST document the details.

The variable-length templates `cstr` and `bytes` MUST NOT appear by value in a core `extern` function signature. To pass a C string or buffer, a program SHOULD use `[P] as addr`, and the ABI documentation SHOULD describe the constraints on the object referenced by that address. The compatibility layer MAY translate `char*` or `cstr*` into `[P] as addr`.

### 17.2 `extern` Variables

```hs
extern errno as i32
extern global_buf[256] as bytes
```

An `extern` variable declaration MAY omit its length; when omitted, the length is inferred from the template. A variable-length template such as `bytes` or `cstr` MUST have an explicit length. An external variable is a locatable lvalue View. Its lifetime, thread locality, alignment, writability, and initialization state are implementation-defined.

### 17.3 FFI Addresses and File Handles

Boundary-checking capability for addresses received through FFI, raw addresses, and file handles is implementation-defined. A file handle is passed as a `handle` View. Behavior defined by the standard language layer includes passing and returning handles through file-I/O functions, same-template default assignment, equality comparison, and formatting. When an unverifiable address is dereferenced, freed, or rebound, static-checked mode handles only statically provable errors. In checked mode, the implementation SHOULD report errors within its detection capability and MUST document the cases it cannot detect.

### 17.4 Minimum ABI Documentation Items

An implementation that provides external linkage MUST document:

1. Symbol-name encoding.
2. Calling convention.
3. Argument-passing mechanism.
4. Return-value storage mechanism.
5. Multiple-return ABI.
6. Vararg ABI.
7. Representation of `addr`, file handles, and external-object addresses.
8. The extent of compatibility with the host C ABI.

---

## 18. Execution Modes, Diagnostics, and Safety-Check Requirements

### 18.1 General Requirements

An implementation MUST produce a compile-time diagnostic for any violation of this Standard that it can prove statically. The syntax, name, template, return-signature, overload, and compatibility-layer diagnostics listed in Section 18.2 are independent of execution mode.

Execution mode affects only the safety-checking policy. Unchecked mode preserves low-level freedom, and many runtime errors are undefined behavior. Static-checked mode MUST report every statically provable safety error as a compile-time diagnostic and MUST NOT insert runtime-checking code. Checked mode MUST report statically provable safety errors and MUST report runtime-detectable safety errors as runtime errors.

### 18.2 Required Diagnostic Conditions

A conforming implementation MUST diagnose the following conditions:

1. Invalid UTF-8, an invalid literal, or an invalid escape sequence.
2. Use of a supported keyword, reserved keyword, standalone `_`, or reserved shape `t[0-9]+` as an ordinary identifier or macro name.
3. A declaration length of 0, a template-instance declaration count of 0, or a dereference length of 0.
4. A mismatch between a fixed-length template and a View length.
5. `as` refers to an unknown template.
6. A user-template or standard-template name is redefined.
7. An `impl` refers to an unknown template.
8. An `op` parameter lacks a template.
9. The parameter count of a user `op` does not match the standard arity of the operator, or `op format` does not conform to the standard formatting signature.
10. The length of an `op` return signature cannot be determined statically, or an ordinary function/method return item lacks a static length.
11. An `op` return signature is `-> ()`.
12. `op` definitions have the same overload key, including definitions with identical parameter-template sequences but different return signatures.
13. An ordinary operator has no applicable candidate or multiple applicable candidates.
14. The left side of a method call has no template, or the template contains no corresponding method.
15. An implicit cross-template conversion occurs.
16. `mut self` or any `mut` parameter is used in standard mode.
17. An rvalue is assigned to, has its address taken, or is incremented or decremented.
18. The left-hand side of `&=` is not a single `IDENT`.
19. A `set` target is not a name or member chain, or the member chain cannot be located statically as a fixed-length region.
20. A `goto` enters the scope of an uninitialized local object.
21. A function-call argument count or return-value count does not match.
22. The number of conversion specifications in a literal format string does not match.
23. A literal standard-library format string does not match the argument templates.
24. An `extern` function signature contains a variable-length template by value.
25. An `extern` variable uses a variable-length template and omits its length.
26. A C compatibility-layer translation result cannot be mapped into core semantics.
27. `sizeof(bytes)` or `sizeof(cstr)` is used.
28. A user-template member uses `[tN]`.
29. A template-instance declaration `[tN]` lacks a user template; a template-instance access `[tK]` lacks a template-instance-array View or has an out-of-range index; or `[tN]` appears outside a `new`/`static` template-instance declaration or template-instance access.
30. Parsing of ternary `? :` and postfix `?` violates Section 4.8.
31. Default assignment occurs outside the standard-template assignment matrix.
32. An explicit typed-operator width is invalid, or a nonliteral operand of `%f` has a length other than 2, 4, 8, or 16.
33. A standard-library `left-context` function appears outside the standard multiple-assignment form, or a literal `scanf`/`fscanf` format string contains no assigning conversion specification.
34. A preprocessor-only token `#` or `##` appears in the core language.
35. A C compatibility-layer `IDENT` type name does not resolve to a registered `typedef` alias.
36. A non-`char` array in the C compatibility layer is not translated according to `N * sizeof(T)`.
37. C compatibility-layer pointer arithmetic does not compute its byte offset using the element size.
38. A by-value parameter of an ordinary function, template method, or core `extern` uses `bytes`, `cstr`, or `none` and omits its length.
39. A return signature uses `-> none`, `-> as none`, `-> bytes`, `-> cstr`, or another form whose length cannot be determined.
40. The C compatibility layer uses `void` as an object, field, array, or by-value parameter.
41. `sizeof(Template)` dependencies in template layouts form a cycle.
42. The standard-library meta-signature form `none[len]` appears in a core function, template method, `op`, or `extern` signature.
43. An ordinary operation outside the standard-permitted behavior is performed on `handle`.
44. An unknown variable, function, member, label, or template name is used.
45. A name is redeclared in the same lexical scope, or a label is defined more than once in the same function.
46. `break` or `continue` appears outside a loop.
47. A `goto` target is cross-function, undefined, or duplicated.
48. A semicolon appears outside a statement terminator, `for`-header separator, or corresponding C compatibility-layer position; a bare semicolon inside any other expression position MUST produce a syntax diagnostic.
49. A destination whose template is `none` receives a source View that cannot be interpreted as an integer.
50. A C compatibility-layer function return pointer cannot be mapped to an `addr` return View.
51. A file-scope `static` function or variable in the C compatibility layer cannot be supplied with internal-linkage metadata.
52. A template method lacks an explicit first parameter, or the first-parameter template does not equal the enclosing `impl` template.
53. Definitions have the same method-overload key, or a method call has multiple applicable candidates.
54. The length of a `catch` parameter cannot be determined statically.
55. `_` is used in a `scanf`/`fscanf` left context for any target other than the first count target, or a scan target is not a writable lvalue.
56. The result Views of the two branches of ternary `? :` differ in template or length.
57. A View carrying a standard or user template is passed to `print()` without a matching `op format`.
58. A multidimensional array, parenthesized complex declarator, function pointer, or pointer to an array appears in the C compatibility layer in standard mode.
59. An expression outside the C compatibility-layer expression subset appears in standard mode, or a compatibility expression cannot be translated into core semantics.
60. Recursive entry into the same function-local `static` declaration during initialization can be proven statically.

### 18.3 Safety Diagnostics in Static-Checked Mode

In static-checked mode, each of the following safety errors MUST produce a compile-time diagnostic whenever it can be proven statically:

1. A static index, static slice, or static dereference length is out of bounds.
2. Compile-time constant division by zero, a negative shift count, or a negative exponent.
3. A null-address dereference determinable at compile time.
4. An invalid free, double free, or use after free determinable at compile time.
5. A dynamic-length mismatch or template-View length inconsistency determinable at compile time.
6. A target-count, target-template, or target-capacity mismatch caused by a literal or statically known format specification.
7. Insufficient standard-library destination capacity determinable at compile time.
8. `size * count` overflow determinable at compile time.
9. Absence of a terminating `0x00` within the visible extent of a `cstr` View determinable at compile time.
10. Insufficient capacity or absent boundaries for a raw address used as a positive-length `mem_view`/`mem_lview`, when determinable at compile time.
11. Recursive entry into a function-local `static` initializer determinable at compile time.

Static-checked mode MUST NOT insert runtime-checking code for the checks above. Dynamic behavior whose safety cannot be proven statically follows the corresponding unchecked-mode clause.

### 18.4 Runtime Errors in Checked Mode

In checked mode, each of the following conditions MUST produce a compile-time diagnostic when statically provable and MUST report a runtime error when detectable at runtime:

1. Out-of-bounds access.
2. Invalid dynamic-memory free, double free, or use after free.
3. Null-address dereference.
4. Integer division by zero.
5. A negative shift count or negative exponent.
6. A dynamic-length mismatch.
7. A dynamic-format-specification mismatch.
8. A template-View length inconsistency determinable only at runtime.
9. Absence of a terminating `0x00` within the visible extent of a `cstr` View.
10. Insufficient destination capacity in a standard-library function.
11. `size * count` overflow.
12. Detectable overlap in `memcpy`.
13. Overflow of `abs(iN_min)`.
14. An invalid file handle, when detectable by the implementation.
15. Insufficient source or destination length for `memcpy`, `memmove`, or `memcmp`.
16. Insufficient source-View length for `fwrite`.
17. A mismatch between a dynamic `scanf`/`fscanf` format specification and left-context targets, or a dynamic format string containing no assigning conversion specification.
18. Insufficient capacity, missing boundaries, or inability to form the required readable/writable View for a raw address used as a positive-length `mem_view`/`mem_lview`.
19. Recursive entry into the same function-local `static` declaration during initialization.

---

## 19. Implementation-Defined Behavior Checklist

A conforming implementation MUST document the following behavior:

1. Target-platform pointer length `P`.
2. Byte order.
3. The implementation mechanism for standard-template fast paths.
4. Support mechanisms and limitations for `f16` and `f128`.
5. NaN-payload propagation rules.
6. External-linkage ABI, symbol-name encoding, calling convention, and argument/return-value storage.
7. Multiple-return ABI and vararg ABI.
8. Representation and error codes for non-null file handles; the all-zero representation of a null handle is fixed by this Standard.
9. Alignment guarantees.
10. How static-checked and checked modes are enabled; checked-mode error codes and the policy for converting errors into `throw`.
11. Boundary-detection capability for raw addresses, FFI addresses, and file handles.
12. Dynamic-memory allocation-failure behavior.
13. Behavior of `alloc(0)` and `realloc(ptr, 0)`.
14. Preprocessor search paths, predefined macros, and extension directives.
15. Names and behavior of standard-library extension functions.
16. How the C compatibility layer is enabled, its extension syntax, and extension translation rules.
17. The element-size inference policy for compatibility-layer pointer extensions.
18. How legacy-syntax compatibility mode is enabled, its accepted syntax, translation rules, and diagnostic policy.
19. Process termination, I/O flushing, and runtime-resource release policy.
20. Pseudorandom-number algorithm.
21. Alternative behavior of `time_ms()` and `clock_ms()` when system time support is unavailable.
22. The display format used to format `bytes`.
23. Absolute magnitudes of nonzero return values from `memcmp` and `strcmp`.
24. End-of-file and error return values from `get()` and `fget()`.
25. The set of format specifications supported by `printf`, `fprintf`, `scanf`, and `fscanf`.
26. Representation of internal-linkage metadata for C file-scope `static`.
27. The set of `whence` values and error codes for `fseek()`.
28. Meanings of negative error codes returned by I/O functions including `put()`, `fput()`, `fflush()`, `printf()`, and `fprintf()`.
29. The throw, reporting, or termination policy of `assert()` and `panic()`.
30. Software-emulation and documentation policy for floating-point mathematical functions when the target platform lacks hardware lowering, has precision limitations, or has limited exception-flag support.
31. Exit code, error-reporting form, and I/O-flushing policy for an uncaught `throw`.
32. Unchecked-mode behavior of `to_iN()` / `to_uN()` for overflow, negative values, infinity, and NaN.
33. The legacy standard-library names `to_float()`, `to_int()`, and `reinterpret()` MUST be rejected, and diagnostics SHOULD provide the corresponding replacement names.
34. The concrete evaluation order among function-argument expressions.
35. The synchronization policy for function-local `static` initialization when a concurrent-execution extension is provided.
36. The concrete signed-integer template used for pointer differences in the C compatibility layer.

---

## 20. Appendix: Complete EBNF

This EBNF describes the syntactic shape of the core language. `preprocessor_directive` describes only the shape of source-file lines before preprocessing. A preprocessing directive MAY appear on any physical line and does not enter core syntax after preprocessing. Disambiguation between postfix `?` and ternary `? :` follows Section 4.8. The semantics of a `return_item` containing only `length_spec` and omitting `template_mark` follow Section 4.4; its return template is `none`. An implementation MAY use an equivalent parser technology.

```ebnf
program             = { external_decl | preprocessor_directive | terminator } ;
external_decl       = function_def | extern_decl | global_decl | template_def | impl_def ;
terminator          = (NEWLINE | ";") { NEWLINE | ";" } ;
separator           = { NEWLINE | ";" } ;
newline_gap         = { NEWLINE } ;

preprocessor_directive
                    = "$" { HSPACE } IDENT { pp_token } NEWLINE ;
HSPACE              = " " | "\t" ;
pp_token            = IDENT | literal | operator | punctuator | STRING_LITERAL | CHAR_LITERAL ;
operator            = "+" | "-" | "*" | "/" | "%" | "**" | "<" | ">" | "<=" | ">="
                    | "==" | "!=" | "!" | "&&" | "||" | "&" | "|" | "^" | "~"
                    | "<<" | ">>" | "=" | "%d=" | "%f=" | "%s=" | "%b=" | "&="
                    | "++" | "--" | "?" | "->" | "#" | "##" | typed_operator ;
punctuator          = "(" | ")" | "[" | "]" | "{" | "}" | "," | ";" | ":" | "." ;

global_decl         = "new" decl_list terminator ;
local_decl          = ("new" | "static") decl_list terminator ;
decl_list           = decl_item { "," decl_item }
                    | "{" { NEWLINE } decl_item { "," { NEWLINE } decl_item } [ "," ] { NEWLINE } "}" ;
decl_item           = IDENT [ length_spec ] [ template_mark ] [ init_assign_op expression ] ;

length_spec         = byte_length_spec | pointer_length_spec | template_count_spec ;
byte_length_spec    = "[" ( INTEGER_LITERAL | sizeof_expr ) "]" ;
pointer_length_spec = "[" "P" "]" ;
template_count_spec = "[" TEMPLATE_COUNT_LITERAL "]" ;
template_mark       = "as" template_name ;
template_name       = IDENT | "none" ;
init_assign_op      = "=" | "%d=" | "%f=" | "%s=" | "%b=" ;
normal_assign_op    = "=" | "%d=" | "%f=" | "%s=" | "%b=" ;

extern_decl         = extern_func_decl | extern_var_decl ;
extern_var_decl     = "extern" IDENT [ length_spec ] [ template_mark ] terminator ;
extern_func_decl    = "extern" IDENT "(" [ extern_param_list ] ")" return_sig terminator ;
extern_param_list   = extern_param { "," extern_param } ;
extern_param        = IDENT [ length_spec ] [ template_mark ] ;

function_def        = "func" IDENT "(" [ param_list ] ")" [ return_sig ] block [ terminator ] ;
param_list          = param { "," param } ;
param               = IDENT [ length_spec ] [ template_mark ] ;
return_sig          = "->" "()"
                    | "->" return_item
                    | "->" "(" return_item { "," return_item } ")" ;
return_item         = IDENT [ return_item_ident_tail ]
                    | "none"
                    | return_template_mark
                    | length_spec [ template_mark ] ;
return_item_ident_tail
                    = return_template_mark
                    | length_spec [ template_mark ] ;
return_template_mark
                    = "as" return_template_name ;
return_template_name
                    = IDENT | "none" ;
block               = "{" separator { statement | terminator } "}" ;

template_def        = "template" IDENT "{" separator { template_member terminator } "}" [ terminator ] ;
template_member     = IDENT length_spec [ template_mark ] ;

impl_def            = "impl" IDENT "{" separator { impl_item [ terminator ] } "}" [ terminator ] ;
impl_item           = op_def | method_def ;
op_def              = "op" overloadable_operator "(" op_param_list ")" return_sig block ;
op_param_list       = op_param { "," op_param } ;
op_param            = IDENT [ length_spec ] template_mark ;
method_def          = "func" IDENT "(" [ method_param_list ] ")" [ return_sig ] block ;
method_param_list   = method_param { "," method_param } ;
method_param        = method_param_name [ length_spec ] template_mark ;
method_param_name   = IDENT | "self" ;
overloadable_operator
                    = "=" | "==" | "!=" | "+" | "-" | "*" | "/" | "%" | "**"
                    | "<<" | ">>" | "&" | "|" | "^"
                    | "<" | "<=" | ">" | ">=" | "format" ;

statement           = local_decl
                    | multi_assign terminator
                    | expr_stmt terminator
                    | return_stmt terminator
                    | throw_stmt terminator
                    | if_stmt [ terminator ]
                    | for_stmt [ terminator ]
                    | while_stmt [ terminator ]
                    | try_stmt [ terminator ]
                    | jump_stmt terminator
                    | label_stmt terminator
                    | set_stmt terminator ;
multi_assign        = assign_target "," assign_target { "," assign_target } "=" expression { "," expression } ;
assign_target       = lvalue | discard_target | "(" lvalue explicit_assign_op ")" ;
discard_target      = "_" ;
explicit_assign_op  = "%d=" | "%f=" | "%s=" | "%b=" ;
expr_stmt           = expression | lvalue "++" | lvalue "--" ;
return_stmt         = "return" [ expression { "," expression } | "(" expression { "," expression } ")" ] ;
throw_stmt          = "throw" expression ;
jump_stmt           = "break" | "continue" | "goto" IDENT ;
label_stmt          = IDENT ":" ;
set_stmt            = "set" set_target template_mark ;
set_target          = IDENT { member_suffix } ;

if_stmt             = "if" "(" expression ")" block { newline_gap "else" "if" "(" expression ")" block } [ newline_gap "else" block ] ;
for_stmt            = "for" "(" [ for_init ] ";" [ expression ] ";" [ for_post ] ")" block ;
for_init            = ("new" | "static") decl_item { "," decl_item } | multi_assign | expression ;
for_post            = expr_stmt { "," expr_stmt } ;
while_stmt          = "while" "(" expression ")" block ;
try_stmt            = "try" block newline_gap "catch" "(" IDENT [ length_spec ] [ template_mark ] ")" block ;

expression          = assignment_expr ;
assignment_expr     = conditional_expr
                    | lvalue normal_assign_op assignment_expr
                    | IDENT "&=" assignment_expr ;
conditional_expr    = logical_or_expr [ "?" expression ":" conditional_expr ] ;
logical_or_expr     = logical_and_expr { "||" logical_and_expr } ;
logical_and_expr    = bit_or_expr { "&&" bit_or_expr } ;
bit_or_expr         = bit_xor_expr { ("|" | typed_bit_or) bit_xor_expr } ;
bit_xor_expr        = bit_and_expr { ("^" | typed_bit_xor) bit_and_expr } ;
bit_and_expr        = equality_expr { ("&" | typed_bit_and) equality_expr } ;
equality_expr       = relational_expr { ("==" | "!=") relational_expr } ;
relational_expr     = shift_expr { ("<" | "<=" | ">" | ">=") shift_expr } ;
shift_expr          = additive_expr { ("<<" | ">>" | typed_shift) additive_expr } ;
additive_expr       = multiplicative_expr { ("+" | "-" | typed_add) multiplicative_expr } ;
multiplicative_expr = power_expr { ("*" | "/" | "%" | typed_mul) power_expr } ;
power_expr          = template_view_expr [ ("**" | typed_power) power_expr ] ;
template_view_expr  = unary_expr [ template_mark ] ;
unary_expr          = ("&" | "!" | "~" | "-" | deref_prefix) unary_expr
                    | sizeof_expr
                    | postfix_expr ;
deref_prefix        = "[" ( INTEGER_LITERAL | "P" | sizeof_expr ) "]*" ;
sizeof_expr         = "sizeof" "(" IDENT ")" ;
postfix_expr        = primary_expr { postfix_suffix } ;
postfix_suffix      = unsigned_suffix | call_suffix | index_suffix | method_suffix | member_suffix ;
unsigned_suffix     = "?" ;
call_suffix         = "(" [ expression { "," expression } ] ")" ;
method_suffix       = "." IDENT "(" [ expression { "," expression } ] ")" ;
member_suffix       = "." IDENT ;
index_suffix        = "[" expression "]"
                    | "[" expression ":" expression "]"
                    | "[" expression ":+" expression "]"
                    | "[" TEMPLATE_COUNT_LITERAL "]" ;
primary_expr        = IDENT | literal | "(" expression ")" ;
literal             = INTEGER_LITERAL | FLOAT_LITERAL | CHAR_LITERAL | STRING_LITERAL | BOOL_LITERAL ;

lvalue              = lvalue_core [ template_mark ] ;
lvalue_core         = lvalue_atom { lvalue_suffix } ;
lvalue_atom         = IDENT | deref_lvalue | "(" lvalue ")" ;
lvalue_suffix       = index_suffix | member_suffix ;
deref_lvalue        = deref_prefix unary_expr ;

typed_add           = typed_int_add | typed_int_sub | typed_float_add | typed_float_sub ;
typed_mul           = typed_int_mul | typed_float_mul ;
typed_shift         = typed_int_shift ;
typed_power         = typed_int_power | typed_float_power ;
typed_bit_and       = typed_int_bit_and ;
typed_bit_or        = typed_int_bit_or ;
typed_bit_xor       = typed_int_bit_xor ;
typed_int_add       = TYPED_INT_ADD ;
typed_int_sub       = TYPED_INT_SUB ;
typed_int_mul       = TYPED_INT_MUL ;
typed_int_shift     = TYPED_INT_SHIFT ;
typed_int_power     = TYPED_INT_POWER ;
typed_int_bit_and   = TYPED_INT_BIT_AND ;
typed_int_bit_or    = TYPED_INT_BIT_OR ;
typed_int_bit_xor   = TYPED_INT_BIT_XOR ;
typed_float_add     = TYPED_FLOAT_ADD ;
typed_float_sub     = TYPED_FLOAT_SUB ;
typed_float_mul     = TYPED_FLOAT_MUL ;
typed_float_power   = TYPED_FLOAT_POWER ;
typed_operator      = typed_add | typed_mul | typed_shift | typed_power | typed_bit_and | typed_bit_or | typed_bit_xor ;

DIGITS              = DIGIT { DIGIT } ;
DIGIT               = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;
TEMPLATE_COUNT_LITERAL = "t" DIGITS ;
INT_WIDTH           = DIGITS ;
FLOAT_WIDTH         = DIGITS ;
TYPED_INT_ADD       = "%d+" | "%" INT_WIDTH "d+" ;
TYPED_INT_SUB       = "%d-" | "%" INT_WIDTH "d-" ;
TYPED_INT_MUL       = "%d*" | "%d/" | "%d%" | "%" INT_WIDTH "d*" | "%" INT_WIDTH "d/" | "%" INT_WIDTH "d%" ;
TYPED_INT_SHIFT     = "%d<<" | "%d>>" | "%" INT_WIDTH "d<<" | "%" INT_WIDTH "d>>" ;
TYPED_INT_POWER     = "%d**" | "%" INT_WIDTH "d**" ;
TYPED_INT_BIT_AND   = "%d&" | "%" INT_WIDTH "d&" ;
TYPED_INT_BIT_OR    = "%d|" | "%" INT_WIDTH "d|" ;
TYPED_INT_BIT_XOR   = "%d^" | "%" INT_WIDTH "d^" ;
TYPED_FLOAT_ADD     = "%f+" | "%" FLOAT_WIDTH "f+" ;
TYPED_FLOAT_SUB     = "%f-" | "%" FLOAT_WIDTH "f-" ;
TYPED_FLOAT_MUL     = "%f*" | "%f/" | "%" FLOAT_WIDTH "f*" | "%" FLOAT_WIDTH "f/" ;
TYPED_FLOAT_POWER   = "%f**" | "%" FLOAT_WIDTH "f**" ;
```

The suffix-disambiguation rules for the Appendix EBNF are as follows: when the `IDENT` after `.` is followed by `(`, `method_suffix` MUST be selected; every other `.` suffix selects `member_suffix`. The lexical shape `TEMPLATE_COUNT_LITERAL` is reserved for `[tN]`/`[tK]` and MUST NOT be used by an ordinary identifier. Declaration-count semantics require `N > 0`; instance-access semantics permit `K = 0` and require `K` to be less than the instance count. At the lexical level, `INT_WIDTH` for integer typed operators accepts decimal digits, and at the semantic level its value is required to be greater than 0. At the lexical level, `FLOAT_WIDTH` for floating-point typed operators accepts decimal digits, and at the semantic level Section 8.3 restricts it to 2, 4, 8, or 16.

---

## 21. Appendix: Normative Standard-Template-Library Pseudocode

The standard-template-library pseudocode illustrates semantics. An implementation MAY provide equivalent behavior through built-ins. In this chapter, `read_*`, `write_*`, `format_*`, and `make_bool` are normative helper functions rather than core-syntax functions. The `op =` definitions below show only the primary same-template paths; every standard template MUST additionally expand its accepted overloads according to the default-assignment matrix in Section 11.10. In pseudocode for dynamic-length templates, `-> cstr` and `-> bytes` denote an rvalue copy having the length of the assignment target. These are standard-template-library pseudo-signatures rather than core-function return signatures.

```hs
template i32 { __size[4] as bytes }
impl i32 {
    op = (dst as i32, src as i32) -> i32 {
        write_i32(dst, read_integer(src))
        return dst
    }
    op +  (lhs as i32, rhs as i32) -> i32 { return wrap_i32(read_i32(lhs) + read_i32(rhs)) }
    op -  (lhs as i32, rhs as i32) -> i32 { return wrap_i32(read_i32(lhs) - read_i32(rhs)) }
    op *  (lhs as i32, rhs as i32) -> i32 { return wrap_i32(read_i32(lhs) * read_i32(rhs)) }
    op /  (lhs as i32, rhs as i32) -> i32 { check_nonzero(rhs); return trunc_div_i32(lhs, rhs) }
    op %  (lhs as i32, rhs as i32) -> i32 { check_nonzero(rhs); return rem_i32(lhs, rhs) }
    op ** (lhs as i32, rhs as i32) -> i32 { check_nonnegative(rhs); return pow_i32(lhs, rhs) }
    op << (lhs as i32, rhs as i32) -> i32 { check_shift(rhs); return wrap_i32(read_i32(lhs) << read_i32(rhs)) }
    op >> (lhs as i32, rhs as i32) -> i32 { check_shift(rhs); return arith_shift_right_i32(lhs, rhs) }
    op &  (lhs as i32, rhs as i32) -> i32 { return bit_i32(lhs, rhs, "and") }
    op |  (lhs as i32, rhs as i32) -> i32 { return bit_i32(lhs, rhs, "or") }
    op ^  (lhs as i32, rhs as i32) -> i32 { return bit_i32(lhs, rhs, "xor") }
    op == (lhs as i32, rhs as i32) -> bool { return make_bool(read_i32(lhs) == read_i32(rhs)) }
    op != (lhs as i32, rhs as i32) -> bool { return make_bool(read_i32(lhs) != read_i32(rhs)) }
    op <  (lhs as i32, rhs as i32) -> bool { return make_bool(read_i32(lhs) <  read_i32(rhs)) }
    op <= (lhs as i32, rhs as i32) -> bool { return make_bool(read_i32(lhs) <= read_i32(rhs)) }
    op >  (lhs as i32, rhs as i32) -> bool { return make_bool(read_i32(lhs) >  read_i32(rhs)) }
    op >= (lhs as i32, rhs as i32) -> bool { return make_bool(read_i32(lhs) >= read_i32(rhs)) }
    op format(value as i32, out[P] as addr) -> i32 { return format_i32(value, out) }
}

template f64 { __size[8] as bytes }
impl f64 {
    op = (dst as f64, src as f64) -> f64 {
        write_f64(dst, read_float(src))
        return dst
    }
    op +  (lhs as f64, rhs as f64) -> f64 { return ieee_add_f64(lhs, rhs) }
    op -  (lhs as f64, rhs as f64) -> f64 { return ieee_sub_f64(lhs, rhs) }
    op *  (lhs as f64, rhs as f64) -> f64 { return ieee_mul_f64(lhs, rhs) }
    op /  (lhs as f64, rhs as f64) -> f64 { return ieee_div_f64(lhs, rhs) }
    op ** (lhs as f64, rhs as f64) -> f64 { return ieee_pow_f64(lhs, rhs) }
    op == (lhs as f64, rhs as f64) -> bool { return make_bool(ieee_eq(lhs, rhs)) }
    op != (lhs as f64, rhs as f64) -> bool { return make_bool(ieee_ne(lhs, rhs)) }
    op <  (lhs as f64, rhs as f64) -> bool { return make_bool(ieee_lt(lhs, rhs)) }
    op <= (lhs as f64, rhs as f64) -> bool { return make_bool(ieee_le(lhs, rhs)) }
    op >  (lhs as f64, rhs as f64) -> bool { return make_bool(ieee_gt(lhs, rhs)) }
    op >= (lhs as f64, rhs as f64) -> bool { return make_bool(ieee_ge(lhs, rhs)) }
    op format(value as f64, out[P] as addr) -> i32 { return format_f64(value, out) }
}

template bool { __size[1] as bytes }
impl bool {
    op = (dst as bool, src as bool) -> bool {
        write_u8(dst, any_bit_set(src) ? 1 : 0)
        return dst
    }
    op == (lhs as bool, rhs as bool) -> bool { return make_bool(read_bool(lhs) == read_bool(rhs)) }
    op != (lhs as bool, rhs as bool) -> bool { return make_bool(read_bool(lhs) != read_bool(rhs)) }
    op format(value as bool, out[P] as addr) -> i32 { return format_bool(value, out) }
}

template addr { __size[P] as bytes }
impl addr {
    op = (dst as addr, src as addr) -> addr {
        copy_or_resize_to_P(dst, src)
        return dst
    }
    op == (lhs as addr, rhs as addr) -> bool { return make_bool(read_addr(lhs) == read_addr(rhs)) }
    op != (lhs as addr, rhs as addr) -> bool { return make_bool(read_addr(lhs) != read_addr(rhs)) }
    op format(value as addr, out[P] as addr) -> i32 { return format_addr(value, out) }
}

template handle { __size[P] as bytes }
impl handle {
    op = (dst as handle, src as handle) -> handle {
        copy_or_resize_to_P(dst, src)
        return dst
    }
    op == (lhs as handle, rhs as handle) -> bool { return make_bool(read_handle(lhs) == read_handle(rhs)) }
    op != (lhs as handle, rhs as handle) -> bool { return make_bool(read_handle(lhs) != read_handle(rhs)) }
    op format(value as handle, out[P] as addr) -> i32 { return format_handle(value, out) }
}

template cstr { __size[dynamic] as bytes }
impl cstr {
    op = (dst as cstr, src as cstr) -> cstr {
        copy_cstr_with_termination(dst, src)
        return dst
    }
    op == (lhs as cstr, rhs as cstr) -> bool { return make_bool(strcmp(lhs, rhs) == 0) }
    op != (lhs as cstr, rhs as cstr) -> bool { return make_bool(strcmp(lhs, rhs) != 0) }
    op <  (lhs as cstr, rhs as cstr) -> bool { return make_bool(strcmp(lhs, rhs) <  0) }
    op <= (lhs as cstr, rhs as cstr) -> bool { return make_bool(strcmp(lhs, rhs) <= 0) }
    op >  (lhs as cstr, rhs as cstr) -> bool { return make_bool(strcmp(lhs, rhs) >  0) }
    op >= (lhs as cstr, rhs as cstr) -> bool { return make_bool(strcmp(lhs, rhs) >= 0) }
    op format(value as cstr, out[P] as addr) -> i32 { return format_cstr(value, out) }
}

template bytes { __size[dynamic] as bytes }
impl bytes {
    op = (dst as bytes, src as bytes) -> bytes {
        require(length(dst) == length(src))
        copy_bytes(dst, src, length(dst))
        return dst
    }
    op == (lhs as bytes, rhs as bytes) -> bool { return make_bool(length(lhs) == length(rhs) && memcmp(lhs, rhs, length(lhs)) == 0) }
    op != (lhs as bytes, rhs as bytes) -> bool { return make_bool(!(lhs == rhs)) }
    op <  (lhs as bytes, rhs as bytes) -> bool { return make_bool(bytes_lexcmp(lhs, rhs) <  0) }
    op <= (lhs as bytes, rhs as bytes) -> bool { return make_bool(bytes_lexcmp(lhs, rhs) <= 0) }
    op >  (lhs as bytes, rhs as bytes) -> bool { return make_bool(bytes_lexcmp(lhs, rhs) >  0) }
    op >= (lhs as bytes, rhs as bytes) -> bool { return make_bool(bytes_lexcmp(lhs, rhs) >= 0) }
    op format(value as bytes, out[P] as addr) -> i32 { return format_bytes(value, out) }
}
```

`i8`, `i16`, `i64`, `u8`, `u16`, `u32`, `u64`, `f16`, `f32`, and `f128` are defined according to the rules of their respective template families. The pseudo-field `__size[dynamic]` in dynamic-length templates is illustrative only and is not core syntax.
