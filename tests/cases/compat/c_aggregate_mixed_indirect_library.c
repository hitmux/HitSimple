struct Weighted {
  int tag;
  double weight;
  int bonus;
};

struct Pair {
  int left;
  int right;
};

extern void observe_adjustment(struct Pair adjustment, int scalar);

struct Weighted hsc_transform(struct Weighted source,
                              struct Pair adjustment,
                              int scalar) {
  observe_adjustment(adjustment, scalar);
  return source;
}
