$include <stdio.hsh>

template Marker {
    value[1]
}

impl Marker {
    op format(value as Marker, out as addr) -> [4] {
        new written = fput(out as handle, 'E')
        return written
    }
}

func main() {
    new file = fopen("user_template_format_fprintf_expr.out", "w")
    new value as Marker
    new written = fprintf(file, value as Marker)
    new closed = fclose(file)
    return written - 1
}
