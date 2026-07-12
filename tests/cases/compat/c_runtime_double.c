double add(double left, double right) {
  return left + right;
}

int main(void) {
  double value = add(20.0, 22.0);
  if (value != 42.0) return 1;
  if (value < 42.0) return 1;
  if (value <= 41.0) return 1;
  if (value > 42.0) return 1;
  if (value >= 43.0) return 1;
  return value == 42.0 ? 0 : 1;
}
