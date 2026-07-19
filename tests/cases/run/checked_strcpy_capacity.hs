$include <string.hsh>

func main() -> i32 {
    new destination[2] as cstr
    new copied as addr = strcpy(destination, "AB")
    return 0
}
