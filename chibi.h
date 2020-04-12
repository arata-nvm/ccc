#define _GNU_SOURCE
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Type Type;

// tokenize.c

typedef enum {
  TK_RESERVED,
  TK_IDENT,
  TK_NUM,
  TK_EOF,
} TokenKind;

typedef struct Token Token;
struct Token {
  TokenKind kind;
  Token *next;
  int val;
  char *str;
  int len;
};

void error(char *fmt, ...);
void error_at(char *loc, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
Token *peek(char *s);
Token *consume(char *op);
Token *consume_ident(void);
void expect(char *op);
long expect_number(void);
char *expect_ident(void);
bool at_eof(void);
Token *tokenize(void);

extern char *user_input;
extern Token *token;

// parse.c

typedef struct Var Var;
struct Var {
  char *name;
  Type *ty;
  int offset;
};

typedef struct VarList VarList;
struct VarList {
  VarList *next;
  Var *var;
};

typedef enum {
  ND_ADD,
  ND_PTR_ADD,
  ND_SUB,
  ND_PTR_SUB,
  ND_PTR_DIFF,
  ND_MUL,
  ND_DIV,
  ND_EQ,
  ND_NE,
  ND_LT,
  ND_LE,
  ND_ASSIGN,
  ND_ADDR,
  ND_DEREF,
  ND_RETURN,
  ND_IF,
  ND_WHILE,
  ND_FOR,
  ND_BLOCK,
  ND_FUNCALL,
  ND_EXPR_STMT,
  ND_VAR,
  ND_NUM,
  ND_NULL,
} NodeKind;

typedef struct Node Node;
struct Node {
  NodeKind kind;
  Node *next;
  Type *ty;
  Token *tok;

  Node *lhs;
  Node *rhs;

  Node *cond;
  Node *then;
  Node *els;
  Node *init;
  Node *inc;

  Node *body;

  char *funcname;
  Node *args;

  Var *var;
  long val;
};

typedef struct Function Function;
struct Function {
  Function *next;
  char *name;
  VarList *params;

  Node *node;
  VarList *locals;
  int stack_size;
};

Function *program(void);

// typing.c

typedef enum {
  TY_INT,
  TY_PTR
} TypeKind;

struct Type {
  TypeKind kind;
  Type *base;
};

extern Type *int_type;

bool is_integer(Type *ty);
Type *pointer_to(Type *base);
void add_type(Node *node);

// codegen.c

void codegen(Function *prog);