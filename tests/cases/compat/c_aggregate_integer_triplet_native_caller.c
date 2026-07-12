struct IntegerTriple {
  int first;
  int second;
  int third;
};

extern struct IntegerTriple pass_integer_triplet(struct IntegerTriple value);

int main(void) {
  struct IntegerTriple input = {10, 20, 12};
  struct IntegerTriple output = pass_integer_triplet(input);
  return output.first + output.second + output.third - 42;
}
