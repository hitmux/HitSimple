$include <stdio.hsh>

func main() -> i32 {
    new format[3] as cstr = "%s"
    new count as i32
    new value[1] as cstr
    count, value = scanf(format)
    return count
}
