#include "StatsDeedFloaterPlayback.hpp"

#include "IBarLayout.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace monopoly::statsui
{
    namespace
    {
        [[nodiscard]] constexpr data::DataId mainId(data::DataTag tag) noexcept
        {
            return data::packDataId(data::LegacyGroupId::Main, tag);
        }

        [[nodiscard]] constexpr data::DataId languageId(data::DataTag tag) noexcept
        {
            return data::packDataId(data::LegacyGroupId::LanguageGraphics, tag);
        }

        [[nodiscard]] bool contains(const DeedPlayback::Published& item,
            int x, int y) noexcept
        {
            return x >= item.x && x < item.x + DeedCardHitWidth &&
                y >= item.y && y < item.y + DeedCardHitHeight;
        }
        [[nodiscard]] data::DataId floaterDeedId(int square, int city) noexcept
        {
            const int property = ibar::layout::propertyIndex(square);
            if (property < 0) return data::EmptyDataId;
            const int safeCity = std::max(city, 0);
            return languageId(static_cast<data::DataTag>(
                DeedFloaterCardBaseTag + property +
                DeedFloaterCardsPerCity * safeCity));
        }
    }

    std::expected<void, std::string> DeedFloaterPlayback::sync(
        const State& state, const rules::GameState& gameState,
        const PlayerPlaybackInputs& inputs, int city,
        display::Screen2D desiredView,
        engine::SequencePlayback& playback, bool deedPopupVisible)
    {
        data::DataId desiredDeed = data::EmptyDataId;
        int desiredFrameX = 0;
        int desiredDeedX = 0;
        // UDStats.cpp: normal hover is disabled while IsPopUpIDOn.
        if (!deedPopupVisible && desiredView == display::Screen2D::Portfolio &&
            state.screen == Screen::Deed && state.mouseKnown)
        {
            auto grid = planDeedGrid(state, gameState, inputs);
            if (!grid) return std::unexpected(grid.error());
            for (const auto& item : *grid)
            {
                if (!contains(item, state.mouseX, state.mouseY)) continue;
                desiredDeed = floaterDeedId(item.square, city);
                if (desiredDeed == data::EmptyDataId)
                    return std::unexpected("UDStats Deed floater square has no large deed");
                const bool showLeft = state.mouseX > 400;
                desiredFrameX = showLeft ? 10 : 410;
                desiredDeedX = showLeft ? 203 : 603;
                break;
            }
        }

        if (desiredDeed == currentDeed_ &&
            desiredFrameX == currentFrameX_ && desiredDeedX == currentDeedX_)
            return {};
        const auto frameId = mainId(DeedFloaterFrameTag);
        std::shared_ptr<const sequence::SequenceProgram> frameProgram;
        std::shared_ptr<const sequence::SequenceProgram> deedProgram;
        if (desiredDeed != data::EmptyDataId)
        {
            auto frame = sequence::SequenceProgram::load(
                playback.resources(), frameId);
            if (!frame)
                return std::unexpected("UDStats Deed floater frame failed: " +
                    frame.error().detail);
            frameProgram = std::move(*frame);

            auto deed = sequence::SequenceProgram::load(
                playback.resources(), desiredDeed);
            if (!deed)
                return std::unexpected("UDStats Deed floater deed failed: " +
                    deed.error().detail);
            deedProgram = std::move(*deed);
        }
        std::vector<sequence::SequenceCommand> commands;
        if (currentDeed_ != data::EmptyDataId)
        {
            commands.push_back(sequence::StopSequenceCommand{
                frameId, DeedFloaterPriority, false});
            commands.push_back(sequence::StopSequenceCommand{
                currentDeed_, DeedFloaterCardPriority, false});
        }

        if (desiredDeed != data::EmptyDataId)
        {
            commands.push_back(sequence::StartSequenceCommand{
                std::move(frameProgram), DeedFloaterPriority, {},
                sequence::moveXYTransform(desiredFrameX, 220)});
            commands.push_back(sequence::StartSequenceCommand{
                std::move(deedProgram), DeedFloaterCardPriority, {},
                sequence::moveXYTransform(desiredDeedX, 225)});
        }
        if (commands.size() > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit UDStats Deed floater transition");
        }
        for (auto& command : commands)
        {
            const auto queued = std::visit(
                [&](auto value)
                {
                    return playback.commands().enqueue(std::move(value));
                }, std::move(command));
            if (!queued)
                return std::unexpected("validated UDStats Deed floater command rejected");
        }

        currentDeed_ = desiredDeed;
        currentFrameX_ = desiredFrameX;
        currentDeedX_ = desiredDeedX;
        return {};
    }
}
