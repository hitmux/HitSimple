struct Weighted {
  int tag;
  double weight;
  int bonus;
};

struct Pair {
  int left;
  int right;
};

extern struct Weighted native_transform(struct Weighted source,
                                        struct Pair adjustment,
                                        int scalar);
extern int native_weighted_sum(struct Weighted value);

struct Weighted global_source;
struct Pair global_adjustment;

int main(void) {
  global_source.tag = 2;
  global_source.weight = 30.0;
  global_source.bonus = 3;
  global_adjustment.left = 4;
  global_adjustment.right = 5;

  return native_weighted_sum(native_transform(global_source, global_adjustment, 1)) - 46;
}
