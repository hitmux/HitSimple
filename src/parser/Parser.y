%language "c++"
%skeleton "lalr1.cc"
%require "3.8"
%defines
%define api.namespace {hitsimple::parser}
%define api.parser.class {BisonParser}
%define api.token.constructor
%define api.value.type variant
%define parse.error verbose
%locations

%code requires {
  #include "hitsimple/ast/AST.h"

  #include <memory>
  #include <string>
  #include <vector>

  namespace hitsimple::parser {
  class ParseContext;

  struct ImplItems {
    std::vector<hitsimple::ast::ImplOpDecl> ops;
    std::vector<std::unique_ptr<hitsimple::ast::FunctionDecl>> methods;
  };
  }
}

%parse-param { hitsimple::parser::ParseContext& parseContext }
%lex-param { hitsimple::parser::ParseContext& parseContext }

%code {
  #include "ParserContext.inc"

  namespace {

  hitsimple::diagnostic::SourceLocation sourceLocation(
      const hitsimple::parser::position &position) {
    return hitsimple::diagnostic::SourceLocation{
        position.filename != nullptr ? *position.filename : std::string{},
        static_cast<std::size_t>(position.line),
        static_cast<std::size_t>(position.column)};
  }

  hitsimple::diagnostic::SourceRange sourceRange(
      const hitsimple::parser::BisonParser::location_type &begin,
      const hitsimple::parser::BisonParser::location_type &end) {
    return hitsimple::diagnostic::SourceRange{
        sourceLocation(begin.begin), sourceLocation(end.end)};
  }

  template <typename NodeT>
  std::unique_ptr<NodeT> withSourceRange(
      std::unique_ptr<NodeT> node,
      const hitsimple::parser::BisonParser::location_type &location) {
    if (node) {
      auto range = sourceRange(location, location);
      if (hitsimple::diagnostic::hasRange(range)) {
        node->range = std::move(range);
      }
    }
    return node;
  }

  template <typename NodeT>
  std::unique_ptr<NodeT> withSourceRange(
      std::unique_ptr<NodeT> node,
      const hitsimple::parser::BisonParser::location_type &begin,
      const hitsimple::parser::BisonParser::location_type &end) {
    if (node) {
      auto range = sourceRange(begin, end);
      if (hitsimple::diagnostic::hasRange(range)) {
        node->range = std::move(range);
      }
    }
    return node;
  }

  std::unique_ptr<hitsimple::ast::Expr>
  normalizePostfixDerefIncrement(std::unique_ptr<hitsimple::ast::Expr> expression) {
    auto *deref = dynamic_cast<hitsimple::ast::DerefExpr *>(expression.get());
    if (deref == nullptr) {
      return expression;
    }

    auto *increment =
        dynamic_cast<hitsimple::ast::UnaryExpr *>(deref->address.get());
    if (increment == nullptr ||
        (increment->op != "post++" && increment->op != "post--")) {
      return expression;
    }

    auto derefExpression = std::unique_ptr<hitsimple::ast::DerefExpr>(
        static_cast<hitsimple::ast::DerefExpr *>(expression.release()));
    const auto normalizedRange = derefExpression->range;
    auto incrementExpression = std::unique_ptr<hitsimple::ast::UnaryExpr>(
        static_cast<hitsimple::ast::UnaryExpr *>(
            derefExpression->address.release()));
    auto normalizedDeref = std::make_unique<hitsimple::ast::DerefExpr>(
        std::move(derefExpression->length),
        std::move(incrementExpression->operand));
    normalizedDeref->range = derefExpression->range;
    auto normalized = std::make_unique<hitsimple::ast::UnaryExpr>(
        std::move(incrementExpression->op),
        std::move(normalizedDeref));
    normalized->range = normalizedRange;
    return normalized;
  }

  } // namespace
}

