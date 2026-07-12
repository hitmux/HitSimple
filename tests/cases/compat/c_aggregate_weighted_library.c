struct Weighted {
  char tag;
  double weight;
  int bonus;
};

struct Weighted pass_weighted(struct Weighted value) {
  return value;
}
