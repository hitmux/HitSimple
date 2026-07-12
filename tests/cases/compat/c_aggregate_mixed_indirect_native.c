struct Weighted {
  int tag;
  double weight;
  int bonus;
};

struct Pair {
  int left;
  int right;
};

struct Weighted native_transform(struct Weighted source,
                                 struct Pair adjustment,
                                 int scalar) {
  source.tag += adjustment.left + scalar;
  source.bonus += adjustment.right + scalar;
  return source;
}

int native_weighted_sum(struct Weighted value) {
  return value.tag + (int)value.weight + value.bonus;
}
