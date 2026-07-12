struct Pair {
  int left;
  char tag;
};

extern int native_sum(struct Pair pair);

struct Pair pass(struct Pair pair) {
  return pair;
}

struct Pair global_pair;

int main(void) {
  global_pair.left = 40;
  global_pair.tag = 2;
  return native_sum(pass(global_pair)) - 42;
}
