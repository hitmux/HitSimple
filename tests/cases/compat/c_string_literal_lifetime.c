char *preserve(char *text) { return text; }
int main(void) {
  char *p = preserve("abc");
  int index = 1;
  return p[index] - 'b';
}
