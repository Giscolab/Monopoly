#include "IBarRuntimeTextPlayback.hpp"

#include "FontRuntime.hpp"
#include "MoneyFormat.hpp"

#include <algorithm>
#include <utility>

namespace monopoly::ibar
{
    namespace
    {
        constexpr std::array<std::uint16_t, 4> Priorities{256, 257, 258, 5000};
        constexpr std::array<int, 4> X{740, 750, 336, 676};
        constexpr std::array<int, 4> Y{506, 525, 530, 508};
        constexpr std::array<data::DataTag, 3> BaseTags{0x01BE, 0x01BD, 0x01B9};

        class FontGuard
        {
        public:
            explicit FontGuard(fonts::Runtime& font) : font_(font), old_(font.settings()) {}
            ~FontGuard()
            {
                (void)font_.setSize(old_.size);
                font_.setWeight(old_.weight);
                font_.setItalic(old_.italic);
                font_.setUnderline(old_.underline);
                font_.setStrikeOut(old_.strikeOut);
            }
        private:
            fonts::Runtime& font_;
            fonts::Settings old_;
        };
    }

    std::expected<void, std::string> RuntimeTextPlayback::sync(
        const rules::GameState& game, const State& ui,
        const RuleProjection& projection, bool visible, bool propertyBar,
        rules::PlayerNumber activePlayer, std::uint64_t tick,
        int monetarySystem, data::BoardEdition edition,
        fonts::Runtime* font, engine::SequencePlayback& playback)
    {
        auto nextMessage = message_;
        auto nextColor = messageColor_;
        auto nextWanted = wantedTick_;
        auto nextCash = cashSerial_;
        auto nextPinned = messagePinned_;
        if (projection.mode == RuleMode::RaiseMoney)
        {
            const auto amount = money::format(-projection.raiseCashNeeded, monetarySystem, true, edition);
            if (!amount) return std::unexpected(amount.error());
            nextMessage = *amount; nextColor = 0x004040FF; nextWanted = tick; nextPinned = true;
        }
        else if (projection.mode == RuleMode::HotelDecomposition && ui.decompositionHousesToSell)
        {
            nextMessage = std::to_string(*ui.decompositionHousesToSell);
            if (edition == data::BoardEdition::Usa) nextMessage += " houses";
            nextColor = 0x000000FF; nextWanted = tick; nextPinned = true;
        }
        else if (ui.cashAnimationAmount && (nextCash != ui.cashAnimationSerial ||
            monetarySystem_ != monetarySystem || edition_ != edition))
        {
            const auto amount = money::format(*ui.cashAnimationAmount, monetarySystem, true, edition);
            if (!amount) return std::unexpected(amount.error());
            nextMessage = *amount; nextColor = 0x00FFFFFF;
            nextWanted = ui.cashAnimationTick; nextCash = ui.cashAnimationSerial; nextPinned = false;
        }
        const std::array<bool, 4> desired{
            visible && propertyBar && activePlayer == rules::BankPlayer,
            visible && propertyBar && activePlayer == rules::BankPlayer,
            visible && game.currentPlayer < game.numberOfPlayers && game.currentPlayer < rules::MaxPlayers,
            visible && nextWanted && tick >= *nextWanted &&
                tick - *nextWanted < (nextPinned ? 1U : 240U)};
        std::size_t required{};
        for (std::size_t i = 0; i < desired.size(); ++i) required += desired[i] != shown_[i];
        if (required > sequence::SequenceCommandQueue::Capacity - playback.commands().pendingCount())
            return std::unexpected("IBar runtime text transition exceeds command capacity");

        std::array<std::optional<data::LegacyBitmapRGBA8>, 4> images;
        auto keys = keys_;
        std::array<std::string, 4> text;
        if (desired[0])
        {
            auto houses = game.options.maximumHouses;
            auto hotels = game.options.maximumHotels;
            for (const auto& square : game.squares)
                if (square.houses < game.options.housesPerHotel) houses -= square.houses;
                else --hotels;
            text[0] = std::to_string(houses); text[1] = std::to_string(hotels);
            for (std::size_t i = 0; i < 2; ++i)
                if (text[i].size() < 3) text[i].insert(0, 3 - text[i].size(), ' ');
        }
        if (desired[2])
        {
            const auto encodedName = fonts::transcodeUtf8(
                std::wstring_view(game.players[game.currentPlayer].name));
            if (!encodedName) return std::unexpected(encodedName.error().detail);
            text[2] = *encodedName;
        }
        text[3] = nextMessage;

        for (std::size_t i = 0; i < desired.size(); ++i)
        {
            if (!desired[i]) continue;
            keys[i] = text[i] + ':' + std::to_string(i == 3 ? nextColor : 0);
            if (surfaces_[i] != data::EmptyDataId && keys[i] == keys_[i]) continue;
            if (!font || !font->ready()) return std::unexpected("IBar runtime text requires a ready font");
            FontGuard guard(*font);
            if (const auto sized = font->setSize(12); !sized) return std::unexpected(sized.error().detail);
            font->setWeight(i == 3 ? 900 : 700);
            font->setItalic(false); font->setUnderline(false); font->setStrikeOut(false);
            data::LegacyBitmapRGBA8 image{120, 20, std::vector<std::uint8_t>(120 * 20 * 4)};
            if (i < 3)
            {
                const auto resources = playback.resources();
                if (!resources) return std::unexpected("IBar runtime text requires resources");
                const auto id = data::packDataId(data::LegacyGroupId::Main, BaseTags[i]);
                const auto metadata = resources->banks().metadata(id);
                if (!metadata) return std::unexpected(metadata.error().detail);
                const auto bytes = resources->banks().load(id);
                if (!bytes) return std::unexpected(bytes.error().detail);
                const auto base = bases_.resolve(id, metadata->type, *bytes);
                if (!base) return std::unexpected(base.error().detail);
                image = (*base)->image;
                if (i == 2) std::fill(image.pixels.begin(), image.pixels.end(), 0);
            }
            if (text[i].empty())
            {
                images[i] = std::move(image);
                continue;
            }
            const auto measured = font->measure(text[i]);
            if (!measured) return std::unexpected(measured.error().detail);
            const auto x = i < 2 ? (static_cast<int>(image.width) - measured->width) / 2 :
                i == 2 ? std::max(0, (140 - measured->width) / 2) : 120 - measured->width;
            const auto blitted = font->blitText(
                image, text[i], x, i < 2 ? 4 : 0,
                i < 2 ? 0x00FFFFFF : i == 2 ? 0 : nextColor);
            if (!blitted) return std::unexpected(blitted.error().detail);
            images[i] = std::move(image);
        }

        // All resource/font work and queue preflight precede publication.
        for (std::size_t i = 0; i < desired.size(); ++i)
        {
            if (images[i])
            {
                if (surfaces_[i] == data::EmptyDataId)
                {
                    const auto created = playback.runtimeBitmaps().create(images[i]->width, images[i]->height, true);
                    if (!created) return std::unexpected(created.error());
                    surfaces_[i] = *created;
                }
                const auto updated = playback.runtimeBitmaps().update(surfaces_[i], std::move(*images[i]));
                if (!updated) return std::unexpected(updated.error());
            }
            if (desired[i] == shown_[i]) continue;
            if (desired[i])
            {
                const auto started = playback.startXY(surfaces_[i], Priorities[i], X[i], Y[i]);
                if (!started) return started;
            }
            else
            {
                const auto stopped = playback.stop(surfaces_[i], Priorities[i]);
                if (!stopped) return stopped;
            }
        }
        shown_ = desired; keys_ = std::move(keys);
        message_ = std::move(nextMessage); messageColor_ = nextColor;
        wantedTick_ = nextWanted; cashSerial_ = nextCash; messagePinned_ = nextPinned;
        monetarySystem_ = monetarySystem; edition_ = edition;
        return {};
    }

    void RuntimeTextPlayback::reset() noexcept
    {
        surfaces_ = {}; keys_ = {}; shown_ = {}; bases_.clear();
        cashSerial_.reset(); wantedTick_.reset(); monetarySystem_.reset(); edition_.reset();
        messagePinned_ = false; message_.clear(); messageColor_ = 0;
    }
}