%token END 0 "end of file"
%token <std::string> INVALID "invalid token"
%token NEWLINE "newline"
%token <std::string> IDENTIFIER "identifier"
%token <std::string> INTEGER "integer"
%token <std::string> FLOAT "float"
%token <std::string> CHAR "char"
%token <std::string> STRING "string"
%token FUNC "func"
%token NEW "new"
%token STATIC "static"
%token EXTERN "extern"
%token RETURN "return"
%token IF "if"
%token ELSE "else"
%token FOR "for"
%token WHILE "while"
%token BREAK "break"
%token CONTINUE "continue"
%token GOTO "goto"
%token TRY "try"
%token CATCH "catch"
%token THROW "throw"
%token TRUE "true"
%token FALSE "false"
%token STRUCT "struct"
%token TEMPLATE "template"
%token IMPL "impl"
%token OP "op"
%token AS "as"
%token SELF "self"
%token MUT "mut"
%token SET "set"
%token NONE "none"
%token SIZEOF "sizeof"
%token <std::string> TYPED_ASSIGN_OPERATOR "typed assignment operator"
%token <std::string> TYPED_ADDITIVE_OPERATOR "typed additive operator"
%token <std::string> TYPED_MULTIPLICATIVE_OPERATOR "typed multiplicative operator"
%token <std::string> TYPED_SHIFT_OPERATOR "typed shift operator"
%token <std::string> TYPED_BITWISE_OPERATOR "typed bitwise operator"
%token <std::string> TYPED_POWER_OPERATOR "typed power operator"
%token EQUAL "="
%token PLUS "+"
%token MINUS "-"
%token STAR "*"
%token SLASH "/"
%token PERCENT "%"
%token POWER "**"
%token LESS "<"
%token GREATER ">"
%token LESS_EQUAL "<="
%token GREATER_EQUAL ">="
%token EQUAL_EQUAL "=="
%token BANG_EQUAL "!="
%token BANG "!"
%token AMPERSAND "&"
%token PIPE "|"
%token CARET "^"
%token TILDE "~"
%token AMPERSAND_AMPERSAND "&&"
%token PIPE_PIPE "||"
%token SHIFT_LEFT "<<"
%token SHIFT_RIGHT ">>"
%token AMPERSAND_EQUAL "&="
%token PLUS_PLUS "++"
%token MINUS_MINUS "--"
%token QUESTION "?"
%token UNSIGNED_QUESTION "unsigned ?"
%token ARROW "->"
%token DOT "."
%token LPAREN "("
%token RPAREN ")"
%token LBRACE "{"
%token RBRACE "}"
%token LBRACKET "["
%token RBRACKET "]"
%token COMMA ","
%token SEMICOLON ";"
%token TEMPLATE_MARK "template mark"
%token COLON ":"

%type <std::unique_ptr<hitsimple::ast::TranslationUnit>> translation_unit
%type <std::unique_ptr<hitsimple::ast::FunctionDecl>> function impl_method
%type <std::unique_ptr<hitsimple::ast::TopLevelDecl>> top_level_item top_level_decl global_new_decl extern_decl struct_decl template_decl impl_decl
%type <std::vector<std::unique_ptr<hitsimple::ast::TopLevelDecl>>> top_level_items
%type <hitsimple::ast::Param> param impl_method_param
%type <std::vector<hitsimple::ast::Param>> optional_param_list param_list optional_impl_method_param_list impl_method_param_list
%type <hitsimple::ast::ReturnItem> return_item
%type <std::vector<hitsimple::ast::ReturnItem>> optional_return_sig return_sig return_item_list
%type <hitsimple::ast::StructMember> struct_member
%type <std::vector<hitsimple::ast::StructMember>> struct_member_list
%type <hitsimple::ast::TemplateMember> template_member
%type <std::vector<hitsimple::ast::TemplateMember>> template_member_list
%type <hitsimple::ast::ImplOpParam> op_param
%type <std::unique_ptr<hitsimple::ast::ImplOpDecl>> op_decl
%type <hitsimple::parser::ImplItems> impl_item_list
%type <std::vector<hitsimple::ast::ImplOpParam>> optional_op_param_list op_param_list
%type <hitsimple::ast::Param> catch_param
%type <hitsimple::ast::DeclItem> decl_item
%type <hitsimple::ast::AssignmentTarget> assignment_target
%type <std::vector<hitsimple::ast::DeclItem>> decl_list batch_decl_list
%type <std::vector<hitsimple::ast::AssignmentTarget>> assignment_target_list
%type <std::string> optional_length_spec length_spec
%type <std::string> decl_storage assignment_operator template_name optional_template_mark optional_as_template overloadable_operator param_name template_param_name
%type <std::unique_ptr<hitsimple::ast::BlockStmt>> block
%type <std::vector<std::unique_ptr<hitsimple::ast::Stmt>>> statement_list
%type <std::unique_ptr<hitsimple::ast::Stmt>> statement var_decl assign_stmt expr_stmt return_stmt if_stmt while_stmt for_stmt break_stmt continue_stmt goto_stmt label_stmt throw_stmt try_catch_stmt set_stmt
%type <std::unique_ptr<hitsimple::ast::Stmt>> optional_for_init
%type <std::unique_ptr<hitsimple::ast::AssignmentExpr>> assignment_stmt_expr
%type <std::unique_ptr<hitsimple::ast::Expr>> optional_for_condition set_target
%type <std::unique_ptr<hitsimple::ast::Expr>> expression assignment_expr conditional_expr logical_or_expr logical_and_expr bitwise_or_expr bitwise_xor_expr bitwise_and_expr equality_expr relational_expr shift_expr additive_expr multiplicative_expr power_expr unary_expr postfix_expr call_expr primary_expr
%type <std::vector<std::unique_ptr<hitsimple::ast::Expr>>> optional_argument_list argument_list optional_for_post for_post_list assignment_value_list return_value_list return_parenthesized_multi

