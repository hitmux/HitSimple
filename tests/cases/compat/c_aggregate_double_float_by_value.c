struct DoubleFloat {
  double first;
  float second;
};

extern int native_double_float_sum(struct DoubleFloat value);

struct DoubleFloat pass_double_float(struct DoubleFloat value) {
  return value;
}

struct DoubleFloat global_double_float;

int main(void) {
  global_double_float.first = 40.0;
  global_double_float.second = 2.0f;
  return native_double_float_sum(pass_double_float(global_double_float)) - 42;
}
