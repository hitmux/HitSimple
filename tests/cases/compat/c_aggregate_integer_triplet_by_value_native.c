struct IntegerTriple {
  int first;
  int second;
  int third;
};

int native_integer_triplet_sum(struct IntegerTriple value) {
  return value.first + value.second + value.third;
}
