struct Pair {
  int left;
  int right;
};

extern int native_int(int value);
extern double native_double(double value);
extern int native_increment(int *value);
extern void native_store(int *value, int replacement);
extern int native_array_sum(int values[3]);
extern int native_pair_sum(struct Pair *pair);
extern int external_seed;

struct Pair pair;

int main(void) {
  int value = native_int(40);
  if (value != 42) return 1;

  double floating = native_double(20.0);
  if (floating != 42.0) return 2;

  native_store(&value, 40);
  if (native_increment(&value) != 42) return 3;

  int values[3];
  values[0] = 10;
  values[1] = 20;
  values[2] = 12;
  if (native_array_sum(values) != 42) return 4;

  pair.left = 20;
  pair.right = 22;
  if (native_pair_sum(&pair) != 42) return 5;
  if (external_seed != 7) return 6;
  return 0;
}
