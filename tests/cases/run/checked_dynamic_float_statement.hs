$include <stdio.hsh>

func main() {
    new raw[8]
    raw %f= 1.5
    print(raw as f64)
    new format[5] as cstr = "%8f\n"
    new value as f64 = 1.5
    new file = fopen("checked_dynamic_float_statement.tmp", "w")
    fprintf(file, format, value)
    new closed[4] = fclose(file)
    return 0
}
