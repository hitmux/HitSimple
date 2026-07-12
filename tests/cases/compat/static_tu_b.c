static int private_global = 11;

static int private_function(void) {
  return private_global;
}

extern int shared;
extern int read_a(void);

int main(void) {
  shared += 2;
  if (shared != 42) return 1;
  if (read_a() != 7) return 2;
  if (private_function() != 11) return 3;
  return 0;
}
