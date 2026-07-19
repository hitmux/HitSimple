$include <stdio.hsh>

func main() -> i32 {
    new file as handle = fopen("/__hitsimple_missing_file__", "r")
    return fget(file)
}
