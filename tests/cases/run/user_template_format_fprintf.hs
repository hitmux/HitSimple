$include <stdio.hsh>

template Marker {
    value[1]
}

impl Marker {
    op format(value as Marker, out as addr) -> [4] {
        new written = fput(out as handle, 'F')
        return written
    }
}

func main() {
    new file = fopen("user_template_format_fprintf.out", "w")
    new value as Marker
    fprintf(file, value as Marker)
    new closed = fclose(file)
    return 0
}
