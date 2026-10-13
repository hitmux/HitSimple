$include <stdio.hsh>

func alias(file as handle) -> handle {
    return file
}

func main() -> i32 {
    new file as handle = fopen("checked-handle.tmp", "w+")
    if (!file) { return 1; }
    new copy as handle = alias(file)
    new closed as i32 = fclose(file)
    if (closed != 0) { return 2; }
    new operation as i32 = get()
    new buffer[4] as bytes = 0x04030201
    new value as i32 = 0
    new count as i32 = 0
    if (operation == 97) { new result = fclose(copy); }
    if (operation == 98) { new result = fget(copy); }
    if (operation == 99) { new result = fput(copy, buffer); }
    if (operation == 100) { new result = fread(buffer, 1, 4, copy); }
    if (operation == 101) { new result = fwrite(buffer, 1, 4, copy); }
    if (operation == 102) { new result = fprintf(copy, "%d", value); }
    if (operation == 103) { count, value = fscanf(copy, "%d"); }
    if (operation == 104) { new result = fflush(copy); }
    if (operation == 105) { new result = fseek(copy, 0, 0); }
    if (operation == 106) { new result = ftell(copy); }
    if (operation == 107) { new result = feof(copy); }
    if (operation == 108) { new result = ferror(copy); }
    if (operation == 109) { new result = fread(buffer, 0, 4, copy); }
    if (operation == 110) { new result = fwrite(buffer, 1, 0, copy); }
    return 99
}
