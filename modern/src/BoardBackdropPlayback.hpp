#pragma once

#include "Display.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace monopoly::boarddisplay
{
    inline constexpr std::uint16_t BoardBackdropPriority = 10;
    inline constexpr data::DataTag MainBoardBitmapBaseTag = 0x0000;
    inline constexpr data::DataTag TradeBoardBitmapBaseTag = 0x01AD;
    inline constexpr std::size_t MainBoardBufferCount = 4;
    inline constexpr std::uint32_t BoardCameraCount = 39;
    inline constexpr std::uint32_t MainBoardWidth = 800;
    inline constexpr std::uint32_t MainBoardHeight = 450;
    inline constexpr std::uint32_t TradeBoardWidth = 400;
    inline constexpr std::uint32_t TradeBoardHeight = 225;

    struct BoardBackdropInputs
    {
        display::Screen2D view{display::Screen2D::Invalid};
        bool game3DOn{true};
        pieces::BoardCameraView camera{pieces::BoardCameraView::TopDownSoccer};
        std::uint32_t tick{};
    };
    struct BoardBackdropBuffer
    {
        data::DataId surface{data::EmptyDataId};
        int viewLoaded{-1};
        std::uint32_t timeLoaded{};
    };

    class BoardBackdropPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const BoardBackdropInputs& inputs,
            engine::SequencePlayback& playback);
        void reset() noexcept;

        [[nodiscard]] data::DataId activeBackdrop() const noexcept
        { return activeBackdrop_; }
        [[nodiscard]] const std::array<BoardBackdropBuffer, MainBoardBufferCount>&
        mainBuffers() const noexcept { return mainBuffers_; }
        [[nodiscard]] std::optional<std::size_t> currentMainBuffer() const noexcept
        { return currentMainBuffer_; }
        [[nodiscard]] data::DataId tradeSurface() const noexcept
        { return tradeSurface_; }

    private:
        [[nodiscard]] std::expected<void, std::string> ensureSurfaces(
            engine::SequencePlayback& playback);
        [[nodiscard]] std::expected<data::LegacyBitmapRGBA8, std::string>
        loadBoardBitmap(data::DataId id, engine::SequencePlayback& playback) const;
        [[nodiscard]] std::expected<void, std::string> compileInto(
            data::DataId surface, data::DataId source,
            engine::SequencePlayback& playback) const;
        [[nodiscard]] std::expected<data::DataId, std::string> selectBackdrop(
            display::Screen2D view, pieces::BoardCameraView camera,
            std::uint32_t tick, engine::SequencePlayback& playback);

        std::array<BoardBackdropBuffer, MainBoardBufferCount> mainBuffers_{};
        data::DataId tradeSurface_{data::EmptyDataId};
        std::optional<std::size_t> currentMainBuffer_;
        data::DataId activeBackdrop_{data::EmptyDataId};
        display::Screen2D currentView_{display::Screen2D::Invalid};
        std::optional<pieces::BoardCameraView> currentCamera_;
        bool surfacesReady_{};
    };
}
