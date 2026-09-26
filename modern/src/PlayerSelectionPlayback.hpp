#pragma once

#include "PlayerSetupFlow.hpp"
#include "Display.hpp"
#include "SequencePlayback.hpp"
#include "PlayerSelectionHistory.hpp"

#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace monopoly::fonts { class Runtime; }

namespace monopoly::playerselection
{
    struct RenderState
    {
        ui::playersetup::State setup;
        rules::GameState game;
        display::Screen2D view{display::Screen2D::Black};
        int localPlayers{};
        int monetarySystem{13};
        std::vector<HistoryEntry> highScores;
        ui::playersetup::Button pressedButton{ui::playersetup::Button::None};
        std::uint64_t pressSerial{};
    };

    struct RuleHit
    {
        ui::playersetup::Rect rect;
        rules::options::SetupRule rule;
        std::uint8_t choice{};
    };

    namespace detail
    {
        // UDPsel.cpp owns a distinct space-only wrapper; do not route rule text
        // through FontRuntime::wrap(), which intentionally follows UDChat quirks.
        [[nodiscard]] std::expected<std::vector<std::string>, std::string> wrapRuleDescription(
            fonts::Runtime& font, std::string_view text, int maxPixelWidth, std::size_t maxLines);
    }

    // Owner-thread renderer. No sequence is substituted when a retail asset
    // is absent; sync returns the resource error and remains retryable.
    class PlayerSelectionPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const RenderState&, fonts::Runtime*, engine::SequencePlayback&);
        void reset() noexcept;
        [[nodiscard]] bool ready() const noexcept { return ready_; }
        // Retail hotspots become active when incoming objects start, not when
        // their clocks settle. Outgoing objects never remain interactive.
        [[nodiscard]] bool interactable() const noexcept { return interactable_; }
        [[nodiscard]] std::optional<ui::playersetup::Phase> takeStartedPhase() noexcept
        {
            const auto result = startedPhase_;
            startedPhase_.reset();
            return result;
        }
        [[nodiscard]] std::span<const RuleHit> ruleHits() const noexcept { return ruleHits_; }
        [[nodiscard]] ui::playersetup::Rect restoreRect() const noexcept { return restoreRect_; }
        [[nodiscard]] ui::playersetup::Rect shortRect() const noexcept { return shortRect_; }
    private:
        struct Live
        {
            data::DataId id{}, idle{}, out{};
            std::uint16_t priority{};
            int x{}, y{};
            bool animating{}, leaving{};
            bool loop{};
        };
        std::map<int, Live> live_;
        std::map<int, data::DataId> surfaces_;
        std::map<int, std::string> textCache_;
        std::vector<RuleHit> ruleHits_;
        ui::playersetup::Rect restoreRect_{}, shortRect_{};
        ui::playersetup::Phase phase_{ui::playersetup::Phase::None};
        bool ready_{};
        bool interactable_{};
        std::optional<ui::playersetup::Phase> startedPhase_;
        std::uint64_t pressSerial_{};
    };
}
