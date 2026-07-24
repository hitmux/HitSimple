struct WideTag {
  long long value;
  char tag;
};

extern struct WideTag pass_wide_tag(struct WideTag value);

int main(void) {
  struct WideTag input = {40, 2};
  struct WideTag output = pass_wide_tag(input);
  return (int)output.value + output.tag - 42;
}
