$include <stdio.hsh>

func main() {
    new file = fopen("/tmp/hitsimple-handle-cli.tmp", "w")
    new copy as handle = file
    new same as bool = file == copy
    print(file as handle)
    new status[4] = fclose(copy)
    return same
}
