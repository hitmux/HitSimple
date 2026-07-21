#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace {

struct MandelbrotConfig {
    std::uint64_t width;
    std::uint64_t height;
    std::uint64_t max_iterations;
};

constexpr MandelbrotConfig kMandelbrotConfig{1600, 1200, 1000};
constexpr std::uint64_t kMemoryElements = 16ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMemoryRounds = 56;
constexpr std::uint64_t kSeedMultiplier = 1664525ULL;
constexpr std::uint64_t kSeedAddend = 1013904223ULL;
constexpr std::uint64_t kTransformMultiplier = 1103515245ULL;
constexpr std::uint64_t kTransformAddend = 12345ULL;
constexpr std::uint64_t kIndexMultiplier = 2654435761ULL;

std::uint64_t mandelbrot_iterations(double cr, double ci, std::uint64_t max_iterations) {
    double zr = 0.0;
    double zi = 0.0;
    std::uint64_t iterations = 0;

    while (zr * zr + zi * zi <= 4.0 && iterations < max_iterations) {
        const double next_zr = zr * zr - zi * zi + cr;
        zi = 2.0 * zr * zi + ci;
        zr = next_zr;
        ++iterations;
    }

    return iterations;
}

std::uint64_t mandelbrot_checksum(const MandelbrotConfig config) {
    std::uint64_t checksum = 0;

    for (std::uint64_t y = 0; y < config.height; ++y) {
        const double ci = (static_cast<double>(y) * 2.0 / static_cast<double>(config.height)) - 1.0;

        for (std::uint64_t x = 0; x < config.width; ++x) {
            const double cr = (static_cast<double>(x) * 3.5 / static_cast<double>(config.width)) - 2.5;
            const std::uint64_t iterations = mandelbrot_iterations(cr, ci, config.max_iterations);
            checksum = checksum * 1315423911ULL + iterations;
        }
    }

    return checksum;
}

std::uint64_t memory_checksum(std::uint64_t elements, std::uint64_t rounds) {
    std::unique_ptr<std::uint64_t[]> data{new std::uint64_t[elements]};

    for (std::uint64_t index = 0; index < elements; ++index) {
        data[index] = index * kSeedMultiplier + kSeedAddend;
    }

    for (std::uint64_t round = 0; round < rounds; ++round) {
        for (std::uint64_t index = 0; index < elements; ++index) {
            std::uint64_t value = data[index];
            value = (value ^ (value >> 17)) * kTransformMultiplier + kTransformAddend;
            data[index] = value;
        }
    }

    std::uint64_t checksum = 0;
    for (std::uint64_t index = 0; index < elements; ++index) {
        checksum += data[index] ^ (index * kIndexMultiplier);
    }

    return checksum;
}

bool run_sanity_checks() {
    return mandelbrot_iterations(0.0, 0.0, 32) == 32 &&
           mandelbrot_iterations(2.0, 0.0, 32) == 2 &&
           mandelbrot_iterations(-1.0, 0.0, 32) == 32 &&
           memory_checksum(8, 1) == 9002283028674721401ULL;
}

void print_checksum(std::string_view name, std::uint64_t checksum) {
    std::cout << name << "_checksum_u64=" << checksum << '\n';
    std::cout << name << "_checksum_i64=" << std::bit_cast<std::int64_t>(checksum) << '\n';
}

int run_check() {
    if (!run_sanity_checks()) {
        std::cerr << "sanity_checks=failed\n";
        std::cerr << "small_memory_checksum=" << memory_checksum(8, 1) << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "sanity_checks=passed\n";
    print_checksum("mandelbrot", mandelbrot_checksum(kMandelbrotConfig));
    print_checksum("memory", memory_checksum(kMemoryElements, kMemoryRounds));
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string_view mode = argc == 2 ? argv[1] : "check";

    if (mode == "check") {
        return run_check();
    }
    if (mode == "mandelbrot") {
        print_checksum("mandelbrot", mandelbrot_checksum(kMandelbrotConfig));
        return EXIT_SUCCESS;
    }
    if (mode == "memory") {
        print_checksum("memory", memory_checksum(kMemoryElements, kMemoryRounds));
        return EXIT_SUCCESS;
    }

    std::cerr << "usage: " << argv[0] << " [check|mandelbrot|memory]\n";
    return EXIT_FAILURE;
}
