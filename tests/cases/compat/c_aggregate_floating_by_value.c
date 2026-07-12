struct Weighted {
  char tag;
  double weight;
  int bonus;
};

extern int native_weighted_sum(struct Weighted value);

struct Weighted pass_weighted(struct Weighted value) {
  return value;
}

struct Weighted global_weighted;

int main(void) {
  global_weighted.tag = 2;
  global_weighted.weight = 39.0;
  global_weighted.bonus = 1;
  return native_weighted_sum(pass_weighted(global_weighted)) - 42;
}
