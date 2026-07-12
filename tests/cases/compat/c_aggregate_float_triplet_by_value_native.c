struct FloatTriple {
  float first;
  float second;
  float third;
};

int native_float_triplet_sum(struct FloatTriple value) {
  return (int)value.first + (int)value.second + (int)value.third;
}
