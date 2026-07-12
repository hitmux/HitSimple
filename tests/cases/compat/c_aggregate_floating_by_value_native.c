struct Weighted {
  char tag;
  double weight;
  int bonus;
};

int native_weighted_sum(struct Weighted value) {
  return value.tag + (int)value.weight + value.bonus;
}
