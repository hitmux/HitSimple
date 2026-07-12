extern double host_value(void);

double add(double left, double right) {
  return left + right;
}

double read_value(void) {
  return host_value();
}

int main(void) {
  double value = add(read_value(), 1.5);
  return 0;
}
