#pragma once
#include "LegacyBitmap.hpp"
#include "DataBanks.hpp"
#include <array>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
namespace monopoly::engine { class SequencePlayback; }
namespace monopoly::openingmovies
{
    using MovieRect = std::array<int,4>;
    using HintRasterizer = std::function<std::expected<data::LegacyBitmapRGBA8,std::string>(std::string_view)>;
    // Optional decoration only: the movie aperture stays transparent, its native owner unchanged.
    [[nodiscard]] std::expected<data::LegacyBitmapRGBA8,std::string> movieBackdrop(
        MovieRect movie, const HintRasterizer& rasterize);
    class Presentation final
    {
    public:
        static constexpr unsigned Priority = 1099; // OpeningMovies movie priority is1100.
        [[nodiscard]] std::expected<void,std::string> sync(
            std::optional<MovieRect> movie, const HintRasterizer& rasterize, engine::SequencePlayback& playback);
        [[nodiscard]] std::expected<void,std::string> reset(engine::SequencePlayback& playback);
        [[nodiscard]] bool active() const noexcept { return surface_.has_value(); }
    private:
        std::optional<data::DataId> surface_;
        std::optional<MovieRect> rectangle_;
    };
}
