static int helper(void);

int helper(void) {
  return 42;
}

int read_static(void) {
  return helper();
}
