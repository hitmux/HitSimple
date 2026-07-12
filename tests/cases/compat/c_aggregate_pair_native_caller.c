struct Pair {
  int left;
  char tag;
};

extern struct Pair pass_pair(struct Pair value);

int main(void) {
  struct Pair input = {40, 2};
  struct Pair output = pass_pair(input);
  return output.left + output.tag - 42;
}
