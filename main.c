#include "chibi.h"

int main(int argc, char **argv) {
  if (argc != 2)
    error("%s: invlid number of arguments\n", argv[0]);

  user_input = argv[1];
  token = tokenize();
  Node *node = program();

  codegen(node);
  return 0;
}
