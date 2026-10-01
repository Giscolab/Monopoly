#pragma once
#include "TradeUI.hpp"
#include "SequencePlayback.hpp"
#include <string>
namespace monopoly::fonts { class Runtime; }
namespace monopoly::tradeui
{
    // Missing UDTrade player chooser consumer. Selection remains owned by TradeUI.
    class PartnerSelectionPlayback final
    {
    public:
        [[nodiscard]] std::expected<void,std::string> sync(const State& state,
            const rules::GameState& game, display::Screen2D view, fonts::Runtime* font,
            engine::SequencePlayback& playback, bool modernPresentation=false);
        [[nodiscard]] std::expected<void,std::string> reset(engine::SequencePlayback& playback);
        [[nodiscard]] data::DataId surface() const noexcept { return surface_; }
        [[nodiscard]] bool visible() const noexcept { return visible_; }
    private:
        data::DataId surface_{data::EmptyDataId};
        bool visible_{};
        std::string key_;
        std::shared_ptr<const data::ResourceSnapshot> resources_;
        data::BitmapRuntimeCache bitmaps_;
    };
}
