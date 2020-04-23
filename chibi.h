#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct Type Type;
typedef struct Member Member;

// tokenize.c

typedef enum {
  TK_RESERVED,
  TK_IDENT,
  TK_STR,
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

  char *contents;
  int cont_len;
};

void error(char *fmt, ...);
void error_at(char *loc, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
void warn_tok(Token *tok, char *fmt, ...);
Token *peek(char *s);
Token *consume(char *op);
Token *consume_ident(void);
void expect(char *op);
long expect_number(void);
char *expect_ident(void);
bool at_eof(void);
Token *tokenize(void);

extern char *filename;
extern char *user_input;
extern Token *token;

// parse.c

typedef struct Var Var;
struct Var {
  char *name;
  Type *ty;
  bool is_local;

  int offset;

  char *contents;
  int cont_len;
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
  ND_BITAND,
  ND_BITOR,
  ND_BITXOR,
  ND_EQ,
  ND_NE,
  ND_LT,
  ND_LE,
  ND_ASSIGN,
  ND_PRE_INC,
  ND_PRE_DEC,
  ND_POST_INC,
  ND_POST_DEC,
  ND_ADD_EQ,
  ND_PTR_ADD_EQ,
  ND_SUB_EQ,
  ND_PTR_SUB_EQ,
  ND_MUL_EQ,
  ND_DIV_EQ,
  ND_COMMA,
  ND_MEMBER,
  ND_ADDR,
  ND_DEREF,
  ND_NOT,
  ND_BITNOT,
  ND_LOGAND,
  ND_LOGOR,
  ND_RETURN,
  ND_IF,
  ND_WHILE,
  ND_FOR,
  ND_BLOCK,
  ND_BREAK,
  ND_CONTINUE,
  ND_GOTO,
  ND_LABEL,
  ND_FUNCALL,
  ND_EXPR_STMT,
  ND_STMT_EXPR,
  ND_VAR,
  ND_NUM,
  ND_CAST,
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

  Member *member;

  char *funcname;
  Node *args;

  char *label_name;

  Var *var;
  long val;
};

typedef struct Function Function;
struct Function {
  Function *next;
  char *name;
  VarList *params;
  bool is_static;

  Node *node;
  VarList *locals;
  int stack_size;
};

typedef struct {
  VarList *globals;
  Function *fns;
} Program;

Program *program(void);

// typing.c

typedef enum {
  TY_VOID,
  TY_BOOL,
  TY_CHAR,
  TY_SHORT,
  TY_INT,
  TY_LONG,
  TY_ENUM,
  TY_PTR,
  TY_ARRAY,
  TY_STRUCT,
  TY_FUNC,
} TypeKind;

struct Type {
  TypeKind kind;
  int size;
  int align;
  bool is_incomplete;

  Type *base;
  int array_len;
  Member *members;
  Type *return_ty;
};

struct  Member {
  Member *next;
  Type *ty;
  Token *tok;
  char *name;
  int offset;
};

extern Type *void_type;
extern Type *bool_type;
extern Type *char_type;
extern Type *short_type;
extern Type *int_type;
extern Type *long_type;

bool is_integer(Type *ty);
int align_to(int n, int align);
Type *pointer_to(Type *base);
Type *array_of(Type *base, int size);
Type *func_type(Type *return_ty);
Type *enum_type(void);
Type *struct_type(void);
void add_type(Node *node);

// codegen.c

void codegen(Program *prog);