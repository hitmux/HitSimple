struct Weighted {
  int tag;
  double weight;
  int bonus;
};

struct Pair {
  int left;
  int right;
};

extern struct Weighted hsc_transform(struct Weighted source,
                                     struct Pair adjustment,
                                     int scalar);

static int observed_left;
static int observed_right;
static int observed_scalar;

void observe_adjustment(struct Pair adjustment, int scalar) {
  observed_left = adjustment.left;
  observed_right = adjustment.right;
  observed_scalar = scalar;
}

int main(void) {
  struct Weighted source = {2, 30.0, 3};
  struct Pair adjustment = {4, 5};
  struct Weighted result = hsc_transform(source, adjustment, 1);

  if (result.tag != 2) return 1;
  if (result.weight != 30.0) return 2;
  if (result.bonus != 3) return 3;
  if (observed_left != 4) return 4;
  if (observed_right != 5) return 5;
  if (observed_scalar != 1) return 6;
  return 0;
}
