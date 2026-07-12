struct Mix {
  int code;
  double weight;
};

extern int native_mix_sum(struct Mix value);

struct Mix pass_mix(struct Mix value) {
  return value;
}

struct Mix global_mix;

int main(void) {
  global_mix.code = 3;
  global_mix.weight = 39.0;
  return native_mix_sum(pass_mix(global_mix)) - 42;
}
