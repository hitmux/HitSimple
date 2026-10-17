$include <string.hsh>
func main() -> i32 {
    new p as addr = strchr("abc", 'a')
    return ([1]*p) - 97
}