%start program

%%

program:
  translation_unit
    { parseContext.setUnit(std::move($1)); }
;

translation_unit:
  top_level_items
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::TranslationUnit>(std::move($1)),
          @$);
    }
;

top_level_items:
  %empty
    { $$ = std::vector<std::unique_ptr<hitsimple::ast::TopLevelDecl>>(); }
| top_level_items top_level_item
    {
      $$ = std::move($1);
      if ($2) {
        $$.push_back(std::move($2));
      }
    }
;

top_level_item:
  function
    { $$ = std::move($1); }
| top_level_decl top_level_separator
    { $$ = std::move($1); }
| top_level_separator
    { $$ = nullptr; }
;

top_level_decl:
  global_new_decl
    { $$ = std::move($1); }
| extern_decl
    { $$ = std::move($1); }
| struct_decl
    { $$ = std::move($1); }
| template_decl
    { $$ = std::move($1); }
| impl_decl
    { $$ = std::move($1); }
;

global_new_decl:
  NEW decl_item
    {
      auto item = std::move($2);
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::GlobalNewDecl>(
              std::move(item.name), std::move(item.length),
              std::move(item.templateName), std::move(item.assignmentOp),
              std::move(item.initializer)),
          @$);
    }
;

extern_decl:
  EXTERN IDENTIFIER optional_length_spec optional_as_template
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::ExternVarDecl>(
              std::move($2), std::move($3), std::move($4)),
          @$);
    }
| EXTERN IDENTIFIER LPAREN optional_param_list RPAREN return_sig
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::ExternFunctionDecl>(
              std::move($2), std::move($4), std::move($6)),
          @$);
    }
;

struct_decl:
  STRUCT IDENTIFIER LBRACE struct_member_list RBRACE
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::StructDecl>(
              std::move($2), std::move($4)),
          @$);
    }
;

struct_member_list:
  %empty
    { $$ = std::vector<hitsimple::ast::StructMember>(); }
| struct_member_list struct_member statement_separator
    {
      $$ = std::move($1);
      $$.push_back(std::move($2));
    }
| struct_member_list statement_separator
    { $$ = std::move($1); }
;

struct_member:
  IDENTIFIER length_spec
    { $$ = hitsimple::ast::StructMember(std::move($1), std::move($2)); }
;

template_decl:
  TEMPLATE IDENTIFIER LBRACE template_member_list RBRACE
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::TemplateDecl>(
              std::move($2), std::move($4)),
          @$);
    }
;

template_member_list:
  %empty
    { $$ = std::vector<hitsimple::ast::TemplateMember>(); }
| template_member_list template_member statement_separator
    {
      $$ = std::move($1);
      $$.push_back(std::move($2));
    }
| template_member_list statement_separator
    { $$ = std::move($1); }
;

template_member:
  IDENTIFIER length_spec optional_as_template
    {
      $$ = hitsimple::ast::TemplateMember(
          std::move($1), std::move($2), std::move($3));
    }
;

impl_decl:
  IMPL IDENTIFIER LBRACE impl_item_list RBRACE
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::ImplDecl>(
              std::move($2), std::move($4.ops), std::move($4.methods)),
          @$);
    }
;

impl_item_list:
  %empty
    { $$ = hitsimple::parser::ImplItems(); }
| impl_item_list op_decl
    {
      $$ = std::move($1);
      $$.ops.push_back(std::move(*$2));
    }
| impl_item_list impl_method
    {
      $$ = std::move($1);
      $$.methods.push_back(std::move($2));
    }
| impl_item_list statement_separator
    { $$ = std::move($1); }
;

op_decl:
  OP overloadable_operator LPAREN optional_op_param_list RPAREN optional_return_sig block
    {
      $$ = std::make_unique<hitsimple::ast::ImplOpDecl>(
          std::move($2), std::move($4), std::move($6), std::move($7));
    }
;

overloadable_operator:
  EQUAL
    { $$ = "="; }
| PLUS
    { $$ = "+"; }
| MINUS
    { $$ = "-"; }
| STAR
    { $$ = "*"; }
| SLASH
    { $$ = "/"; }
| PERCENT
    { $$ = "%"; }
| POWER
    { $$ = "**"; }
| LESS
    { $$ = "<"; }
| GREATER
    { $$ = ">"; }
| LESS_EQUAL
    { $$ = "<="; }
| GREATER_EQUAL
    { $$ = ">="; }
| EQUAL_EQUAL
    { $$ = "=="; }
| BANG_EQUAL
    { $$ = "!="; }
| SHIFT_LEFT
    { $$ = "<<"; }
