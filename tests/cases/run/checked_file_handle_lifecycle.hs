$include <stdio.hsh>

func alias(file as handle) -> handle {
    return file
}

func main() -> i32 {
    new missing as handle = fopen("missing-handle-directory/file", "r")
    if (missing) { return 1; }
    new iteration as i32 = 0
    while (iteration < 128) {
        new file as handle = fopen("checked-handle.tmp", "w+")
        if (!file) { return 2; }
        new copy as handle = alias(file)
        if (file != copy) { return 3; }
        new written as i32 = fprintf(copy, "%d", 42)
        if (written != 2) { return 4; }
        if (fflush(file) != 0) { return 5; }
        if (ftell(copy) != 2) { return 6; }
        if (fseek(file, 0, 0) != 0) { return 7; }
        new value as i32 = 0
        new count as i32 = 0
        count, value = fscanf(copy, "%d")
        if (count != 1) { return 8; }
        if (value != 42) { return 9; }
        if (ferror(file) != 0) { return 10; }
        if (fclose(copy) != 0) { return 11; }
        iteration++
   ; }
    print("handle lifecycle ok")
    return 0
}
