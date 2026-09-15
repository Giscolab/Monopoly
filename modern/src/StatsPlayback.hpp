#pragma once

#include "StatsUI.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace monopoly::statsui
{
    enum class ButtonVisual : std::uint8_t
    {
        Off = 0,
        Idle,
        Return,
        Press
    };

    inline constexpr std::uint16_t StatusBackgroundPriority = 10;
    inline constexpr std::uint16_t DeedBackgroundPriority = 20;
    inline constexpr std::uint16_t BankBackgroundPriority = 50;
    inline constexpr std::uint16_t StatusBarPriority = 54;
    inline constexpr std::uint16_t StatusBarOverlayPriority = 154;
    inline constexpr std::uint8_t StatusStayAtEnd = 2;

    inline constexpr data::DataTag StatusBackgroundTag = 0x0089;
    inline constexpr data::DataTag DeedBackgroundTag = 0x00CD;
    inline constexpr std::array<data::DataTag, 4> BankBackgroundTags{
        0x000C, 0x000E, 0x000D, 0x000B};
    inline constexpr std::array<data::DataTag, 2> CategoryBarTags{
        0x029B, 0x029C};
    inline constexpr std::array<std::array<data::DataTag, 2>, 3> SortBarTags{{
        {0x0286, 0x0287},
        {0x022C, 0x022D},
        {0x0006, 0x0007}}};

    inline constexpr std::array<std::array<data::DataTag, 3>, 3> CategoryButtonTags{{
        {0x019E, 0x01A0, 0x019F},
        {0x017F, 0x0181, 0x0180},
        {0x0176, 0x0178, 0x0177}}};
    inline constexpr std::array<std::uint16_t, 3> CategoryIdlePriorities{56, 58, 60};
    inline constexpr std::array<std::uint16_t, 3> CategoryPressPriorities{57, 59, 61};

    inline constexpr std::array<std::array<std::array<data::DataTag, 3>, 4>, 3>
        SortButtonTags{{
            {{{0x01A4,0x01A6,0x01A5},{0x0198,0x019A,0x0199},
              {0x0195,0x0197,0x0196},{0x017C,0x017E,0x017D}}},
            {{{0x01A1,0x01A3,0x01A2},{0x019B,0x019D,0x019C},
              {0x0179,0x017B,0x017A},{0x0182,0x0184,0x0183}}},
            {{{0x00FE,0x00FD,0x00FF},{0x0104,0x0103,0x0105},
              {0x0101,0x0100,0x0102},{0x00FB,0x00FA,0x00FC}}}
        }};
    inline constexpr std::array<std::uint16_t, 4> SortIdlePriorities{62, 60, 58, 56};
    inline constexpr std::array<std::uint16_t, 4> SortPressPriorities{63, 61, 59, 57};

    [[nodiscard]] constexpr data::DataId languageSequence(data::DataTag tag) noexcept
    {
        return data::packDataId(data::LegacyGroupId::LanguageGraphics, tag);
    }
    [[nodiscard]] constexpr data::DataId mainSequence(data::DataTag tag) noexcept
    {
        return data::packDataId(data::LegacyGroupId::Main, tag);
    }

    class Playback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state, display::Screen2D desiredView,
            engine::SequencePlayback& playback);
        void reset() noexcept;
        [[nodiscard]] bool visible() const noexcept { return visible_; }
        [[nodiscard]] ButtonVisual categoryVisual(std::size_t index) const noexcept;
        [[nodiscard]] ButtonVisual sortVisual(std::size_t index) const noexcept;

    private:
        struct Published
        {
            data::DataId id{data::EmptyDataId};
            std::uint16_t priority{};
            int x{};
            int y{};
            bool moved{};
            bool stay{};
            friend bool operator==(const Published&, const Published&) = default;
        };

        static constexpr std::size_t SlotCount = 13;
        std::array<std::optional<Published>, SlotCount> published_{};
        std::array<ButtonVisual, 3> categoryVisual_{};
        std::array<ButtonVisual, 4> sortVisual_{};
        bool visible_{};
        Screen screen_{Screen::Player};
        std::uint8_t sort_{};
    };
}