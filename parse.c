#include "chibi.h"

typedef struct VarScope VarScope;
struct VarScope {
  VarScope *next;
  char *name;
  int depth;

  Var *var;
  Type *type_def;
  Type *enum_ty;
  int enum_val;
};

typedef struct TagScope TagScope;
struct TagScope {
  TagScope *next;
  char *name;
  int depth;
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
static int scope_depth;

static Node *current_switch;

static Scope *enter_scope(void) {
  Scope *sc = calloc(1, sizeof(Scope));
  sc->var_scope = var_scope;
  sc->tag_scope = tag_scope;
  scope_depth++;
  return sc;
}

static void leave_scope(Scope *sc) {
  var_scope = sc->var_scope;
  tag_scope = sc->tag_scope;
  scope_depth--;
}

static VarScope *find_var(Token *tok) {
  for (VarScope *sc = var_scope; sc; sc = sc->next)
    if (strlen(sc->name) == tok->len && !strncmp(tok->str, sc->name, tok->len))
      return sc;
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
  sc->depth = scope_depth;
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

static Var *new_gvar(char *name, Type *ty, bool is_static, bool emit) {
  Var *var = new_var(name, ty, false);
  var->is_static = is_static;
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
  EXTERN = 1 << 2,
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
static long eval(Node *node);
static long eval2(Node *node, Var **var);
static long const_expr(void);
static Node *assign(void);
static Node *conditional(void);
static Node *logor(void);
static Node *logand(void);
static Node *bitand(void);
static Node * bitor (void);
static Node *bitxor(void);
static Node *equality(void);
static Node *relational(void);
static Node *shift(void);
static Node *new_add(Node *lhs, Node *rhs, Token *tok);
static Node *add(void);
static Node *mul(void);
static Node *cast(void);
static Node *unary(void);
static Node *postfix(void);
static Node *compound_literal(void);
static Node *primary(void);

static bool is_function(void) {
  Token *tok = token;
  bool isfunc = false;

  StorageClass sclass;
  Type *ty = basetype(&sclass);

  if (!consume(";")) {
    char *name = NULL;
    declarator(ty, &name);
    isfunc = name && consume("(");
  }

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

    if (peek("typedef") || peek("static") || peek("extern")) {
      if (!sclass)
        error_tok(tok, "storage class specifier is not allowed");

      if (consume("typedef"))
        *sclass |= TYPEDEF;
      else if (consume("static"))
        *sclass |= STATIC;
      else if (consume("extern"))
        *sclass |= EXTERN;

      if (*sclass & (*sclass - 1))
        error_tok(tok, "typedef, static and extern may not be used together");
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

  int sz = 0;
  bool is_incomplete = true;
  if (!consume("]")) {
    sz = const_expr();
    is_incomplete = false;
    expect("]");
  }

  Token *tok = token;
  ty = type_suffix(ty);
  if (ty->is_incomplete)
    error_tok(tok, "incomplete element type");

  ty = array_of(ty, sz);
  ty->is_incomplete = is_incomplete;
  return ty;
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
  sc->depth = scope_depth;
  sc->ty = ty;
  tag_scope = sc;
}

static Type *struct_decl(void) {
  expect("struct");
  Token *tag = consume_ident();
  if (tag && !peek("{")) {
    TagScope *sc = find_tag(tag);

    if (!sc) {
      Type *ty = struct_type();
      push_tag_scope(tag, ty);
      return ty;
    }

    if (sc->ty->kind != TY_STRUCT)
      error_tok(tag, "not a struct tag");
    return sc->ty;
  }

  if (!consume("{"))
    return struct_type();

  Type *ty;

  TagScope *sc = NULL;
  if (tag)
    sc = find_tag(tag);

  if (sc && sc->depth == scope_depth) {
    if (sc->ty->kind != TY_STRUCT)
      error_tok(tag, "not a struct tag");
    ty = sc->ty;
  } else {
    ty = struct_type();
    if (tag)
      push_tag_scope(tag, ty);
  }

  Member head = {};
  Member *cur = &head;

  while (!consume("}")) {
    cur->next = struct_member();
    cur = cur->next;
  }

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

  ty->is_incomplete = false;
  return ty;
}

static bool consume_end(void) {
  Token *tok = token;
  if (consume("}") || (consume(",") && consume("}")))
    return true;
  token = tok;
  return false;
}

static bool peek_end(void) {
  Token *tok = token;
  bool ret = consume("}") || (consume(",") && consume("}"));
  token = tok;
  return ret;
}

static void expect_end(void) {
  if (!consume_end())
    expect("}");
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
      cnt = const_expr();

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

  if (ty->kind == TY_ARRAY)
    ty = pointer_to(ty->base);

  VarList *vl = calloc(1, sizeof(VarList));
  vl->var = new_lvar(name, ty);
  return vl;
}

static VarList *read_func_params(void) {
  if (consume(")"))
    return NULL;

  Token *tok = token;
  if (consume("void") && consume(")"))
    return NULL;
  token = tok;

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

  new_gvar(name, func_type(ty), false, false);

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

static Initializer *new_init_val(Initializer *cur, int sz, int val) {
  Initializer *init = calloc(1, sizeof(Initializer));
  init->sz = sz;
  init->val = val;
  cur->next = init;
  return init;
}

static Initializer *new_init_label(Initializer *cur, char *label, long addend) {
  Initializer *init = calloc(1, sizeof(Initializer));
  init->label = label;
  init->addend = addend;
  cur->next = init;
  return init;
}

static Initializer *new_init_zero(Initializer *cur, int nbytes) {
  for (int i = 0; i < nbytes; i++)
    cur = new_init_val(cur, 1, 0);
  return cur;
}

static Initializer *gvar_init_string(char *p, int len) {
  Initializer head = {};
  Initializer *cur = &head;
  for (int i = 0; i < len; i++)
    cur = new_init_val(cur, 1, p[i]);
  return head.next;
}

static Initializer *emit_struct_padding(Initializer *cur, Type *parent,
                                        Member *mem) {
  int start = mem->offset + mem->ty->size;
  int end = mem->next ? mem->next->offset : parent->size;
  return new_init_zero(cur, end - start);
}

static void skip_excess_elements2(void) {
  for (;;) {
    if (consume("{"))
      skip_excess_elements2();
    else
      assign();

    if (consume_end())
      return;
    expect(",");
  }
}

static void skip_excess_elements(void) {
  expect(",");
  warn_tok(token, "excess elements in initializer");
  skip_excess_elements2();
}

static Initializer *gvar_initializer2(Initializer *cur, Type *ty) {
  Token *tok = token;

  if (ty->kind == TY_ARRAY && ty->base->kind == TY_CHAR &&
      token->kind == TK_STR) {
    token = token->next;

    if (ty->is_incomplete) {
      ty->size = tok->cont_len;
      ty->array_len = tok->cont_len;
      ty->is_incomplete = false;
    }

    int len = (ty->array_len < tok->cont_len) ? ty->array_len : tok->cont_len;

    for (int i = 0; i < len; i++)
      cur = new_init_val(cur, 1, tok->contents[i]);
    return new_init_zero(cur, ty->array_len - len);
  }

  if (ty->kind == TY_ARRAY) {
    bool open = consume("{");
    int i = 0;
    int limit = ty->is_incomplete ? INT_MAX : ty->array_len;

    if (!peek("}")) {
      do {
        cur = gvar_initializer2(cur, ty->base);
        i++;
      } while (i < limit && !peek_end() && consume(","));
    }

    if (open && !consume_end())
      skip_excess_elements();

    cur = new_init_zero(cur, ty->base->size * (ty->array_len - i));

    if (ty->is_incomplete) {
      ty->size = ty->base->size * i;
      ty->array_len = i;
      ty->is_incomplete = false;
    }
    return cur;
  }

  if (ty->kind == TY_STRUCT) {
    bool open = consume("{");
    Member *mem = ty->members;

    if (!peek("}")) {
      do {
        cur = gvar_initializer2(cur, mem->ty);
        cur = emit_struct_padding(cur, ty, mem);
        mem = mem->next;
      } while (mem && !peek_end() && consume(","));
    }

    if (open && !consume_end())
      skip_excess_elements();

    if (mem)
      cur = new_init_zero(cur, ty->size - mem->offset);
    return cur;
  }

  bool open = consume("{");
  Node *expr = conditional();
  if (open)
    expect_end();

  Var *var = NULL;
  long addend = eval2(expr, &var);

  if (var) {
    int scale =
        (var->ty->kind == TY_ARRAY) ? var->ty->base->size : var->ty->size;
    return new_init_label(cur, var->name, addend * scale);
  }

  return new_init_val(cur, ty->size, addend);
}

static Initializer *gvar_initializer(Type *ty) {
  Initializer head = {};
  gvar_initializer2(&head, ty);
  return head.next;
}

static void global_var(void) {
  StorageClass sclass;
  Type *ty = basetype(&sclass);
  if (consume(";"))
    return;

  char *name = NULL;
  Token *tok = token;
  ty = declarator(ty, &name);
  ty = type_suffix(ty);

  if (sclass == TYPEDEF) {
    expect(";");
    push_scope(name)->type_def = ty;
    return;
  }

  Var *var = new_gvar(name, ty, sclass == STATIC, sclass != EXTERN);

  if (sclass == EXTERN) {
    expect(";");
    return;
  }

  if (consume("=")) {
    var->initializer = gvar_initializer(ty);
    expect(";");
    return;
  }

  if (ty->is_incomplete)
    error_tok(tok, "incomplete type");
  expect(";");
}

typedef struct Designator Designator;
struct Designator {
  Designator *next;
  int idx;
  Member *mem;
};

static Node *new_desg_node2(Var *var, Designator *desg, Token *tok) {
  if (!desg)
    return new_var_node(var, tok);

  Node *node = new_desg_node2(var, desg->next, tok);

  if (desg->mem) {
    node = new_unary(ND_MEMBER, node, desg->mem->tok);
    node->member = desg->mem;
    return node;
  }

  node = new_add(node, new_num(desg->idx, tok), tok);
  return new_unary(ND_DEREF, node, tok);
}

static Node *new_desg_node(Var *var, Designator *desg, Node *rhs) {
  Node *lhs = new_desg_node2(var, desg, rhs->tok);
  Node *node = new_binary(ND_ASSIGN, lhs, rhs, rhs->tok);
  return new_unary(ND_EXPR_STMT, node, rhs->tok);
}

static Node *lvar_init_zero(Node *cur, Var *var, Type *ty, Designator *desg) {
  if (ty->kind == TY_ARRAY) {
    for (int i = 0; i < ty->array_len; i++) {
      Designator desg2 = {desg, i++};
      cur = lvar_init_zero(cur, var, ty->base, &desg2);
    }
    return cur;
  }

  cur->next = new_desg_node(var, desg, new_num(0, token));
  return cur->next;
}

static Node *lvar_initializer2(Node *cur, Var *var, Type *ty,
                               Designator *desg) {
  if (ty->kind == TY_ARRAY && ty->base->kind == TY_CHAR &&
      token->kind == TK_STR) {
    Token *tok = token;
    token = token->next;

    if (ty->is_incomplete) {
      ty->size = tok->cont_len;
      ty->array_len = tok->cont_len;
      ty->is_incomplete = false;
    }

    int len = (ty->array_len < tok->cont_len) ? ty->array_len : tok->cont_len;

    for (int i = 0; i < len; i++) {
      Designator desg2 = {desg, i};
      Node *rhs = new_num(tok->contents[i], tok);
      cur->next = new_desg_node(var, &desg2, rhs);
      cur = cur->next;
    }

    for (int i = len; i < ty->array_len; i++) {
      Designator desg2 = {desg, i};
      cur = lvar_init_zero(cur, var, ty->base, &desg2);
    }
    return cur;
  }

  if (ty->kind == TY_ARRAY) {
    bool open = consume("{");
    int i = 0;
    int limit = ty->is_incomplete ? INT_MAX : ty->array_len;

    if (!peek("}")) {
      do {
        Designator desg2 = {desg, i++};
        cur = lvar_initializer2(cur, var, ty->base, &desg2);
      } while (i < limit && !peek_end() && consume(","));
    }

    if (open && !consume_end())
      skip_excess_elements();

    while (i < ty->array_len) {
      Designator desg2 = {desg, i++};
      cur = lvar_init_zero(cur, var, ty->base, &desg2);
    }

    if (ty->is_incomplete) {
      ty->size = ty->base->size * i;
      ty->array_len = i;
      ty->is_incomplete = false;
    }
    return cur;
  }

  if (ty->kind == TY_STRUCT) {
    bool open = consume("{");
    Member *mem = ty->members;

    if (!peek("}")) {
      do {
        Designator desg2 = {desg, 0, mem};
        cur = lvar_initializer2(cur, var, mem->ty, &desg2);
        mem = mem->next;
      } while (mem && !peek_end() && consume(","));
    }

    if (open && !consume_end())
      skip_excess_elements();

    for (; mem; mem = mem->next) {
      Designator desg2 = {desg, 0, mem};
      cur = lvar_init_zero(cur, var, mem->ty, &desg2);
    }
    return cur;
  }

  bool open = consume("{");
  cur->next = new_desg_node(var, desg, assign());
  if (open)
    expect_end();
  return cur->next;
}

static Node *lvar_initializer(Var *var, Token *tok) {
  Node head = {};
  lvar_initializer2(&head, var, var->ty, NULL);

  Node *node = new_node(ND_BLOCK, tok);
  node->body = head.next;
  return node;
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

  if (sclass == STATIC) {
    Var *var = new_gvar(new_label(), ty, true, true);
    push_scope(name)->var = var;

    if (consume("="))
      var->initializer = gvar_initializer(ty);
    else if (ty->is_incomplete)
      error_tok(tok, "incomplete type");
    consume(";");
    return new_node(ND_NULL, tok);
  }

  Var *var = new_lvar(name, ty);

  if (consume(";")) {
    if (ty->is_incomplete)
      error_tok(tok, "incomplete type");
    return new_node(ND_NULL, tok);
  }

  expect("=");

  Node *node = lvar_initializer(var, tok);
  expect(";");
  return node;
}

static Node *read_expr_stmt(void) {
  Token *tok = token;
  return new_unary(ND_EXPR_STMT, expr(), tok);
}

static bool is_typename(void) {
  return peek("void") || peek("_Bool") || peek("char") || peek("short") ||
         peek("int") || peek("long") || peek("enum") || peek("struct") ||
         peek("typedef") || peek("static") || peek("extern") ||
         find_typedef(token);
}

static Node *stmt(void) {
  Node *node = stmt2();
  add_type(node);
  return node;
}

static Node *stmt2(void) {
  Token *tok;
  if (tok = consume("return")) {
    if (tok = consume(";"))
      return new_node(ND_RETURN, tok);

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

  if (tok = consume("switch")) {
    Node *node = new_node(ND_SWITCH, tok);
    expect("(");
    node->cond = expr();
    expect(")");

    Node *sw = current_switch;
    current_switch = node;
    node->then = stmt();
    current_switch = sw;
    return node;
  }

  if (tok = consume("case")) {
    if (!current_switch)
      error_tok(tok, "stray case");
    int val = const_expr();
    expect(":");

    Node *node = new_unary(ND_CASE, stmt(), tok);
    node->val = val;
    node->case_next = current_switch->case_next;
    current_switch->case_next = node;
    return node;
  }

  if (tok = consume("default")) {
    if (!current_switch)
      error_tok(tok, "stray default");
    expect(":");

    Node *node = new_unary(ND_CASE, stmt(), tok);
    current_switch->default_case = node;
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

  if (tok = consume("break")) {
    expect(";");
    return new_node(ND_BREAK, tok);
  }

  if (tok = consume("continue")) {
    expect(";");
    return new_node(ND_CONTINUE, tok);
  }

  if (tok = consume("goto")) {
    Node *node = new_node(ND_GOTO, tok);
    node->label_name = expect_ident();
    expect(";");
    return node;
  }

  if (tok = consume(";"))
    return new_node(ND_NULL, tok);

  if (tok = consume_ident()) {
    if (consume(":")) {
      Node *node = new_unary(ND_LABEL, stmt(), tok);
      node->label_name = strndup(tok->str, tok->len);
      return node;
    }
    token = tok;
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

static long eval(Node *node) { return eval2(node, NULL); }

static long eval2(Node *node, Var **var) {
  switch (node->kind) {
  case ND_ADD:
    return eval(node->lhs) + eval(node->rhs);
  case ND_PTR_ADD:
    return eval2(node->lhs, var) + eval(node->rhs);
  case ND_SUB:
    return eval(node->lhs) - eval(node->rhs);
  case ND_PTR_SUB:
    return eval2(node->lhs, var) - eval(node->rhs);
  case ND_PTR_DIFF:
    return eval2(node->lhs, var) - eval2(node->rhs, var);
  case ND_MUL:
    return eval(node->lhs) * eval(node->rhs);
  case ND_DIV:
    return eval(node->lhs) / eval(node->rhs);
  case ND_BITAND:
    return eval(node->lhs) & eval(node->rhs);
  case ND_BITOR:
    return eval(node->lhs) | eval(node->rhs);
  case ND_BITXOR:
    return eval(node->lhs) | eval(node->rhs);
  case ND_SHL:
    return eval(node->lhs) << eval(node->rhs);
  case ND_SHR:
    return eval(node->lhs) >> eval(node->rhs);
  case ND_EQ:
    return eval(node->lhs) == eval(node->rhs);
  case ND_NE:
    return eval(node->lhs) != eval(node->rhs);
  case ND_LT:
    return eval(node->lhs) < eval(node->rhs);
  case ND_LE:
    return eval(node->lhs) <= eval(node->rhs);
  case ND_TERNARY:
    return eval(node->cond) ? eval(node->then) : eval(node->els);
  case ND_COMMA:
    return eval(node->rhs);
  case ND_NOT:
    return !eval(node->lhs);
  case ND_BITNOT:
    return ~eval(node->lhs);
  case ND_LOGAND:
    return eval(node->lhs) && eval(node->rhs);
  case ND_LOGOR:
    return eval(node->lhs) || eval(node->rhs);
  case ND_NUM:
    return node->val;
  case ND_ADDR:
    if (!var || *var || node->lhs->kind != ND_VAR || node->lhs->var->is_local)
      error_tok(node->tok, "invalid initializer");
    *var = node->lhs->var;
    return 0;
  case ND_VAR:
    if (!var || *var || node->var->ty->kind != TY_ARRAY)
      error_tok(node->tok, "invalid initializer");
    *var = node->var;
    return 0;
  }

  error_tok(node->tok, "not a constant expression");
}

static long const_expr(void) { return eval(conditional()); }

static Node *assign(void) {
  Node *node = conditional();
  Token *tok;
  if (tok = consume("="))
    return new_binary(ND_ASSIGN, node, assign(), tok);

  if (tok = consume("*="))
    return new_binary(ND_MUL_EQ, node, assign(), tok);

  if (tok = consume("/="))
    return new_binary(ND_DIV_EQ, node, assign(), tok);

  if (tok = consume("<<="))
    return new_binary(ND_SHL_EQ, node, assign(), tok);

  if (tok = consume(">>="))
    return new_binary(ND_SHR_EQ, node, assign(), tok);

  if (tok = consume("+=")) {
    add_type(node);
    if (node->ty->base)
      return new_binary(ND_PTR_ADD_EQ, node, assign(), tok);
    else
      return new_binary(ND_ADD_EQ, node, assign(), tok);
  }

  if (tok = consume("-=")) {
    add_type(node);
    if (node->ty->base)
      return new_binary(ND_PTR_SUB_EQ, node, assign(), tok);
    else
      return new_binary(ND_SUB_EQ, node, assign(), tok);
  }

  return node;
}

static Node *conditional(void) {
  Node *node = logor();
  Token *tok = consume("?");
  if (!tok)
    return node;

  Node *ternary = new_node(ND_TERNARY, tok);
  ternary->cond = node;
  ternary->then = expr();
  expect(":");
  ternary->els = conditional();
  return ternary;
}

static Node *logor(void) {
  Node *node = logand();
  Token *tok;
  while (tok = consume("||"))
    node = new_binary(ND_LOGOR, node, logand(), tok);
  return node;
}

static Node *logand(void) {
  Node *node = bitor ();
  Token *tok;
  while (tok = consume("&&"))
    node = new_binary(ND_LOGAND, node, bitor (), tok);
  return node;
}

static Node * bitor (void) {
  Node *node = bitxor();
  Token *tok;
  while (tok = consume("|"))
    node = new_binary(ND_BITOR, node, bitxor(), tok);
  return node;
}

static Node *bitxor(void) {
  Node *node = bitand();
  Token *tok;
  while (tok = consume("^"))
    node = new_binary(ND_BITXOR, node, bitxor(), tok);
  return node;
}

static Node *bitand(void) {
  Node *node = equality();
  Token *tok;
  while (tok = consume("&"))
    node = new_binary(ND_BITAND, node, equality(), tok);
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
  Node *node = shift();
  Token *tok;

  for (;;) {
    if (tok = consume("<"))
      node = new_binary(ND_LT, node, shift(), tok);
    else if (tok = consume("<="))
      node = new_binary(ND_LE, node, shift(), tok);
    else if (tok = consume(">"))
      node = new_binary(ND_LT, shift(), node, tok);
    else if (tok = consume(">="))
      node = new_binary(ND_LE, shift(), node, tok);
    else
      return node;
  }
}

static Node *shift(void) {
  Node *node = add();
  Token *tok;

  for (;;) {
    if (tok = consume("<<"))
      node = new_binary(ND_SHL, node, add(), tok);
    else if (tok = consume(">>"))
      node = new_binary(ND_SHR, node, add(), tok);
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
      if (!consume("{")) {
        Node *node = new_unary(ND_CAST, cast(), tok);
        add_type(node->lhs);
        node->ty = ty;
        return node;
      }
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
  if (tok = consume("!"))
    return new_unary(ND_NOT, cast(), tok);
  if (tok = consume("~"))
    return new_unary(ND_BITNOT, cast(), tok);
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
  Token *tok;

  Node *node = compound_literal();
  if (node)
    return node;

  node = primary();

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

static Node *compound_literal(void) {
  Token *tok = token;
  if (!consume("(") || !is_typename()) {
    token = tok;
    return NULL;
  }

  Type *ty = type_name();
  expect(")");

  if (!peek("{")) {
    token = tok;
    return NULL;
  }

  if (scope_depth == 0) {
    Var *var = new_gvar(new_label(), ty, true, true);
    var->initializer = gvar_initializer(ty);
    return new_var_node(var, tok);
  }

  Var *var = new_lvar(new_label(), ty);
  Node *node = new_var_node(var, tok);
  node->init = lvar_initializer(var, tok);
  return node;
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

  if (tok = consume("_Alignof")) {
    expect("(");
    Type *ty = type_name();
    expect(")");
    return new_num(ty->align, tok);
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
    error_tok(tok, "undefined variable");
  }

  tok = token;
  if (tok->kind == TK_STR) {
    token = token->next;

    Type *ty = array_of(char_type, tok->cont_len);
    Var *var = new_gvar(new_label(), ty, true, true);
    var->initializer = gvar_init_string(tok->contents, tok->cont_len);
    return new_var_node(var, tok);
  }

  if (tok->kind != TK_NUM)
    error_tok(tok, "expected expression");
  return new_num(expect_number(), tok);
}
