#pragma once

#include "IBar.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <optional>
#include <string>

namespace monopoly::fonts { class Runtime; }

namespace monopoly::ibar
{
    // Dynamic GRAFIX objects from UDIBar: two bank cards, current name and
    // ButtonBarMessage. Static card/token playback retains its existing owner.
    class RuntimeTextPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const rules::GameState& game, const State& ui,
            const RuleProjection& projection, bool visible, bool propertyBar,
            rules::PlayerNumber activePlayer, std::uint64_t tick,
            int monetarySystem, data::BoardEdition edition,
            fonts::Runtime* font, engine::SequencePlayback& playback);
        void reset() noexcept;
        [[nodiscard]] data::DataId surface(std::size_t index) const noexcept
        { return index < surfaces_.size() ? surfaces_[index] : data::EmptyDataId; }

    private:
        std::array<data::DataId, 4> surfaces_{};
        std::array<std::string, 4> keys_{};
        std::array<bool, 4> shown_{};
        data::BitmapRuntimeCache bases_;
        std::optional<std::uint64_t> cashSerial_;
        std::optional<int> monetarySystem_;
        std::optional<data::BoardEdition> edition_;
        std::optional<std::uint64_t> wantedTick_;
        bool messagePinned_{};
        std::string message_;
        std::uint32_t messageColor_{};
    };
}
