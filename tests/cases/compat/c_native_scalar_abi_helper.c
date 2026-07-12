struct Pair {
  int left;
  int right;
};

int external_seed = 7;

int native_int(int value) {
  return value + 2;
}

double native_double(double value) {
  return value + 22.0;
}

int native_increment(int *value) {
  *value += 2;
  return *value;
}

void native_store(int *value, int replacement) {
  *value = replacement;
}

int native_array_sum(int values[3]) {
  return values[0] + values[1] + values[2];
}

int native_pair_sum(struct Pair *pair) {
  return pair->left + pair->right;
}
