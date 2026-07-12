struct DoubleFloat {
  double first;
  float second;
};

extern struct DoubleFloat pass_double_float(struct DoubleFloat value);

int main(void) {
  struct DoubleFloat input = {40.0, 2.0f};
  struct DoubleFloat output = pass_double_float(input);
  return (int)output.first + (int)output.second - 42;
}
