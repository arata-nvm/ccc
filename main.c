#include "chibi.h"

static char *read_file(char *path) {
  FILE *fp = fopen(path, "r");
  if (!fp)
    error("cannot open %s: %s", path, strerror(errno));

  int filemax = 10 * 1024 * 1024;
  char *buf = malloc(filemax);
  int size = fread_unlocked(buf, 1, filemax - 2, fp);
  if (!feof(fp))
    error("%s: file too large");

  if (size == 0 || buf[size - 1] != '\n')
    buf[size++] = '\n';
  buf[size] = '\0';
  return buf;
}

int align_to(int n, int align) { return (n + align - 1) & ~(align - 1); }

int main(int argc, char **argv) {
  if (argc != 2)
    error("%s: invlid number of arguments\n", argv[0]);

  filename = argv[1];
  user_input = read_file(argv[1]);
  token = tokenize();
  Program *prog = program();

  for (Function *fn = prog->fns; fn; fn = fn->next) {
    int offset = 0;
    for (VarList *vl = fn->locals; vl; vl = vl->next) {
      Var *var = vl->var;
      offset += var->ty->size;
      var->offset = offset;
    }
    fn->stack_size = align_to(offset, 8);
  }

  codegen(prog);

  return 0;
}
