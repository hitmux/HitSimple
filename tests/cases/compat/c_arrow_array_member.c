struct Buffer {
  char tag;
  int items[3];
};

int first(int *items) {
  return items[0];
}

int exercise(struct Buffer *pointer) {
  pointer->items[0] = 19;
  pointer->items[1] = 23;
  pointer->items[2] = first(pointer->items);
  return pointer->items[0] + pointer->items[1] + pointer->items[2] - 61;
}

int main(void) {
  struct Buffer buffer;
  return exercise(&buffer);
}
