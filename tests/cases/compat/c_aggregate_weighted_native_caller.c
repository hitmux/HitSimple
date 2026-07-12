struct Weighted {
  char tag;
  double weight;
  int bonus;
};

extern struct Weighted pass_weighted(struct Weighted value);

int main(void) {
  struct Weighted input = {2, 39.0, 1};
  struct Weighted output = pass_weighted(input);
  return output.tag + (int)output.weight + output.bonus - 42;
}