| SHIFT_RIGHT
    { $$ = ">>"; }
| AMPERSAND
    { $$ = "&"; }
| PIPE
    { $$ = "|"; }
| CARET
    { $$ = "^"; }
| IDENTIFIER
    { $$ = std::move($1); }
;

optional_op_param_list:
  %empty
    { $$ = std::vector<hitsimple::ast::ImplOpParam>(); }
| op_param_list
    { $$ = std::move($1); }
;

op_param_list:
  op_param
    {
      $$.push_back(std::move($1));
    }
| op_param_list COMMA op_param
    {
      $$ = std::move($1);
      $$.push_back(std::move($3));
    }
;

op_param:
  template_param_name AS template_name
    { $$ = hitsimple::ast::ImplOpParam(std::move($1), std::move($3), false); }
| MUT template_param_name AS template_name
    { $$ = hitsimple::ast::ImplOpParam(std::move($2), std::move($4), true); }
;

template_param_name:
  IDENTIFIER
    { $$ = std::move($1); }
| SELF
    { $$ = "self"; }
;

function:
  FUNC IDENTIFIER LPAREN optional_param_list RPAREN optional_return_sig block
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::FunctionDecl>(
              std::move($2), std::move($4), std::move($6), std::move($7)),
          @$);
    }
;

impl_method:
  FUNC IDENTIFIER LPAREN optional_impl_method_param_list RPAREN optional_return_sig block
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::FunctionDecl>(
              std::move($2), std::move($4), std::move($6), std::move($7)),
          @$);
    }
;

optional_param_list:
  %empty
    { $$ = std::vector<hitsimple::ast::Param>(); }
| param_list
    { $$ = std::move($1); }
;

param_list:
  param
    {
      $$.push_back(std::move($1));
    }
| param_list COMMA param
    {
      $$ = std::move($1);
      $$.push_back(std::move($3));
    }
;

param:
  param_name optional_length_spec optional_as_template
    {
      $$ = hitsimple::ast::Param(
          std::move($1), std::move($2), std::move($3));
    }
;

optional_impl_method_param_list:
  %empty
    { $$ = std::vector<hitsimple::ast::Param>(); }
| impl_method_param_list
    { $$ = std::move($1); }
;

impl_method_param_list:
  impl_method_param
    {
      $$.push_back(std::move($1));
    }
| impl_method_param_list COMMA impl_method_param
    {
      $$ = std::move($1);
      $$.push_back(std::move($3));
    }
;

impl_method_param:
  param
    { $$ = std::move($1); }
| MUT param_name optional_length_spec optional_as_template
    {
      $$ = hitsimple::ast::Param(
          std::move($2), std::move($3), std::move($4), true);
    }
;

param_name:
  IDENTIFIER
    { $$ = std::move($1); }
| SELF
    { $$ = "self"; }
;

optional_length_spec:
  %empty
    { $$ = ""; }
| length_spec
    { $$ = std::move($1); }
;

length_spec:
  LBRACKET INTEGER RBRACKET
    { $$ = std::move($2); }
| LBRACKET IDENTIFIER RBRACKET
    { $$ = std::move($2); }
| LBRACKET IDENTIFIER INTEGER RBRACKET
    { $$ = std::move($2) + std::move($3); }
;

optional_return_sig:
  %empty
    { $$ = std::vector<hitsimple::ast::ReturnItem>(); }
| return_sig
    { $$ = std::move($1); }
;

return_sig:
  ARROW LPAREN RPAREN
    { $$ = std::vector<hitsimple::ast::ReturnItem>(); }
| ARROW return_item
    {
      $$.push_back(std::move($2));
    }
| ARROW LPAREN return_item_list RPAREN
    { $$ = std::move($3); }
;

return_item_list:
  return_item
    {
      $$.push_back(std::move($1));
    }
| return_item_list COMMA return_item
    {
      $$ = std::move($1);
      $$.push_back(std::move($3));
    }
;

return_item:
  length_spec
    { $$ = hitsimple::ast::ReturnItem("", std::move($1), ""); }
| length_spec AS template_name
    {
      $$ = hitsimple::ast::ReturnItem(
          "", std::move($1), std::move($3));
    }
| AS template_name
    { $$ = hitsimple::ast::ReturnItem("", "", std::move($2)); }
| IDENTIFIER
    { $$ = hitsimple::ast::ReturnItem("", "", std::move($1)); }
| IDENTIFIER length_spec
    { $$ = hitsimple::ast::ReturnItem(std::move($1), std::move($2), ""); }
| IDENTIFIER optional_length_spec AS template_name
    {
      $$ = hitsimple::ast::ReturnItem(
          std::move($1), std::move($2), std::move($4));
    }
;

block:
  LBRACE statement_list RBRACE
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BlockStmt>(std::move($2)), @$);
    }
