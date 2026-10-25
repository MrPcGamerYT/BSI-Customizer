// Synthetic timings, not a real-player performance guarantee. See docs/PERFORMANCE.md.
#include "../src/core/RpcIdScan.h"

#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

template<class F>
void measure(const char *label, F operation)
{
    const auto start = std::chrono::steady_clock::now();
    const auto checksum = operation();
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    std::printf("%s: %.2f ms (checksum %llu)\n", label, elapsed,
                static_cast<unsigned long long>(checksum));
}

int main()
{
    std::mt19937_64 random(123);
    std::vector<std::uint64_t> values(200000);
    for (auto &value : values) value = random();
    measure("constant rejection", [&] {
        std::size_t count = 0;
        for (const auto value : values) count += RpcIdScan::compilerConstant(value);
        return count;
    });

    std::string image(8 * 1024 * 1024, '\0');
    image.replace(image.size() - 64, 12, "ab12345678cd");
    const RpcIdScan::Signature signature {"test", "6162", "6364", 8};
    measure("signature relocation", [&] {
        const auto result = RpcIdScan::findSignature(image, signature);
        return result.offsets.empty() ? 0 : result.offsets[0];
    });

    std::string noise(1024 * 1024, '\0');
    for (char &byte : noise) byte = char(random());
    const std::string fixture = "discord client_id=821361671729709086";
    noise.replace(100, fixture.size(), fixture);
    measure("full discovery", [&] {
        const auto records = RpcIdScan::harvest(noise);
        std::size_t checksum = 0;
        for (const auto &candidate : records) checksum += candidate.score + candidate.refs;
        return checksum;
    });
}
