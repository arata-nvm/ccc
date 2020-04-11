#include "chibi.h"

int main(int argc, char **argv) {
  if (argc != 2)
    error("%s: invlid number of arguments\n", argv[0]);

  user_input = argv[1];
  token = tokenize();
  Function *prog = program();

  int offset = 0;
  for (Var *var = prog->locals; var; var = var->next) {
    offset += 8;
    var->offset = offset;
  }
  prog->stack_size = offset;

  codegen(prog);

  return 0;
}