;

statement_list:
  %empty
    { $$ = std::vector<std::unique_ptr<hitsimple::ast::Stmt>>(); }
| statement_list statement statement_separator
    {
      $$ = std::move($1);
      $$.push_back(std::move($2));
    }
| statement_list statement_separator
    { $$ = std::move($1); }
;

statement:
  var_decl
    { $$ = std::move($1); }
| assign_stmt
    { $$ = std::move($1); }
| expr_stmt
    { $$ = std::move($1); }
| return_stmt
    { $$ = std::move($1); }
| if_stmt
    { $$ = std::move($1); }
| while_stmt
    { $$ = std::move($1); }
| for_stmt
    { $$ = std::move($1); }
| break_stmt
    { $$ = std::move($1); }
| continue_stmt
    { $$ = std::move($1); }
| goto_stmt
    { $$ = std::move($1); }
| label_stmt
    { $$ = std::move($1); }
| throw_stmt
    { $$ = std::move($1); }
| try_catch_stmt
    { $$ = std::move($1); }
| set_stmt
    { $$ = std::move($1); }
;

var_decl:
  decl_storage decl_list
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::VarDeclStmt>(
              std::move($1), std::move($2)),
          @$);
    }
| decl_storage LBRACE optional_newlines batch_decl_list optional_newlines RBRACE
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::VarDeclStmt>(
              std::move($1), std::move($4)),
          @$);
    }
;

decl_storage:
  NEW
    { $$ = "new"; }
| STATIC
    { $$ = "static"; }
;

decl_list:
  decl_item
    {
      $$.push_back(std::move($1));
    }
| decl_list COMMA decl_item
    {
      $$ = std::move($1);
      $$.push_back(std::move($3));
    }
;

batch_decl_list:
  decl_item
    {
      $$.push_back(std::move($1));
    }
| batch_decl_list COMMA optional_newlines decl_item
    {
      $$ = std::move($1);
      $$.push_back(std::move($4));
    }
;

decl_item:
  IDENTIFIER optional_length_spec optional_template_mark
    {
      $$ = hitsimple::ast::DeclItem(
          std::move($1), std::move($2), "", nullptr, std::move($3));
    }
| IDENTIFIER optional_length_spec AS template_name
    {
      $$ = hitsimple::ast::DeclItem(
          std::move($1), std::move($2), "", nullptr, std::move($4));
    }
| IDENTIFIER optional_length_spec assignment_operator expression optional_template_mark
    {
      $$ = hitsimple::ast::DeclItem(
          std::move($1), std::move($2), std::move($3), std::move($4),
          std::move($5));
    }
| IDENTIFIER optional_length_spec AS template_name assignment_operator expression
    {
      $$ = hitsimple::ast::DeclItem(
          std::move($1), std::move($2), std::move($5), std::move($6),
          std::move($4));
    }
;

optional_template_mark:
  %empty
    { $$ = ""; }
| TEMPLATE_MARK template_name
    { $$ = std::move($2); }
;

optional_as_template:
  %empty
    { $$ = ""; }
| AS template_name
    { $$ = std::move($2); }
;

template_name:
  IDENTIFIER
    { $$ = std::move($1); }
| NONE
    { $$ = "none"; }
;

assignment_operator:
  TYPED_ASSIGN_OPERATOR
    { $$ = std::move($1); }
| EQUAL
    { $$ = "="; }
| AMPERSAND_EQUAL
    { $$ = "&="; }
;

assign_stmt:
  assignment_stmt_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::AssignStmt>(std::move($1)), @$);
    }
;

assignment_stmt_expr:
  assignment_target_list EQUAL assignment_value_list
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::AssignmentExpr>(
              std::move($1), std::move($3)),
          @$);
    }
;

expr_stmt:
  expression
    {
      if (auto *assignment =
              dynamic_cast<hitsimple::ast::AssignmentExpr*>($1.get())) {
        (void)assignment;
        auto assignmentExpr =
            std::unique_ptr<hitsimple::ast::AssignmentExpr>(
                static_cast<hitsimple::ast::AssignmentExpr*>($1.release()));
        $$ = withSourceRange(
            std::make_unique<hitsimple::ast::AssignStmt>(
                std::move(assignmentExpr)),
            @$);
      } else {
        $$ = withSourceRange(
            std::make_unique<hitsimple::ast::ExprStmt>(
                normalizePostfixDerefIncrement(std::move($1))),
            @$);
      }
    }
;

return_stmt:
  RETURN
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::ReturnStmt>(
              std::vector<std::unique_ptr<hitsimple::ast::Expr>>()),
          @$);
    }
| RETURN return_value_list
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::ReturnStmt>(std::move($2)), @$);
    }
| RETURN LPAREN return_parenthesized_multi RPAREN
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::ReturnStmt>(std::move($3)), @$);
    }
