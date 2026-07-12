struct FloatTriple {
  float first;
  float second;
  float third;
};

extern int native_float_triplet_sum(struct FloatTriple value);

struct FloatTriple pass_float_triplet(struct FloatTriple value) {
  return value;
}

struct FloatTriple global_float_triplet;

int main(void) {
  global_float_triplet.first = 10.0f;
  global_float_triplet.second = 20.0f;
  global_float_triplet.third = 12.0f;
  return native_float_triplet_sum(pass_float_triplet(global_float_triplet)) - 42;
}
