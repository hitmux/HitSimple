extern int hsc_increment(int value);
extern void hsc_store(int *value, int replacement);

int main(void) {
  int value = 1;
  hsc_store(&value, 40);
  if (value != 40) return 1;
  return hsc_increment(value) - 42;
}
