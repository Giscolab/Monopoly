#include "AuctionTextPlayback.hpp"

#include "AuctionPlayback.hpp"
#include "FontRuntime.hpp"
#include "LanguageResources.hpp"
#include "MoneyFormat.hpp"
#include "RuntimeBitmapSurface.hpp"
#include "SequenceTransforms.hpp"

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

namespace monopoly::auctionui
{
    namespace
    {
        constexpr std::uint32_t CurrentBidMessageId = 2000;
        constexpr std::uint32_t White = 0x00FFFFFFU;
        constexpr std::uint32_t Black = 0x00000000U;
        constexpr std::uint16_t TextPriorityOffset = 2;
        constexpr std::int32_t NameTextWidth = BackdropSmallWidth;
        constexpr std::int32_t NameTextHeight = 30;
        constexpr std::int32_t NameTextY = BackdropY + 10;
        constexpr std::int32_t CashTextWidth = BackdropSmallWidth;
        constexpr std::int32_t CashTextHeight = 30;
        constexpr std::int32_t CashTextY = BackdropY + 55;
        constexpr std::int32_t CurrentBidWidth = 200;
        constexpr std::int32_t CurrentBidHeight = 50;
        constexpr std::int32_t CurrentBidX = 20;
        constexpr std::int32_t CurrentBidY = 280;

        [[nodiscard]] data::LegacyBitmapRGBA8 blankImage(
            std::uint32_t width, std::uint32_t height)
        {
            data::LegacyBitmapRGBA8 result{width, height, {}};
            result.pixels.assign(static_cast<std::size_t>(width) * height * 4U, 0U);
            return result;
        }

        [[nodiscard]] std::expected<void, std::string> drawText(
            data::LegacyBitmapRGBA8& destination,
            fonts::Runtime& fontRuntime,
            std::string_view text,
            std::uint32_t colour,
            int x, int y)
        {
            if (text.empty()) return {};
            const auto rendered = fontRuntime.render(text, colour);
            if (!rendered) return std::unexpected(rendered.error().detail);
            const auto blitted = data::blitStraightRGBA8(
                destination, *rendered, x, y, data::BitmapBlitMode::SourceOver);
            if (!blitted) return std::unexpected(blitted.error());
            return {};
        }

        [[nodiscard]] std::expected<void, std::string> drawCentered(
            data::LegacyBitmapRGBA8& destination,
            fonts::Runtime& fontRuntime,
            std::string_view text,
            std::uint32_t colour,
            int y)
        {
            if (text.empty()) return {};
            const auto metrics = fontRuntime.measure(text);
            if (!metrics) return std::unexpected(metrics.error().detail);
            const int x = (static_cast<int>(destination.width) - metrics->width) / 2;
            return drawText(destination, fontRuntime, text, colour, x, y);
        }

        [[nodiscard]] std::expected<std::u16string, std::string> languageText(
            const engine::SequencePlayback& playback, std::uint32_t messageId)
        {
            const auto resources = playback.resources();
            if (!resources) return std::unexpected("auction text has no resource snapshot");
            const auto language = resources->language();
            if (!language || !language->catalog)
                return std::unexpected("auction text has no language catalog");
            const auto text = language->catalog->message(messageId);
            if (!text) return std::unexpected(text.error().detail);
            return **text;
        }
    }

    std::expected<void, std::string> TextPlayback::ensureSurfaces(
        engine::SequencePlayback& playback)
    {
        auto ensure = [&](std::optional<data::DataId>& target,
                          std::uint32_t width, std::uint32_t height)
            -> std::expected<void, std::string>
        {
            if (target) return {};
            const auto created = playback.runtimeBitmaps().create(width, height, true);
            if (!created) return std::unexpected(created.error());
            target = *created;
            return {};
        };

        if (auto result = ensure(currentBidSurface_, CurrentBidWidth, CurrentBidHeight); !result)
            return result;
        for (std::size_t player = 0; player < rules::MaxPlayers; ++player)
        {
            if (auto result = ensure(nameSurfaces_[player], NameTextWidth, NameTextHeight); !result)
                return result;
            if (auto result = ensure(bidSurfaces_[player], AuctionBidTextWidth, AuctionBidTextHeight); !result)
                return result;
            if (auto result = ensure(cashSurfaces_[player], CashTextWidth, CashTextHeight); !result)
                return result;
        }
        return {};
    }

