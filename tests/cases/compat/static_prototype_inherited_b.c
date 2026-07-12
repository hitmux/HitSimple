int helper(void) {
  return 7;
}

extern int read_static(void);

int main(void) {
  if (read_static() != 42) return 1;
  return helper() - 7;
}
