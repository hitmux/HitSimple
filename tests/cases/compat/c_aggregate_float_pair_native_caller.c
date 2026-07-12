struct FloatPair {
  float first;
  float second;
};

extern struct FloatPair pass_float_pair(struct FloatPair value);

int main(void) {
  struct FloatPair input = {20.0f, 22.0f};
  struct FloatPair output = pass_float_pair(input);
  return (int)output.first + (int)output.second - 42;
}
