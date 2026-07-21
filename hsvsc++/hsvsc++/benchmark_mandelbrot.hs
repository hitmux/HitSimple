$include <stdio.hsh>
$include <stdlib.hsh>

func mandelbrot_iterations(cr[8] as f64, ci[8] as f64, max_iterations[8] as u64) -> [8] as u64 {
    new zr[8] as f64
    new zi[8] as f64
    new iterations[8] as u64 = 0
    zr %f= 0.0
    zi %f= 0.0

    while (iterations < max_iterations) {
        new zr_squared[8] as f64
        new zi_squared[8] as f64
        new magnitude_squared[8] as f64
        zr_squared %f= zr %8f* zr
        zi_squared %f= zi %8f* zi
        magnitude_squared %f= zr_squared %8f+ zi_squared
        if (magnitude_squared > 4.0) {
            break
        }

        new next_zr[8] as f64
        new zi_product[8] as f64
        new next_zi[8] as f64
        next_zr %f= zr_squared %8f- zi_squared
        next_zr %f= next_zr %8f+ cr
        zi_product %f= zr %8f* zi
        next_zi %f= 2.0 %8f* zi_product
        next_zi %f= next_zi %8f+ ci
        zi = next_zi
        zr = next_zr
        iterations++
    }

    return iterations
}

func main() -> i32 {
    new width[8] as u64 = 1600
    new height[8] as u64 = 1200
    new max_iterations[8] as u64 = 1000
    new checksum[8] as u64 = 0
    new y[8] as u64 = 0

    while (y < height) {
        new scaled_y[8] as f64
        new normalized_y[8] as f64
        new ci[8] as f64
        scaled_y %f= to_f64(y) %8f* 2.0
        normalized_y %f= scaled_y %8f/ to_f64(height)
        ci %f= normalized_y %8f- 1.0

        new x[8] as u64 = 0
        while (x < width) {
            new scaled_x[8] as f64
            new normalized_x[8] as f64
            new cr[8] as f64
            new iterations[8] as u64
            new checksum_product[8] as u64
            scaled_x %f= to_f64(x) %8f* 3.5
            normalized_x %f= scaled_x %8f/ to_f64(width)
            cr %f= normalized_x %8f- 2.5
            iterations = mandelbrot_iterations(cr, ci, max_iterations)
            checksum_product = checksum %8d* 1315423911
            checksum = checksum_product %8d+ iterations
            x++
        }

        y++
    }

    printf("mandelbrot_checksum_i64=%d\n", checksum)
    return 0
}