;

return_value_list:
  expression
    {
      $$.push_back(std::move($1));
    }
| return_value_list COMMA expression
    {
      $$ = std::move($1);
      $$.push_back(std::move($3));
    }
;

return_parenthesized_multi:
  expression COMMA expression
    {
      $$.push_back(std::move($1));
      $$.push_back(std::move($3));
    }
| return_parenthesized_multi COMMA expression
    {
      $$ = std::move($1);
      $$.push_back(std::move($3));
    }
;

if_stmt:
  IF LPAREN expression RPAREN block
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::IfStmt>(
              std::move($3), std::move($5), nullptr),
          @$);
    }
| IF LPAREN expression RPAREN block ELSE block
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::IfStmt>(
              std::move($3), std::move($5), std::move($7)),
          @$);
    }
| IF LPAREN expression RPAREN block ELSE if_stmt
    {
      std::vector<std::unique_ptr<hitsimple::ast::Stmt>> statements;
      statements.push_back(std::move($7));
      auto elseBlock = withSourceRange(
          std::make_unique<hitsimple::ast::BlockStmt>(std::move(statements)),
          @7);
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::IfStmt>(
              std::move($3), std::move($5), std::move(elseBlock)),
          @$);
    }
;

while_stmt:
  WHILE LPAREN expression RPAREN block
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::WhileStmt>(
              std::move($3), std::move($5)),
          @$);
    }
;

for_stmt:
  FOR LPAREN optional_for_init SEMICOLON optional_for_condition SEMICOLON optional_for_post RPAREN block
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::ForStmt>(
              std::move($3), std::move($5), std::move($7), std::move($9)),
          @$);
    }
;

optional_for_init:
  %empty
    { $$ = nullptr; }
| var_decl
    { $$ = std::move($1); }
| assign_stmt
    { $$ = std::move($1); }
| expr_stmt
    { $$ = std::move($1); }
;

optional_for_condition:
  %empty
    { $$ = nullptr; }
| expression
    { $$ = std::move($1); }
;

optional_for_post:
  %empty
    { $$ = std::vector<std::unique_ptr<hitsimple::ast::Expr>>(); }
| for_post_list
    { $$ = std::move($1); }
;

for_post_list:
  expression
    {
      $$.push_back(normalizePostfixDerefIncrement(std::move($1)));
    }
| for_post_list COMMA expression
    {
      $$ = std::move($1);
      $$.push_back(normalizePostfixDerefIncrement(std::move($3)));
    }
;

break_stmt:
  BREAK
    {
      $$ = withSourceRange(std::make_unique<hitsimple::ast::BreakStmt>(), @$);
    }
;

continue_stmt:
  CONTINUE
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::ContinueStmt>(), @$);
    }
;

goto_stmt:
  GOTO IDENTIFIER
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::GotoStmt>(std::move($2)), @$);
    }
;

label_stmt:
  IDENTIFIER COLON statement
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::LabelStmt>(
              std::move($1), std::move($3)),
          @$);
    }
| IDENTIFIER COLON
    {
      auto empty = withSourceRange(
          std::make_unique<hitsimple::ast::EmptyStmt>(), @2);
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::LabelStmt>(
              std::move($1), std::move(empty)),
          @$);
    }
;

throw_stmt:
  THROW expression
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::ThrowStmt>(std::move($2)), @$);
    }
;

try_catch_stmt:
  TRY block CATCH LPAREN catch_param RPAREN block
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::TryCatchStmt>(
              std::move($2), std::move($5.name), std::move($5.length),
              std::move($5.templateName), std::move($7)),
          @$);
    }
;

catch_param:
  IDENTIFIER optional_length_spec optional_as_template
    {
      $$ = hitsimple::ast::Param(
          std::move($1), std::move($2), std::move($3));
    }
;

set_stmt:
  SET set_target TEMPLATE_MARK template_name
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::SetStmt>(
              std::move($2), std::move($4)),
          @$);
    }
| SET set_target AS template_name
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::SetStmt>(
              std::move($2), std::move($4)),
          @$);
    }
;

set_target:
  IDENTIFIER
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::IdentifierExpr>(std::move($1)),
          @$);
    }
| set_target DOT IDENTIFIER
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::MemberExpr>(
              std::move($1), std::move($3)),
          @$);
    }
;

expression:
  assignment_expr
    { $$ = std::move($1); }
;

assignment_expr:
  conditional_expr
    { $$ = std::move($1); }
| postfix_expr assignment_operator assignment_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::AssignmentExpr>(
              std::move($1), std::move($2), std::move($3)),
          @$);
    }
