#include "chibi.h"

int main(int argc, char **argv) {
  if (argc != 2)
    error("%s: invlid number of arguments\n", argv[0]);

  user_input = argv[1];
  token = tokenize();
  Program *prog = program();

  for (Function *fn = prog->fns; fn; fn = fn->next) {
    int offset = 0;
    for (VarList *vl = fn->locals; vl; vl = vl->next) {
      Var *var = vl->var;
      offset += var->ty->size;
      var->offset = offset;
    }
    fn->stack_size = offset;
  }

  codegen(prog);

  return 0;
}
