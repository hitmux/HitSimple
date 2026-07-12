struct FloatPair {
  float first;
  float second;
};

int native_float_pair_sum(struct FloatPair value) {
  return (int)value.first + (int)value.second;
}
