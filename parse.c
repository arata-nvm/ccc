#include "chibi.h"

typedef struct VarScope VarScope;
struct VarScope {
  VarScope *next;
  char *name;

  Var *var;
  Type *type_def;
  Type *enum_ty;
  int enum_val;
};

typedef struct TagScope TagScope;
struct TagScope {
  TagScope *next;
  char *name;
  Type *ty;
};

typedef struct {
  VarScope *var_scope;
  TagScope *tag_scope;
} Scope;

static VarList *locals;

static VarList *globals;

static VarScope *var_scope;
static TagScope *tag_scope;

static Scope *enter_scope(void) {
  Scope *sc = calloc(1, sizeof(Scope));
  sc->var_scope = var_scope;
  sc->tag_scope = tag_scope;
  return sc;
}

static void leave_scope(Scope *sc) {
  var_scope = sc->var_scope;
  tag_scope = sc->tag_scope;
}

static VarScope *find_var(Token *tok) {
  for (VarScope *sc = var_scope; sc; sc = sc->next) {
    if (strlen(sc->name) == tok->len && !strncmp(tok->str, sc->name, tok->len))
      return sc;
  }
  return NULL;
}

static TagScope *find_tag(Token *tok) {
  for (TagScope *sc = tag_scope; sc; sc = sc->next)
    if (strlen(sc->name) == tok->len && !strncmp(tok->str, sc->name, tok->len))
      return sc;
  return NULL;
}

static Node *new_node(NodeKind kind, Token *tok) {
  Node *node = calloc(1, sizeof(Node));
  node->kind = kind;
  node->tok = tok;
  return node;
}

