static int private_global = 7;

static int private_function(void) {
  return private_global;
}

int shared = 40;

int read_a(void) {
  return private_function();
}
