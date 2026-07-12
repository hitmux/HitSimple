struct Mix {
  int code;
  double weight;
};

extern struct Mix pass_mix(struct Mix value);

int main(void) {
  struct Mix input = {3, 39.0};
  struct Mix output = pass_mix(input);
  return output.code + (int)output.weight - 42;
}