| LBRACKET INTEGER RBRACKET STAR unary_expr assignment_operator assignment_expr
    {
      auto target = withSourceRange(
          std::make_unique<hitsimple::ast::DerefExpr>(
              std::move($2), std::move($5)),
          @1, @5);
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::AssignmentExpr>(
              std::move(target), std::move($6), std::move($7)),
          @$);
    }
;

assignment_target:
  postfix_expr
    {
      $$ = hitsimple::ast::AssignmentTarget(std::move($1), "=");
    }
| LBRACKET INTEGER RBRACKET STAR unary_expr
    {
      $$ = hitsimple::ast::AssignmentTarget(
          withSourceRange(
              std::make_unique<hitsimple::ast::DerefExpr>(
                  std::move($2), std::move($5)),
              @1, @5),
          "=");
    }
| LPAREN postfix_expr assignment_operator RPAREN
    {
      $$ = hitsimple::ast::AssignmentTarget(std::move($2), std::move($3));
    }
| LPAREN LBRACKET INTEGER RBRACKET STAR unary_expr assignment_operator RPAREN
    {
      $$ = hitsimple::ast::AssignmentTarget(
          withSourceRange(
              std::make_unique<hitsimple::ast::DerefExpr>(
                  std::move($3), std::move($6)),
              @2, @6),
          std::move($7));
    }
;

assignment_target_list:
  assignment_target COMMA assignment_target
    {
      $$.push_back(std::move($1));
      $$.push_back(std::move($3));
    }
| assignment_target_list COMMA assignment_target
    {
      $$ = std::move($1);
      $$.push_back(std::move($3));
    }
;

assignment_value_list:
  assignment_expr
    {
      $$.push_back(std::move($1));
    }
| assignment_value_list COMMA assignment_expr
    {
      $$ = std::move($1);
      $$.push_back(std::move($3));
    }
;

conditional_expr:
  logical_or_expr
    { $$ = std::move($1); }
| logical_or_expr QUESTION expression COLON conditional_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::TernaryExpr>(
              std::move($1), std::move($3), std::move($5)),
          @$);
    }
;

logical_or_expr:
  logical_and_expr
    { $$ = std::move($1); }
| logical_or_expr PIPE_PIPE logical_and_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), "||", std::move($3)),
          @$);
    }
;

logical_and_expr:
  bitwise_or_expr
    { $$ = std::move($1); }
| logical_and_expr AMPERSAND_AMPERSAND bitwise_or_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), "&&", std::move($3)),
          @$);
    }
;

bitwise_or_expr:
  bitwise_xor_expr
    { $$ = std::move($1); }
| bitwise_or_expr PIPE bitwise_xor_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), "|", std::move($3)),
          @$);
    }
| bitwise_or_expr TYPED_BITWISE_OPERATOR bitwise_xor_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), std::move($2), std::move($3)),
          @$);
    }
;

bitwise_xor_expr:
  bitwise_and_expr
    { $$ = std::move($1); }
| bitwise_xor_expr CARET bitwise_and_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), "^", std::move($3)),
          @$);
    }
;

bitwise_and_expr:
  equality_expr
    { $$ = std::move($1); }
| bitwise_and_expr AMPERSAND equality_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), "&", std::move($3)),
          @$);
    }
;

equality_expr:
  relational_expr
    { $$ = std::move($1); }
| equality_expr EQUAL_EQUAL relational_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), "==", std::move($3)),
          @$);
    }
| equality_expr BANG_EQUAL relational_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), "!=", std::move($3)),
          @$);
    }
;

relational_expr:
  shift_expr
    { $$ = std::move($1); }
| relational_expr LESS shift_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), "<", std::move($3)),
          @$);
    }
| relational_expr GREATER shift_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), ">", std::move($3)),
          @$);
    }
| relational_expr LESS_EQUAL shift_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), "<=", std::move($3)),
          @$);
    }
| relational_expr GREATER_EQUAL shift_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), ">=", std::move($3)),
          @$);
    }
;

shift_expr:
  additive_expr
    { $$ = std::move($1); }
| shift_expr SHIFT_LEFT additive_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), "<<", std::move($3)),
          @$);
    }
| shift_expr SHIFT_RIGHT additive_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), ">>", std::move($3)),
          @$);
    }
| shift_expr TYPED_SHIFT_OPERATOR additive_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), std::move($2), std::move($3)),
          @$);
    }
;

additive_expr:
  multiplicative_expr
    { $$ = std::move($1); }
| additive_expr TYPED_ADDITIVE_OPERATOR multiplicative_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), std::move($2), std::move($3)),
          @$);
    }
| additive_expr PLUS multiplicative_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), "+", std::move($3)),
          @$);
    }
| additive_expr MINUS multiplicative_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), "-", std::move($3)),
          @$);
    }
;

multiplicative_expr:
  power_expr
    { $$ = std::move($1); }
