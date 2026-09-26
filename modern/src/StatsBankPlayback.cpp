#include "StatsBankPlayback.hpp"

#include "IBarLayout.hpp"
#include "LegacyBitmap.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <variant>

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

        [[nodiscard]] std::expected<std::pair<int, int>, std::string>
        bitmapSize(const sequence::SequenceProgram& program)
        {
            const auto resources = program.resources();
            if (!resources)
                return std::unexpected("UDStats Bank sequence has no resource snapshot");

            for (const auto& description : program.descriptions())
            {
                if (!std::holds_alternative<data::SequenceBitmapData>(
                        description.record.data) || !description.contentsDataId)
                    continue;

                const auto id = *description.contentsDataId;
                const auto metadata = resources->banks().metadata(id);
                const auto bytes = resources->banks().load(id);
                if (!metadata || !bytes)
                    return std::unexpected("UDStats Bank bitmap dependency failed");
                if (metadata->type == data::LegacyDataType::Bitmap)
                {
                    const auto bitmap = data::inspectLegacyBitmap(**bytes);
                    if (!bitmap) return std::unexpected(bitmap.error().detail);
                    return std::pair{bitmap->width, bitmap->height};
                }
                if (metadata->type == data::LegacyDataType::Uap)
                {
                    const auto bitmap = data::inspectLegacyUap(**bytes);
                    if (!bitmap) return std::unexpected(bitmap.error().detail);
                    return std::pair{static_cast<int>(bitmap->width),
                        static_cast<int>(bitmap->height)};
                }
                return std::unexpected(
                    "UDStats Bank 2D sequence content is not a bitmap");
            }
            return std::unexpected("UDStats Bank sequence contains no bitmap record");
        }

        using Objects = std::vector<BankPlayback::Published>;

        [[nodiscard]] std::optional<Rect> deedRectForSquare(int target) noexcept
        {
            int column = 0;
            int row = 1;
            for (int square = 0; square < static_cast<int>(rules::SquareCount); ++square)
            {
                if (ibar::layout::propertyIndex(square) < 0) continue;
                Rect rect{};
                switch (square)
                {
                case 5:  rect = {583,100,619,142}; break;
                case 12: rect = {714,100,750,142}; break;
                case 15: rect = {583,49,619,91}; break;
                case 25: rect = {648,100,684,142}; break;
                case 28: rect = {714,49,750,91}; break;
                case 35: rect = {648,49,684,91}; break;
                case 37:
                    --row;
                    rect = {38 + column * 69, 49 + row * 51,
                        74 + column * 69, 91 + row * 51};
                    --row;
                    break;
                default:
                    rect = {38 + column * 69, 49 + row * 51,
                        74 + column * 69, 91 + row * 51};
                    --row;
                    if (row < 0)
                    {
                        ++column;
                        row = 2;
                    }
                    break;
                }
                if (square == target) return rect;
            }
            return std::nullopt;
        }

        [[nodiscard]] data::DataId deedId(int square) noexcept
        {
            const auto property = ibar::layout::propertyIndex(square);
            if (property < 0) return data::EmptyDataId;
            return data::packDataId(data::LegacyGroupId::Patterns,
                static_cast<data::DataTag>(BankDeedBaseTag + property));
        }

        [[nodiscard]] std::expected<Objects, std::string> houseObjects(
            const State& state, const rules::GameState& gameState,
            engine::SequencePlayback& playback)
        {
            Objects result;
            const auto count = std::min<std::size_t>(
                gameState.numberOfPlayers, rules::MaxPlayers);
            const bool anyHotels = std::any_of(
                state.bankPlayerHotels.begin(), state.bankPlayerHotels.begin() + count,
                [](int value) { return value > 0; });
            int hotelWidth = 0;
            if (anyHotels)
            {
                const auto hotel = sequence::SequenceProgram::load(
                    playback.resources(), mainId(BankHotelTag));
                if (!hotel) return std::unexpected(hotel.error().detail);
                const auto size = bitmapSize(**hotel);
                if (!size) return std::unexpected(size.error());
                hotelWidth = size->first;
            }
            const bool anyHouses = std::any_of(
                state.bankPlayerHouses.begin(), state.bankPlayerHouses.begin() + count,
                [](int value) { return value > 0; });
            const int housesPerRow = gameState.options.maximumHouses / 2;
            if (anyHouses && housesPerRow <= 0)
                return std::unexpected("UDStats Bank house capacity cannot form retail rows");
            const int houseWidth = housesPerRow > 0 ? 346 / housesPerRow : 0;

            for (std::size_t slot = 0; slot < count; ++slot)
            {
                const auto colour = gameState.players[slot].colour;
                if (colour >= rules::MaxPlayerColours)
                    return std::unexpected("UDStats Bank player colour is out of range");
                result.push_back({mainId(static_cast<data::DataTag>(
                    BankPlayerBarBaseTag + colour)), BankOverlayPriority, 0, 0, false});

                const int top = 300 + static_cast<int>(colour) * 22;
                for (int hotel = 0; hotel < state.bankPlayerHotels[slot]; ++hotel)
                    result.push_back({mainId(BankHotelTag), BankIconPriority,
                        418 + hotel * hotelWidth, top + 2, true});
                for (int house = 0; house < state.bankPlayerHouses[slot]; ++house)
                {
                    const bool secondRow = house >= housesPerRow;
                    const int column = secondRow ? house - housesPerRow : house;
                    result.push_back({mainId(BankHouseTag), BankIconPriority,
                        38 + column * houseWidth + (secondRow ? 12 : 2),
                        top + (secondRow ? 6 : 1), true});
                }
            }
            return result;
        }

        [[nodiscard]] std::expected<Objects, std::string> propertyObjects(
            const State& state, engine::SequencePlayback& playback)
        {
            Objects result;
            const auto sold = sequence::SequenceProgram::load(
                playback.resources(), languageId(BankSoldSignTag));
            if (!sold) return std::unexpected(sold.error().detail);
            const auto soldSize = bitmapSize(**sold);
            if (!soldSize) return std::unexpected(soldSize.error());
            const int signX = (BankDeedWidth - soldSize->first) / 2;
            const int signY = (BankDeedHeight - soldSize->second) / 2;

            for (int square = 0; square < static_cast<int>(rules::SquareCount); ++square)
            {
                const auto rect = deedRectForSquare(square);
                const auto id = deedId(square);
                if (!rect || id == data::EmptyDataId) continue;
                const int x = BankContentX + rect->left;
                const int y = BankContentY + rect->top;
                result.push_back({id, BankOverlayPriority, x, y, true});

                const auto status = state.bankDeeds[static_cast<std::size_t>(square)];
                if (status == BankDeedState::Sold)
                    result.push_back({languageId(BankSoldSignTag), BankSignPriority,
                        x + signX, y + signY, true});
                else if (status == BankDeedState::Mortgaged)
                    result.push_back({languageId(BankMortgageSignTag), BankSignPriority,
                        x + signX, y + signY, true});
            }
            return result;
        }

        [[nodiscard]] Objects liabilityObjects(
            engine::SequencePlayback& playback)
        {
            Objects result;
            const auto resources = playback.resources();
            if (!resources || resources->context().board != data::BoardEdition::Usa)
                return result;
            result.push_back({languageId(BankLiabilityDividendCardTag),
                51, 365, 275, true, BankLiabilityCardScale});
            result.push_back({languageId(BankLiabilityErrorCardTag),
                51, 365, 358, true, BankLiabilityCardScale});
            return result;
        }
    }

    std::optional<Rect> bankDeedRect(int square) noexcept
    {
        return deedRectForSquare(square);
    }

    data::DataId bankDeedSequence(int square) noexcept
    {
        return deedId(square);
    }

    std::expected<void, std::string> BankPlayback::sync(
        const State& state, const rules::GameState& gameState,
        display::Screen2D desiredView, engine::SequencePlayback& playback,
        const AccountState* accounts)
    {
        Objects desired;
        const bool visible = desiredView == display::Screen2D::Portfolio &&
            state.screen == Screen::Bank;
        if (visible && state.activeSort == 0)
        {
            auto objects = houseObjects(state, gameState, playback);
            if (!objects) return std::unexpected(objects.error());
            desired = std::move(*objects);
        }
        else if (visible && state.activeSort == 1)
        {
            auto objects = propertyObjects(state, playback);
            if (!objects) return std::unexpected(objects.error());
            desired = std::move(*objects);
        }
        else if (visible && state.activeSort == 2)
            desired = liabilityObjects(playback);
        else if (visible && state.activeSort == 3 && accounts)
        {
            for (int arrow = 0; arrow < 2; ++arrow)
            {
                const bool enabled = arrow == 0 ? accounts->scrollLines < accounts->scrollLimit :
                    accounts->scrollLines > 0;
                if (!enabled) continue;
                const auto tag = static_cast<data::DataTag>(0x0007 + arrow * 2 +
                    (state.historyArrowPressed == arrow ? 1 : 0));
                desired.push_back({mainId(tag), 620, 0, 0, false});
            }
        }

        if (desired == current_) return {};

        std::map<data::DataId, std::shared_ptr<const sequence::SequenceProgram>> programs;
        for (const auto& object : desired)
        {
            if (programs.contains(object.id)) continue;
            auto loaded = sequence::SequenceProgram::load(playback.resources(), object.id);
            if (!loaded)
                return std::unexpected("UDStats Bank overlay resource failed: " +
                    loaded.error().detail);
            programs.emplace(object.id, std::move(*loaded));
        }

        std::set<std::pair<data::DataId, std::uint16_t>> stops;
        for (const auto& object : current_)
            stops.emplace(object.id, object.priority);

        const std::size_t required = stops.size() + desired.size();
        if (required > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit UDStats Bank overlay transition");
        }

        for (const auto& [id, priority] : stops)
        {
            if (!playback.commands().enqueue(
                    sequence::StopSequenceCommand{id, priority, false}))
                return std::unexpected("validated UDStats Bank stop rejected");
        }

        for (const auto& object : desired)
        {
            std::optional<sequence::SequenceTransform> transform;
            if (object.positioned && object.scale != 1.0F)
                transform = sequence::SequenceTransform{
                    sequence::moveXYSRTransform(
                        object.x, object.y, object.scale, 0.0F)};
            else if (object.positioned)
                transform = sequence::moveXYTransform(object.x, object.y);
            if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                    programs.at(object.id), object.priority, {}, std::move(transform)}))
                return std::unexpected("validated UDStats Bank start rejected");
        }

        current_ = std::move(desired);
        return {};
    }
}
