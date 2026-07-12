struct IntegerTriple {
  int first;
  int second;
  int third;
};

extern int native_integer_triplet_sum(struct IntegerTriple value);

struct IntegerTriple pass_integer_triplet(struct IntegerTriple value) {
  return value;
}

struct IntegerTriple global_integer_triplet;

int main(void) {
  global_integer_triplet.first = 10;
  global_integer_triplet.second = 20;
  global_integer_triplet.third = 12;
  return native_integer_triplet_sum(pass_integer_triplet(global_integer_triplet)) - 42;
}
