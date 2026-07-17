extern int hsc_increment(int value);

int main(void) {
  return hsc_increment(41) == 42 ? 0 : 1;
}
