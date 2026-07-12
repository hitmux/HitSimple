$include <stdio.hsh>

template Marker {
    value[1]
}

impl Marker {
    op format(value as Marker, out as addr) -> [4] {
        printf("printf-sink\n")
        return 12
    }
}

func main() {
    new value as Marker
    printf(value as Marker)
    return 0
}
