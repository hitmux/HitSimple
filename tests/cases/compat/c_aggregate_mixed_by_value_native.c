struct Mix {
  int code;
  double weight;
};

int native_mix_sum(struct Mix value) {
  return value.code + (int)value.weight;
}
