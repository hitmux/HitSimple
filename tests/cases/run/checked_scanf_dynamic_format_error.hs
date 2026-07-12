$include <stdio.hsh>

func main() {
    new format[3] as cstr = "%q"
    new count[4]
    new value[4]
    count, value = scanf(format)
    return count
}
