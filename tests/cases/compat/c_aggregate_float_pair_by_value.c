struct FloatPair {
  float first;
  float second;
};

extern int native_float_pair_sum(struct FloatPair value);

struct FloatPair pass_float_pair(struct FloatPair value) {
  return value;
}

struct FloatPair global_float_pair;

int main(void) {
  global_float_pair.first = 20.0f;
  global_float_pair.second = 22.0f;
  return native_float_pair_sum(pass_float_pair(global_float_pair)) - 42;
}