    std::expected<void, std::string> TextPlayback::sync(
        const State& state,
        const rules::GameState& gameState,
        display::Screen2D desiredView,
        int monetarySystem,
        fonts::Runtime* fontRuntime,
        engine::SequencePlayback& playback)
    {
        std::vector<Published> desired;
        if (desiredView != display::Screen2D::Auction)
        {
            if (currentObjects_.empty()) return {};
        }
        else
        {
            if (gameState.numberOfPlayers > rules::MaxPlayers)
                return std::unexpected("auction text player count exceeds retail maximum");
            if (fontRuntime == nullptr || !fontRuntime->ready())
                return std::unexpected("auction text font runtime is unavailable");
            if (const auto ready = ensureSurfaces(playback); !ready)
                return ready;

            const auto resources = playback.resources();
            if (!resources) return std::unexpected("auction text has no resources");
            const auto edition = resources->context().board;

            if (!currentBidLabel_)
            {
                const auto label = languageText(playback, CurrentBidMessageId);
                if (!label) return std::unexpected(label.error());
                currentBidLabel_ = *label;
            }
            const auto amount = money::format(
                state.highestBid, monetarySystem, true, edition);
            if (!amount) return std::unexpected(amount.error());
            const auto currentBidLabelUtf8 =
                fonts::transcodeUtf8(std::u16string_view(*currentBidLabel_));
            if (!currentBidLabelUtf8)
                return std::unexpected(currentBidLabelUtf8.error().detail);
            const std::string currentKey = *currentBidLabelUtf8 + '\n' + *amount;
            if (!currentBidCache_ || *currentBidCache_ != currentKey)
            {
                auto image = blankImage(CurrentBidWidth, CurrentBidHeight);
                if (auto drawn = drawText(image, *fontRuntime,
                        *currentBidLabelUtf8, White, 10, 5); !drawn)
                    return drawn;
                if (auto drawn = drawText(image, *fontRuntime,
                        *amount, White, 10, 30); !drawn)
                    return drawn;
                if (auto updated = playback.runtimeBitmaps().update(
                        *currentBidSurface_, std::move(image)); !updated)
                    return updated;
                currentBidCache_ = currentKey;
            }
            desired.push_back({*currentBidSurface_,
                static_cast<std::uint16_t>(AuctionPropertyPriority + 1),
                CurrentBidX, CurrentBidY});

            const auto count = std::min<rules::PlayerNumber>(
                gameState.numberOfPlayers, rules::MaxPlayers);
            for (rules::PlayerNumber player = 0; player < count; ++player)
            {
                if (!playerPanelWanted(state, count, desiredView, player)) continue;
                const auto index = static_cast<std::size_t>(player);
                const auto priority = static_cast<std::uint16_t>(
                    AuctionBasePriority + player + TextPriorityOffset);
                const int center = state.backdropCenterX[index];

                const auto encodedName = fonts::transcodeUtf8(
                    std::wstring_view(gameState.players[index].name));
                if (!encodedName) return std::unexpected(encodedName.error().detail);
                const auto& name = *encodedName;
                if (!nameCache_[index] || *nameCache_[index] != name)
                {
                    auto image = blankImage(NameTextWidth, NameTextHeight);
                    if (auto drawn = drawCentered(image, *fontRuntime, name, Black, 0); !drawn)
                        return drawn;
                    if (auto updated = playback.runtimeBitmaps().update(
                            *nameSurfaces_[index], std::move(image)); !updated)
                        return updated;
                    nameCache_[index] = name;
                }

                const auto bid = money::format(
                    state.bids[index], monetarySystem, false, edition);
                if (!bid) return std::unexpected(bid.error());
                if (!bidCache_[index] || *bidCache_[index] != *bid)
                {
                    auto image = blankImage(AuctionBidTextWidth, AuctionBidTextHeight);
                    if (auto drawn = drawCentered(image, *fontRuntime, *bid, White, 5); !drawn)
                        return drawn;
                    if (auto updated = playback.runtimeBitmaps().update(
                            *bidSurfaces_[index], std::move(image)); !updated)
                        return updated;
                    bidCache_[index] = *bid;
                }

                const auto cash = money::format(
                    gameState.players[index].cash - state.bids[index],
                    monetarySystem, true, edition);
                if (!cash) return std::unexpected(cash.error());
                if (!cashCache_[index] || *cashCache_[index] != *cash)
                {
                    auto image = blankImage(CashTextWidth, CashTextHeight);
                    if (auto drawn = drawCentered(image, *fontRuntime, *cash, Black, 5); !drawn)
                        return drawn;
                    if (auto updated = playback.runtimeBitmaps().update(
                            *cashSurfaces_[index], std::move(image)); !updated)
                        return updated;
                    cashCache_[index] = *cash;
                }

                desired.push_back({*nameSurfaces_[index], priority,
                    center - NameTextWidth / 2, NameTextY});
                desired.push_back({*bidSurfaces_[index], priority,
                    center - AuctionBidTextWidth / 2, AuctionBidTextY});
                desired.push_back({*cashSurfaces_[index], priority,
                    center - CashTextWidth / 2, CashTextY});
            }
        }

        if (desired == currentObjects_) return {};

        std::vector<std::shared_ptr<const sequence::SequenceProgram>> programs;
        programs.reserve(desired.size());
        for (const auto& object : desired)
        {
            auto program = sequence::SequenceProgram::rawBitmap(
                object.id, data::LegacyDataType::Native);
            if (!program) return std::unexpected(program.error().detail);
            programs.push_back(std::move(*program));
        }

        const std::size_t required = currentObjects_.size() + desired.size();
        if (required > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
            return std::unexpected("sequence command queue cannot fit auction text transition");

        for (const auto& object : currentObjects_)
        {
            if (!playback.commands().enqueue(sequence::StopSequenceCommand{
                    object.id, object.priority, false}))
                return std::unexpected("validated auction text stop rejected");
        }
        for (std::size_t index = 0; index < desired.size(); ++index)
        {
            const auto& object = desired[index];
            if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                    programs[index], object.priority, {},
                    sequence::moveXYTransform(object.x, object.y)}))
                return std::unexpected("validated auction text start rejected");
        }
        currentObjects_ = std::move(desired);
        return {};
    }

    void TextPlayback::reset() noexcept
    {
        currentBidSurface_.reset();
        nameSurfaces_.fill(std::nullopt);
        bidSurfaces_.fill(std::nullopt);
        cashSurfaces_.fill(std::nullopt);
        currentObjects_.clear();
        currentBidCache_.reset();
        nameCache_.fill(std::nullopt);
        bidCache_.fill(std::nullopt);
        cashCache_.fill(std::nullopt);
        currentBidLabel_.reset();
    }
}