| multiplicative_expr TYPED_MULTIPLICATIVE_OPERATOR power_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), std::move($2), std::move($3)),
          @$);
    }
| multiplicative_expr STAR power_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), "*", std::move($3)),
          @$);
    }
| multiplicative_expr SLASH power_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), "/", std::move($3)),
          @$);
    }
| multiplicative_expr PERCENT power_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), "%", std::move($3)),
          @$);
    }
;

power_expr:
  unary_expr
    { $$ = std::move($1); }
| unary_expr POWER power_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), "**", std::move($3)),
          @$);
    }
| unary_expr TYPED_POWER_OPERATOR power_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BinaryExpr>(
              std::move($1), std::move($2), std::move($3)),
          @$);
    }
;

unary_expr:
  postfix_expr
    { $$ = std::move($1); }
| BANG unary_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::UnaryExpr>("!", std::move($2)),
          @$);
    }
| TILDE unary_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::UnaryExpr>("~", std::move($2)),
          @$);
    }
| MINUS unary_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::UnaryExpr>("-", std::move($2)),
          @$);
    }
| AMPERSAND unary_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::UnaryExpr>("&", std::move($2)),
          @$);
    }
| PLUS_PLUS unary_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::UnaryExpr>("++", std::move($2)),
          @$);
    }
| MINUS_MINUS unary_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::UnaryExpr>("--", std::move($2)),
          @$);
    }
| LBRACKET INTEGER RBRACKET STAR unary_expr
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::DerefExpr>(
              std::move($2), std::move($5)),
          @$);
    }
| SIZEOF LPAREN IDENTIFIER RPAREN
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::SizeofExpr>(std::move($3)), @$);
    }
;

postfix_expr:
  call_expr
    { $$ = std::move($1); }
| primary_expr
    { $$ = std::move($1); }
| postfix_expr LBRACKET expression RBRACKET
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::IndexExpr>(
              std::move($1), std::move($3)),
          @$);
    }
| postfix_expr LBRACKET expression COLON expression RBRACKET
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::SliceExpr>(
              std::move($1), std::move($3), std::move($5), false),
          @$);
    }
| postfix_expr LBRACKET expression COLON PLUS expression RBRACKET
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::SliceExpr>(
              std::move($1), std::move($3), std::move($6), true),
          @$);
    }
| postfix_expr DOT IDENTIFIER
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::MemberExpr>(
              std::move($1), std::move($3)),
          @$);
    }
| postfix_expr DOT IDENTIFIER LPAREN optional_argument_list RPAREN
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::MethodCallExpr>(
              std::move($1), std::move($3), std::move($5)),
          @$);
    }
| postfix_expr UNSIGNED_QUESTION
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::UnsignedExpr>(std::move($1)), @$);
    }
| postfix_expr AS template_name
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::AsExpr>(
              std::move($1), std::move($3)),
          @$);
    }
| postfix_expr PLUS_PLUS
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::UnaryExpr>(
              "post++", std::move($1)),
          @$);
    }
| postfix_expr MINUS_MINUS
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::UnaryExpr>(
              "post--", std::move($1)),
          @$);
    }
;

call_expr:
  IDENTIFIER LPAREN optional_argument_list RPAREN
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::CallExpr>(
              std::move($1), std::move($3)),
          @$);
    }
;

optional_argument_list:
  %empty
    { $$ = std::vector<std::unique_ptr<hitsimple::ast::Expr>>(); }
| argument_list
    { $$ = std::move($1); }
;

argument_list:
  expression
    {
      $$.push_back(std::move($1));
    }
| argument_list COMMA expression
    {
      $$ = std::move($1);
      $$.push_back(std::move($3));
    }
;

primary_expr:
  IDENTIFIER
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::IdentifierExpr>(std::move($1)),
          @$);
    }
| INTEGER
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::IntegerLiteral>(std::move($1)),
          @$);
    }
| FLOAT
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::FloatLiteral>(std::move($1)), @$);
    }
| CHAR
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::CharLiteral>(std::move($1)), @$);
    }
| STRING
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::StringLiteral>(std::move($1)),
          @$);
    }
| SELF
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::IdentifierExpr>("self"), @$);
    }
| TRUE
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BoolLiteral>(true), @$);
    }
| FALSE
    {
      $$ = withSourceRange(
          std::make_unique<hitsimple::ast::BoolLiteral>(false), @$);
    }
| LPAREN expression RPAREN
    { $$ = std::move($2); }
;

statement_separator:
  NEWLINE
| SEMICOLON
;

top_level_separator:
  NEWLINE
| SEMICOLON
;

optional_newlines:
  %empty
| newlines
;

newlines:
  NEWLINE
| newlines NEWLINE
;

%%

void hitsimple::parser::BisonParser::error(
    const location_type& location, const std::string& message) {
  parseContext.setError(location, message);
}
