$include <string.hsh>

func main() -> i32 {
    new destination[3] as cstr = "A"
    new appended as addr = strcat(destination, "BC")
    return 0
}
