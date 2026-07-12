typedef int Row[3];
typedef int *Slots[2];

Row row;
Slots slots;
int value = 40;

int main(void) {
  row[1] = 2;
  slots[0] = &value;
  return row[1] + *slots[0] - 42;
}