static Node *new_binary(NodeKind kind, Node *lhs, Node *rhs, Token *tok) {
  Node *node = new_node(kind, tok);
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

static Node *new_unary(NodeKind kind, Node *expr, Token *tok) {
  Node *node = new_node(kind, tok);
  node->lhs = expr;
  return node;
}

static Node *new_num(long val, Token *tok) {
  Node *node = new_node(ND_NUM, tok);
  node->val = val;
  return node;
}

static Node *new_var_node(Var *var, Token *tok) {
  Node *node = new_node(ND_VAR, tok);
  node->var = var;
  return node;
}

static VarScope *push_scope(char *name) {
  VarScope *sc = calloc(1, sizeof(VarScope));
  sc->name = name;
  sc->next = var_scope;
  var_scope = sc;
  return sc;
}

static Var *new_var(char *name, Type *ty, bool is_local) {
  Var *var = calloc(1, sizeof(Var));
  var->name = name;
  var->ty = ty;
  var->is_local = is_local;
  return var;
}

static Var *new_lvar(char *name, Type *ty) {
  Var *var = new_var(name, ty, true);
  push_scope(name)->var = var;

  VarList *vl = calloc(1, sizeof(VarList));
  vl->var = var;
  vl->next = locals;
  locals = vl;
  return var;
}

static Var *new_gvar(char *name, Type *ty, bool emit) {
  Var *var = new_var(name, ty, false);
  push_scope(name)->var = var;

  if (emit) {
    VarList *vl = calloc(1, sizeof(VarList));
    vl->var = var;
    vl->next = globals;
    globals = vl;
  }
  return var;
}

static Type *find_typedef(Token *tok) {
  if (tok->kind == TK_IDENT) {
    VarScope *sc = find_var(tok);
    if (sc)
      return sc->type_def;
  }
  return NULL;
}

static char *new_label(void) {
  static int cnt = 0;
  char buf[20];
  sprintf(buf, ".L.data.%d", cnt++);
  return strndup(buf, 20);
}

typedef enum {
  TYPEDEF = 1 << 0,
  STATIC = 1 << 1,
} StorageClass;

static Function *function(void);
static Type *basetype(StorageClass *sclass);
static Type *declarator(Type *ty, char **name);
static Type *abstract_declarator(Type *ty);
static Type *type_suffix(Type *ty);
static Type *type_name(void);
static Type *struct_decl(void);
static Type *enum_specifier(void);
static Member *struct_member(void);
static void global_var(void);
static Node *declaration(void);
static bool is_typename(void);
static Node *stmt(void);
static Node *stmt2(void);
static Node *expr(void);
static Node *assign(void);
static Node *equality(void);
static Node *relational(void);
static Node *add(void);
static Node *mul(void);
static Node *cast(void);
static Node *unary(void);
static Node *postfix(void);
static Node *primary(void);

static bool is_function(void) {
  Token *tok = token;

  StorageClass sclass;
  Type *ty = basetype(&sclass);
  char *name = NULL;
  declarator(ty, &name);
  bool isfunc = name && consume("(");

  token = tok;
  return isfunc;
}

Program *program(void) {
  Function head = {};
  Function *cur = &head;
  globals = NULL;

  while (!at_eof()) {
    if (is_function()) {
      Function *fn = function();
      if (!fn)
        continue;
      cur->next = fn;
      cur = cur->next;
      continue;
    }
    global_var();
  }

  Program *prog = calloc(1, sizeof(Program));
  prog->globals = globals;
  prog->fns = head.next;
  return prog;
}

static Type *basetype(StorageClass *sclass) {
  if (!is_typename())
    error_tok(token, "typename expected");

  enum {
    VOID = 1 << 0,
    BOOL = 1 << 2,
    CHAR = 1 << 4,
    SHORT = 1 << 6,
    INT = 1 << 8,
    LONG = 1 << 10,
    OTHER = 1 << 12,
  };

  Type *ty = int_type;
  int counter = 0;

  if (sclass)
    *sclass = 0;

  while (is_typename()) {
    Token *tok = token;

    if (peek("typedef") || peek("static")) {
      if (!sclass)
        error_tok(tok, "storage class specifier is not allowed");

      if (consume("typedef"))
        *sclass |= TYPEDEF;
      else if (consume("static"))
        *sclass |= STATIC;

      if (*sclass & (*sclass - 1))
        error_tok(tok, "typedef and static may not be used together");
      continue;
    }

    if (!peek("void") && !peek("_Bool") && !peek("char") && !peek("short") &&
        !peek("int") && !peek("long")) {
      if (counter)
        break;

      if (peek("struct")) {
        ty = struct_decl();
      } else if (peek("enum")) {
        ty = enum_specifier();
      } else {
        ty = find_typedef(token);
        assert(ty);
        token = token->next;
      }

      counter |= OTHER;
      continue;
    }

    if (consume("void"))
      counter += VOID;
    else if (consume("_Bool"))
      counter += BOOL;
    else if (consume("_Bool"))
      counter += BOOL;
    else if (consume("char"))
      counter += CHAR;
    else if (consume("short"))
      counter += SHORT;
    else if (consume("int"))
      counter += INT;
    else if (consume("long"))
      counter += LONG;

    switch (counter) {
    case VOID:
      ty = void_type;
      break;
    case BOOL:
      ty = bool_type;
      break;
    case CHAR:
      ty = char_type;
      break;
    case SHORT:
    case SHORT + INT:
      ty = short_type;
      break;
    case INT:
      ty = int_type;
      break;
    case LONG:
    case LONG + INT:
    case LONG + LONG:
    case LONG + LONG + INT:
      ty = long_type;
      break;
    default:
      error_tok(tok, "invalid type");
    }
  }

  return ty;
}

static Type *declarator(Type *ty, char **name) {
  while (consume("*"))
    ty = pointer_to(ty);

  if (consume("(")) {
    Type *placeholder = calloc(1, sizeof(Type));
    Type *new_ty = declarator(placeholder, name);
    expect(")");
    memcpy(placeholder, type_suffix(ty), sizeof(Type));
    return new_ty;
  }

  *name = expect_ident();
  return type_suffix(ty);
}

static Type *abstract_declarator(Type *ty) {
  while (consume("*"))
    ty = pointer_to(ty);

  if (consume("(")) {
    Type *placeholder = calloc(1, sizeof(Type));
    Type *new_ty = abstract_declarator(placeholder);
    expect(")");
    memcpy(placeholder, type_suffix(ty), sizeof(Type));
    return new_ty;
  }
  return type_suffix(ty);
}

static Type *type_suffix(Type *ty) {
  if (!consume("["))
    return ty;
  int sz = expect_number();
  expect("]");
  ty = type_suffix(ty);
  return array_of(ty, sz);
}

static Type *type_name(void) {
  Type *ty = basetype(NULL);
  ty = abstract_declarator(ty);
  return type_suffix(ty);
}

static void push_tag_scope(Token *tok, Type *ty) {
  TagScope *sc = calloc(1, sizeof(TagScope));
  sc->next = tag_scope;
  sc->name = strndup(tok->str, tok->len);
  sc->ty = ty;
  tag_scope = sc;
}

static Type *struct_decl(void) {
  expect("struct");
  Token *tag = consume_ident();
  if (tag && !peek("{")) {
    TagScope *sc = find_tag(tag);
    if (!sc)
      error_tok(tag, "unknown struct type");
    if (sc->ty->kind != TY_STRUCT)
      error_tok(tag, "not a struct tag");
    return sc->ty;
  }

  expect("{");

  Member head = {};
  Member *cur = &head;

  while (!consume("}")) {
    cur->next = struct_member();
    cur = cur->next;
  }

  Type *ty = calloc(1, sizeof(Type));
  ty->kind = TY_STRUCT;
  ty->members = head.next;

  int offset = 0;
  for (Member *mem = ty->members; mem; mem = mem->next) {
    offset = align_to(offset, mem->ty->align);
    mem->offset = offset;
    offset += mem->ty->size;

    if (ty->align < mem->ty->align)
      ty->align = mem->ty->align;
  }
  ty->size = align_to(offset, ty->align);

  if (tag)
    push_tag_scope(tag, ty);
  return ty;
}

static bool consume_end(void) {
  Token *tok = token;
  if (consume("}") || (consume(",") && consume("}")))
    return true;
  token = tok;
  return false;
}

static Type *enum_specifier(void) {
  expect("enum");
  Type *ty = enum_type();

  Token *tag = consume_ident();
  if (tag && !peek("{")) {
    TagScope *sc = find_tag(tag);
    if (!sc)
      error_tok(tag, "unknown enum type");
    if (sc->ty->kind != TY_ENUM)
      error_tok(tag, "not an enum tag");
    return sc->ty;
  }

  expect("{");

  int cnt = 0;
  for (;;) {
    char *name = expect_ident();
    if (consume("="))
      cnt = expect_number();

    VarScope *sc = push_scope(name);
    sc->enum_ty = ty;
    sc->enum_val = cnt++;

    if (consume_end())
      break;
    expect(",");
  }

  if (tag)
    push_tag_scope(tag, ty);
  return ty;
}

static Member *struct_member(void) {
  Type *ty = basetype(NULL);
  char *name = NULL;
  ty = declarator(ty, &name);
  ty = type_suffix(ty);
  expect(";");

  Member *mem = calloc(1, sizeof(Member));
  mem->name = name;
  mem->ty = ty;
  return mem;
}

static VarList *read_func_param(void) {
  Type *ty = basetype(NULL);
  char *name = NULL;
  ty = declarator(ty, &name);
  ty = type_suffix(ty);

  VarList *vl = calloc(1, sizeof(VarList));
  vl->var = new_lvar(name, ty);
  return vl;
}

static VarList *read_func_params(void) {
  if (consume(")"))
    return NULL;

  VarList *head = read_func_param();
  VarList *cur = head;

  while (!consume(")")) {
    expect(",");
    cur->next = read_func_param();
    cur = cur->next;
  }

  return head;
}

static Function *function(void) {
  locals = NULL;

  StorageClass sclass;
  Type *ty = basetype(&sclass);
  char *name = NULL;
  ty = declarator(ty, &name);

  new_gvar(name, func_type(ty), false);

  Function *fn = calloc(1, sizeof(Function));
  fn->name = name;
  fn->is_static = (sclass == STATIC);
  expect("(");

  Scope *sc = enter_scope();
  fn->params = read_func_params();

  if (consume(";")) {
    leave_scope(sc);
    return NULL;
  }

  Node head = {};
  Node *cur = &head;
  expect("{");
  while (!consume("}")) {
    cur->next = stmt();
    cur = cur->next;
  }
  leave_scope(sc);

  fn->node = head.next;
  fn->locals = locals;
  return fn;
}

static void global_var(void) {
  StorageClass sclass;
  Type *ty = basetype(&sclass);
  char *name = NULL;
  ty = declarator(ty, &name);
  ty = type_suffix(ty);
  expect(";");

  if (sclass == TYPEDEF)
    push_scope(name)->type_def = ty;
  else
    new_gvar(name, ty, true);
}

static Node *declaration(void) {
  Token *tok = token;
  StorageClass sclass;
  Type *ty = basetype(&sclass);
  if (consume(";"))
    return new_node(ND_NULL, tok);

  char *name = NULL;
  ty = declarator(ty, &name);
  ty = type_suffix(ty);

  if (sclass == TYPEDEF) {
    expect(";");
    push_scope(name)->type_def = ty;
    return new_node(ND_NULL, tok);
  }

  if (ty->kind == TY_VOID)
    error_tok(tok, "variable declared void");

  Var *var = new_lvar(name, ty);
  if (consume(";"))
    return new_node(ND_NULL, tok);

  expect("=");

  Node *lhs = new_var_node(var, tok);
  Node *rhs = expr();
  expect(";");
  Node *node = new_binary(ND_ASSIGN, lhs, rhs, tok);
  return new_unary(ND_EXPR_STMT, node, tok);
}

static Node *read_expr_stmt(void) {
  Token *tok = token;
  return new_unary(ND_EXPR_STMT, expr(), tok);
}

static bool is_typename(void) {
  return peek("void") || peek("_Bool") || peek("char") || peek("short") ||
         peek("int") || peek("long") || peek("enum") || peek("struct") ||
         peek("typedef") || peek("static") || find_typedef(token);
}

static Node *stmt(void) {
  Node *node = stmt2();
  add_type(node);
  return node;
}

static Node *stmt2(void) {
  Token *tok;
  if (tok = consume("return")) {
    Node *node = new_unary(ND_RETURN, expr(), tok);
    expect(";");
    return node;
  }

  if (tok = consume("if")) {
    Node *node = new_node(ND_IF, tok);
    expect("(");
    node->cond = expr();
    expect(")");
    node->then = stmt();
    if (consume("else"))
      node->els = stmt();
    return node;
  }

  if (tok = consume("while")) {
    Node *node = new_node(ND_WHILE, tok);
    expect("(");
    node->cond = expr();
    expect(")");
    node->then = stmt();
    return node;
  }

  if (tok = consume("for")) {
    Node *node = new_node(ND_FOR, tok);
    expect("(");
    Scope *sc = enter_scope();

    if (!consume(";")) {
      if (is_typename()) {
        node->init = declaration();
      } else {
        node->init = read_expr_stmt();
        expect(";");
      }
    }
    if (!consume(";")) {
      node->cond = expr();
      expect(";");
    }
    if (!consume(")")) {
      node->inc = read_expr_stmt();
      expect(")");
    }
    node->then = stmt();

    leave_scope(sc);
    return node;
  }

  if (tok = consume("{")) {
    Node head = {};
    Node *cur = &head;

    Scope *sc = enter_scope();
    while (!consume("}")) {
      cur->next = stmt();
      cur = cur->next;
    }
    leave_scope(sc);

    Node *node = new_node(ND_BLOCK, tok);
    node->body = head.next;
    return node;
  }

  if (is_typename())
    return declaration();

  Node *node = read_expr_stmt();
  expect(";");
  return node;
}

static Node *expr(void) {
  Node *node = assign();
  Token *tok;
  while (tok = consume(",")) {
    node = new_unary(ND_EXPR_STMT, node, node->tok);
    node = new_binary(ND_COMMA, node, assign(), tok);
  }
  return node;
}

static Node *assign(void) {
  Node *node = equality();
  Token *tok;
  if (tok = consume("="))
    node = new_binary(ND_ASSIGN, node, assign(), tok);
  return node;
}

static Node *equality(void) {
  Node *node = relational();
  Token *tok;

  for (;;) {
    if (tok = consume("=="))
      node = new_binary(ND_EQ, node, relational(), tok);
    else if (tok = consume("!="))
      node = new_binary(ND_NE, node, relational(), tok);
    else
      return node;
  }
}

static Node *relational(void) {
  Node *node = add();
  Token *tok;

  for (;;) {
    if (tok = consume("<"))
      node = new_binary(ND_LT, node, add(), tok);
    else if (tok = consume("<="))
      node = new_binary(ND_LE, node, add(), tok);
    else if (tok = consume(">"))
      node = new_binary(ND_LT, add(), node, tok);
    else if (tok = consume(">="))
      node = new_binary(ND_LE, add(), node, tok);
    else
      return node;
  }
}

static Node *new_add(Node *lhs, Node *rhs, Token *tok) {
  add_type(lhs);
  add_type(rhs);

  if (is_integer(lhs->ty) && is_integer(rhs->ty))
    return new_binary(ND_ADD, lhs, rhs, tok);
  if (lhs->ty->base && is_integer(rhs->ty))
    return new_binary(ND_PTR_ADD, lhs, rhs, tok);
  if (is_integer(lhs->ty) && rhs->ty->base)
    return new_binary(ND_PTR_ADD, rhs, lhs, tok);
  error_tok(tok, "invalid operands");
}

static Node *new_sub(Node *lhs, Node *rhs, Token *tok) {
  add_type(lhs);
  add_type(rhs);

  if (is_integer(lhs->ty) && is_integer(rhs->ty))
    return new_binary(ND_SUB, lhs, rhs, tok);
  if (lhs->ty->base && is_integer(rhs->ty))
    return new_binary(ND_PTR_SUB, lhs, rhs, tok);
  if (lhs->ty->base && rhs->ty->base)
    return new_binary(ND_PTR_DIFF, lhs, rhs, tok);
  error_tok(tok, "invalid operands");
}

static Node *add(void) {
  Node *node = mul();
  Token *tok;

  for (;;) {
    if (tok = consume("+"))
      node = new_add(node, mul(), tok);
    else if (tok = consume("-"))
      node = new_sub(node, mul(), tok);
    else
      return node;
  }
}

static Node *mul(void) {
  Node *node = cast();
  Token *tok;

  for (;;) {
    if (tok = consume("*"))
      node = new_binary(ND_MUL, node, cast(), tok);
    else if (tok = consume("/"))
      node = new_binary(ND_DIV, node, cast(), tok);
    else
      return node;
  }
}

static Node *cast(void) {
  Token *tok = token;

  if (consume("(")) {
    if (is_typename()) {
      Type *ty = type_name();
      expect(")");
      Node *node = new_unary(ND_CAST, cast(), tok);
      add_type(node->lhs);
      node->ty = ty;
      return node;
    }
    token = tok;
  }

  return unary();
}

static Node *unary(void) {
  Token *tok;

  if (consume("+"))
    return cast();
  if (tok = consume("-"))
    return new_binary(ND_SUB, new_num(0, tok), cast(), tok);
  if (tok = consume("&"))
    return new_unary(ND_ADDR, cast(), tok);
  if (tok = consume("*"))
    return new_unary(ND_DEREF, cast(), tok);
  if (tok = consume("++"))
    return new_unary(ND_PRE_INC, unary(), tok);
  if (tok = consume("--"))
    return new_unary(ND_PRE_DEC, unary(), tok);
  return postfix();
}

static Member *find_member(Type *ty, char *name) {
  for (Member *mem = ty->members; mem; mem = mem->next)
    if (!strcmp(mem->name, name))
      return mem;
  return NULL;
}

static Node *struct_ref(Node *lhs) {
  add_type(lhs);
  if (lhs->ty->kind != TY_STRUCT)
    error_tok(lhs->tok, "not a struct");

  Token *tok = token;
  Member *mem = find_member(lhs->ty, expect_ident());
  if (!mem)
    error_tok(tok, "no such member");

  Node *node = new_unary(ND_MEMBER, lhs, tok);
  node->member = mem;
  return node;
}

static Node *postfix(void) {
  Node *node = primary();
  Token *tok;

  for (;;) {
    if (tok = consume("[")) {
      Node *exp = new_add(node, expr(), tok);
      expect("]");
      node = new_unary(ND_DEREF, exp, tok);
      continue;
    }

    if (tok = consume(".")) {
      node = struct_ref(node);
      continue;
    }

    if (tok = consume("->")) {
      node = new_unary(ND_DEREF, node, tok);
      node = struct_ref(node);
      continue;
    }

    if (tok = consume("++")) {
      node = new_unary(ND_POST_INC, node, tok);
      continue;
    }

    if (tok = consume("--")) {
      node = new_unary(ND_POST_DEC, node, tok);
      continue;
    }

    return node;
  }
}

static Node *stmt_expr(Token *tok) {
  Scope *sc = enter_scope();

  Node *node = new_node(ND_STMT_EXPR, tok);
  node->body = stmt();
  Node *cur = node->body;

  while (!consume("}")) {
    cur->next = stmt();
    cur = cur->next;
  }
  expect(")");

  leave_scope(sc);

  if (cur->kind != ND_EXPR_STMT)
    error_tok(cur->tok, "stmt expr returning void is not supported");
  memcpy(cur, cur->lhs, sizeof(Node));
  return node;
}

static Node *func_args(void) {
  if (consume(")"))
    return NULL;

  Node *head = assign();
  Node *cur = head;
  while (consume(",")) {
    cur->next = assign();
    cur = cur->next;
  }
  expect(")");
  return head;
}

static Node *primary(void) {
  Token *tok;

  if (tok = consume("(")) {
    if (consume("{"))
      return stmt_expr(tok);

    Node *node = expr();
    expect(")");
    return node;
  }

  if (tok = consume("sizeof")) {
    if (consume("(")) {
      if (is_typename()) {
        Type *ty = type_name();
        expect(")");
        return new_num(ty->size, tok);
      }
      token = tok->next;
    }

    Node *node = unary();
    add_type(node);
    return new_num(node->ty->size, tok);
  }

  if (tok = consume_ident()) {
    if (consume("(")) {
      Node *node = new_node(ND_FUNCALL, tok);
      node->funcname = strndup(tok->str, tok->len);
      node->args = func_args();
      add_type(node);

      VarScope *sc = find_var(tok);
      if (sc) {
        if (!sc->var || sc->var->ty->kind != TY_FUNC)
          error_tok(tok, "not a function");
        node->ty = sc->var->ty->return_ty;
      } else {
        warn_tok(node->tok, "implicit declaration of a function");
        node->ty = int_type;
      }
      return node;
    }

    VarScope *sc = find_var(tok);
    if (sc) {
      if (sc->var)
        return new_var_node(sc->var, tok);
      if (sc->enum_ty)
        return new_num(sc->enum_val, tok);
    }
    return new_var_node(sc->var, tok);
    error_tok(tok, "undefined variable");
  }

  tok = token;
  if (tok->kind == TK_STR) {
    token = token->next;

    Type *ty = array_of(char_type, tok->cont_len);
    Var *var = new_gvar(new_label(), ty, true);
    var->contents = tok->contents;
    var->cont_len = tok->cont_len;
    return new_var_node(var, tok);
  }

  if (tok->kind != TK_NUM)
    error_tok(tok, "expected expression");
  return new_num(expect_number(), tok);
}
