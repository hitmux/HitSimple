$include <stdio.hsh>

func close_alias(file as handle) -> i32 {
    return fclose(file)
}

func main() -> i32 {
    new file as handle = fopen("checked-handle.tmp", "w+")
    if (!file) { return 1; }
    fprintf(file, "%d", close_alias(file))
    return 99
}
