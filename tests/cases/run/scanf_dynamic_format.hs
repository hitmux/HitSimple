$include <stdio.hsh>

func main() {
    new format[3] as cstr = "%d"
    new count[4]
    new value[4]
    count, value = scanf(format)
    if (count != 1) {
        return 1
    }
    if (value != 42) {
        return 2
    }
    return 0
}
