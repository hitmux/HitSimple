struct FloatTriple {
  float first;
  float second;
  float third;
};

extern struct FloatTriple pass_float_triplet(struct FloatTriple value);

int main(void) {
  struct FloatTriple input = {10.0f, 20.0f, 12.0f};
  struct FloatTriple output = pass_float_triplet(input);
  return (int)output.first + (int)output.second + (int)output.third - 42;
}
