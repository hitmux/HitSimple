$include <stdio.hsh>

func main() {
    new format[5] as cstr = "%4f\n"
    new value as f32 = 1.5
    new written[4] = printf(format, value)
    if (written == 0) {
        return 1
    }
    return 0
}
