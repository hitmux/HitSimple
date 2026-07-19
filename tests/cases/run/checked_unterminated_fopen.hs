$include <stdio.hsh>

func main() -> i32 {
    new name[2] as cstr
    name[0] = 'x'
    name[1] = 'x'
    new file as handle = fopen(name, "r")
    return 0
}
