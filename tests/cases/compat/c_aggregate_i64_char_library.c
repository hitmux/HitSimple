struct WideTag {
  long long value;
  char tag;
};

struct WideTag pass_wide_tag(struct WideTag value) {
  return value;
}
