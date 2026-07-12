struct Pair {
  int left;
  char tag;
};

int native_sum(struct Pair pair) {
  return pair.left + pair.tag;
}
