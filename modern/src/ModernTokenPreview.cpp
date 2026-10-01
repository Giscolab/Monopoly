#include "ModernTokenPreview.hpp"
#include "ModernImageDecoder.hpp"
#include <fstream>

namespace monopoly::menu
{
    ModernTokenPreview::PackState ModernTokenPreview::packState(std::uint8_t token) const noexcept
    {
        if (token >= data::ModernTokenCount || failedPacks_[token]) return PackState::Failed;
        if (activeToken_ == token) return PackState::Ready;
        if (pendingToken_ == token) return PackState::Loading;
        return PackState::Unloaded;
    }

    ModernTokenPreview::PackResult ModernTokenPreview::loadPack(std::filesystem::path directory) noexcept
    {
        try
        {
            PackResult result;
            for (unsigned index = 0; index < result.frames.size(); ++index)
            {
                const std::string name{char('0' + index / 10), char('0' + index % 10)};
                result.frames[index] = decode(directory / (name + ".png"));
                if (!result.frames[index]) return {};
            }
            result.complete = true;
            return result;
        }
        catch (...) { return {}; }
    }

    std::shared_ptr<const data::LegacyBitmapRGBA8>
    ModernTokenPreview::decode(const std::filesystem::path& path)
    {
        constexpr std::size_t MaximumEncodedBytes = 2U * 1024U * 1024U;
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) return {};
        const auto size = input.tellg();
        if (size <= 0 || size > static_cast<std::streamoff>(MaximumEncodedBytes)) return {};
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return {};
        const auto decoded = data::decodeModernImage(bytes, data::ModernImageEncoding::Png,
            {MaximumEncodedBytes, 768, 768U * 640U * 4U});
        if (!decoded || (*decoded)->width != 768 || (*decoded)->height != 640 ||
            (*decoded)->rgba.size() != 768U * 640U * 4U) return {};
        bool transparent{}, visible{};
        for (std::size_t index = 3; index < (*decoded)->rgba.size(); index += 4)
        {
            transparent |= (*decoded)->rgba[index] == 0;
            visible |= (*decoded)->rgba[index] != 0;
        }
        if (!transparent || !visible) return {};
        return std::make_shared<const data::LegacyBitmapRGBA8>(
            data::LegacyBitmapRGBA8{768, 640, (*decoded)->rgba});
    }

    std::shared_ptr<const data::LegacyBitmapRGBA8>
    ModernTokenPreview::image(std::uint8_t token, std::uint8_t frame) noexcept
    {
        if (token >= data::ModernTokenCount || (frame >= 28 && frame != 255)) return {};
        try
        {
            const auto directory = root_ / data::modernTokenDefinitions()[token].slug;
            if (frame == 255)
            {
                if (!attemptedThumbnails_[token])
                {
                    attemptedThumbnails_[token] = true;
                    thumbnails_[token] = decode(directory / "thumbnail.png");
                }
                return thumbnails_[token];
            }
            if (pending_.valid())
            {
                if (pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return {};
                const auto completedToken = *pendingToken_;
                PackResult completed;
                try { completed = pending_.get(); }
                catch (...) { /* An actual failed job qualifies only its own token. */ }
                pendingToken_.reset();
                if (!completed.complete) failedPacks_[completedToken] = true;
                else if (completedToken == token)
                {
                    frames_ = std::move(completed.frames);
                    activeToken_ = token;
                }
                // A successful obsolete pack is destroyed before another job.
            }
            if (failedPacks_[token]) return {};
            if (activeToken_ == token) return frames_[frame];
            // Retire the previous pack before allocating: even a switch cannot
            // temporarily retain two complete 28-frame packs inside this cache.
            frames_ = {};
            activeToken_.reset();
            pending_ = std::async(std::launch::async, &ModernTokenPreview::loadPack, directory);
            pendingToken_ = token;
            return {};
        }
        catch (...)
        {
            if (frame == 255) attemptedThumbnails_[token] = true;
            else failedPacks_[token] = true;
            return {};
        }
    }
}
