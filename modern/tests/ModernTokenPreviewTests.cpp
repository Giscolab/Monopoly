#include "ModernTokenPreview.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string_view>
#include <thread>

namespace
{
    int failures{};
    void expect(bool value, std::string_view message)
    {
        std::cout << (value ? "[PASS] " : "[FAIL] ") << message << '\n';
        failures += !value;
    }
    void word(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    { for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(std::uint8_t(value >> shift)); }
    void chunk(std::vector<std::uint8_t>& output, std::string_view type,
        const std::vector<std::uint8_t>& payload)
    {
        word(output, static_cast<std::uint32_t>(payload.size()));
        const auto start = output.size();
        output.insert(output.end(), type.begin(), type.end());
        output.insert(output.end(), payload.begin(), payload.end());
        std::uint32_t crc = ~0U;
        for (auto index = start; index < output.size(); ++index)
        {
            crc ^= output[index];
            for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
        word(output, ~crc);
    }
    // Independent PNG fixture writer: stored DEFLATE blocks, no production
    // decoder helpers. Pixel 0 transparent, pixel 1 fractional, others opaque.
    std::vector<std::uint8_t> png(unsigned width = 768, unsigned height = 640, int alpha = -1)
    {
        std::vector<std::uint8_t> raw;
        for (unsigned y = 0; y < height; ++y)
        {
            raw.push_back(0);
            for (unsigned x = 0; x < width; ++x)
            { raw.insert(raw.end(), {17, 51, 89, std::uint8_t(alpha >= 0 ? alpha : (x == 0 ? 0 : x == 1 ? 127 : 255))}); }
        }
        std::vector<std::uint8_t> compressed{0x78, 0x01};
        for (std::size_t offset = 0; offset < raw.size();)
        {
            const auto count = std::min<std::size_t>(65535, raw.size() - offset);
            compressed.push_back(offset + count == raw.size() ? 1 : 0);
            const auto n = static_cast<std::uint16_t>(count);
            compressed.insert(compressed.end(), {std::uint8_t(n), std::uint8_t(n >> 8),
                std::uint8_t(~n), std::uint8_t((~n) >> 8)});
            compressed.insert(compressed.end(), raw.begin() + offset, raw.begin() + offset + count);
            offset += count;
        }
        std::uint32_t a = 1, b = 0;
        for (const auto byte : raw) { a = (a + byte) % 65521; b = (b + a) % 65521; }
        word(compressed, (b << 16) | a);
        std::vector<std::uint8_t> output{137, 80, 78, 71, 13, 10, 26, 10}, header;
        word(header, width); word(header, height);
        header.insert(header.end(), {8, 6, 0, 0, 0});
        chunk(output, "IHDR", header); chunk(output, "IDAT", compressed); chunk(output, "IEND", {});
        return output;
    }
    void write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    void pack(const std::filesystem::path& directory, const std::vector<std::uint8_t>& bytes)
    {
        for (unsigned index = 0; index < 28; ++index)
            write(directory / (std::string{char('0' + index / 10), char('0' + index % 10)} + ".png"), bytes);
    }
    std::shared_ptr<const monopoly::data::LegacyBitmapRGBA8> waitImage(
        monopoly::menu::ModernTokenPreview& cache, std::uint8_t token, std::uint8_t frame)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        do
        {
            auto result = cache.image(token, frame);
            if (result || cache.packState(token) == monopoly::menu::ModernTokenPreview::PackState::Failed)
                return result;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        expect(false, "background decode completes within bounded test deadline");
        return {};
    }
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() /
        ("monopoly-token-preview-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); } } cleanup{root};
    using monopoly::menu::ModernTokenPreview;
    ModernTokenPreview missing(root);
    expect(!missing.image(11, 0) && !missing.image(0, 28), "out-of-range input rejected");
    expect(!missing.image(0, 0) && !missing.image(0, 255), "absent pack and thumbnail fall back");
    const auto valid = png();
    pack(root / "cannon", valid);
    write(root / "cannon/thumbnail.png", valid);
    ModernTokenPreview cache(root);
    expect(!cache.image(0, 0) && cache.packState(0) == ModernTokenPreview::PackState::Loading,
        "first request returns fallback with a pending job instead of decoded pixels");
    auto first = waitImage(cache, 0, 0);
    expect(first && first->width == 768 && first->height == 640 && first->pixels[7] == 127,
        "exact-size real PNG preserves fractional alpha and pixels");
    expect(cache.image(0, 27) && cache.image(0, 0) == first, "complete pack publishes and caches all frames");
    expect(cache.image(0, 255) && cache.image(0, 255) == cache.image(0, 255), "thumbnail independently cached");
    pack(root / "race_car", valid);
    std::filesystem::remove(root / "race_car/27.png");
    expect(!waitImage(cache, 1, 0) && cache.packState(1) == ModernTokenPreview::PackState::Failed,
        "missing last frame rejects entire pack transactionally");
    write(root / "race_car/27.png", valid);
    expect(!cache.image(1, 0), "failed pack does not retry on every animation frame");
    first.reset();
    auto reacquired = waitImage(cache, 0, 0);
    std::weak_ptr<const monopoly::data::LegacyBitmapRGBA8> retired = reacquired;
    reacquired.reset();
    pack(root / "dog", valid);
    expect(!cache.image(2, 0) && retired.expired(), "switch retires previous active pack before decoding");
    expect(bool(waitImage(cache, 2, 0)), "new selected token publishes after background completion");
    {
        ModernTokenPreview rapid(root);
        expect(!rapid.image(0, 0), "rapid selection starts with retail fallback");
        (void)rapid.image(1, 0);
        (void)rapid.image(2, 0);
        unsigned loading{};
        for (std::uint8_t token = 0; token < 11; ++token)
            loading += rapid.packState(token) == ModernTokenPreview::PackState::Loading;
        expect(loading <= 1, "rapid switches retain at most one pending decode");
        expect(bool(waitImage(rapid, 2, 27)) && rapid.packState(0) != ModernTokenPreview::PackState::Ready &&
            rapid.packState(1) != ModernTokenPreview::PackState::Ready,
            "obsolete completions never publish as the latest selected token");
        expect(bool(waitImage(rapid, 0, 0)), "discarded successful token can be decoded on reselection");
    }
    { ModernTokenPreview joining(root); (void)joining.image(0, 0); }
    expect(true, "destruction safely joins an outstanding independent decode task");
    write(root / "top_hat/thumbnail.png", {1, 2, 3});
    write(root / "iron/thumbnail.png", png(767, 640));
    write(root / "horse/thumbnail.png", png(768, 640, 255));
    write(root / "ship/thumbnail.png", png(768, 640, 0));
    write(root / "boot/thumbnail.png", std::vector<std::uint8_t>(2U * 1024U * 1024U + 1U));
    expect(!cache.image(3, 255), "malformed PNG rejected");
    expect(!cache.image(4, 255), "wrong dimensions rejected");
    expect(!cache.image(5, 255) && !cache.image(6, 255), "opaque-only and empty-alpha images rejected");
    expect(!cache.image(7, 255), "encoded byte limit enforced");
    pack(root / "thimble", valid);
    write(root / "thimble/14.png", {0});
    expect(!waitImage(cache, 8, 27) && cache.packState(8) == ModernTokenPreview::PackState::Failed,
        "malformed middle frame prevents partial publication");
    return failures ? 1 : 0;
}
