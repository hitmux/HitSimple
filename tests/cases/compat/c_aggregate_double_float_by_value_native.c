struct DoubleFloat {
  double first;
  float second;
};

int native_double_float_sum(struct DoubleFloat value) {
  return (int)value.first + (int)value.second;
}
