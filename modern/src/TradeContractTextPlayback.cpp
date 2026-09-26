#include "TradeContractTextPlayback.hpp"

#include "FontRuntime.hpp"
#include "IBarLayout.hpp"
#include "LanguageResources.hpp"
#include "RuntimeBitmapSurface.hpp"
#include "SequenceTransforms.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <utility>

namespace monopoly::tradeui
{
    namespace
    {
        constexpr std::uint32_t PanelWidth = 200;
        constexpr std::uint32_t PanelHeight = 225;
        constexpr int PanelX = 600;
        constexpr int WrapWidth = 176;
        // GameInc.h includes Dat_Mon/dat_lang.h, not the obsolete Langids.h.
        constexpr std::uint32_t OkayMessageId = 933;
        constexpr std::uint32_t SquareNameBase = 1001;
        constexpr std::uint32_t FutureHeadingId = 2016;
        constexpr std::uint32_t ImmunityHeadingId = 2025;
        constexpr std::uint32_t TextColour = 0x00DCDCDCU;
        constexpr std::uint32_t SelectedTextColour = 0x00FFFFFFU;
        constexpr std::uint32_t SelectedColour = 0x00B40000U; // COLORREF: (0,0,180).

        [[nodiscard]] std::expected<std::string, std::string> messageText(
            const data::LanguageCatalog& catalog, std::uint32_t id)
        {
            const auto text = catalog.message(id);
            if (!text) return std::unexpected(text.error().detail);
            const auto encoded = fonts::transcodeUtf8(std::u16string_view(**text));
            if (!encoded) return std::unexpected(encoded.error().detail);
            return *encoded;
        }

        [[nodiscard]] std::expected<std::string, std::string> propertyName(
            const data::LanguageCatalog& catalog, std::uint32_t properties,
            int city, data::BoardEdition edition)
        {
            // RULE_BitSetToProperty uses the first property, or SQ_OFF_BOARD.
            int square = OffBoardSquare;
            for (int candidate = 0; candidate < OffBoardSquare; ++candidate)
                if ((ibar::layout::propertyBit(candidate) & properties) != 0)
                {
                    square = candidate;
                    break;
                }
            // Rule.cpp::InitialisePredefinedData: the Europe custom-board
            // baseline uses language-2; a leading '*' refers to the base set.
            if (city == -1 && edition == data::BoardEdition::Europe)
                city = static_cast<int>(catalog.language()) - 2;
            const auto id = SquareNameBase +
                42ULL * static_cast<std::uint32_t>(std::max(city, 0)) +
                static_cast<std::uint32_t>(square);
            if (id > std::numeric_limits<std::uint32_t>::max())
                return std::unexpected("Trade property-name index is out of range");
            auto text = catalog.lookup(static_cast<std::uint32_t>(id));
            if (!text) return std::unexpected(text.error().detail);
            if (!*text) return std::unexpected("Trade property name is missing from LANG");
            if (!(**text)->empty() && (**text)->front() == u'*')
            {
                text = catalog.lookup(SquareNameBase + static_cast<std::uint32_t>(square));
                if (!text) return std::unexpected(text.error().detail);
                if (!*text) return std::unexpected("Trade base property name is missing from LANG");
            }
            const auto encoded = fonts::transcodeUtf8(std::u16string_view(***text));
            if (!encoded) return std::unexpected(encoded.error().detail);
            return *encoded;
        }

        [[nodiscard]] std::expected<std::string, std::string> confirmationText(
            std::string_view pattern, std::int32_t amount,
            rules::PlayerNumber recipient, std::string_view name,
            const rules::GameState& gameState, const data::LanguageCatalog& catalog)
        {
            // The two mode-3 callers of FormatErrorNotification define only
            // numberB (count) and numberC (recipient). Never interpret names
            // inserted here as another template or as a printf format.
            std::string output;
            for (std::size_t i = 0; i < pattern.size(); ++i)
            {
                if (pattern[i] != '^') { output.push_back(pattern[i]); continue; }
                if (++i == pattern.size()) break;
                switch (pattern[i])
                {
                case '1': output += std::to_string(amount); break;
                case '2': output += std::to_string(recipient); break;
                case 'P': case 'p': output += name; break;
                case 'Q': case 'q':
                    if (amount >= 0 && amount < rules::MaxPlayers)
                    {
                        const auto encoded = fonts::transcodeUtf8(std::wstring_view(
                            gameState.players[static_cast<std::size_t>(amount)].name));
                        if (!encoded) return std::unexpected(encoded.error().detail);
                        output += *encoded;
                    }
                    else
                    {
                        const auto playerText = messageText(catalog,
                            amount == rules::BankPlayer ? 900U :
                            amount == rules::NobodyPlayer ? 902U : 901U);
                        if (!playerText) return std::unexpected(playerText.error());
                        output += *playerText;
                    }
                    break;
                case '3': case '4': case 'A': case 'a':
                case 'S': case 's': case 'T': case 't':
                    return std::unexpected("Trade confirmation refers to an undefined argument");
                default:
                    // FormatErrorNotification's default copies two carets.
                    output += "^^";
                    break;
                }
            }
            return output;
        }

        void fillRect(data::LegacyBitmapRGBA8& image, Rect rect,
            std::uint32_t colour, std::uint8_t alpha = 255)
        {
            for (int y = std::max(0, rect.top);
                 y < std::min(static_cast<int>(image.height), rect.bottom); ++y)
                for (int x = std::max(0, rect.left);
                     x < std::min(static_cast<int>(image.width), rect.right); ++x)
                {
                    const auto offset = (static_cast<std::size_t>(y) * image.width +
                        static_cast<std::size_t>(x)) * 4U;
                    image.pixels[offset] = static_cast<std::uint8_t>(colour);
                    image.pixels[offset + 1] = static_cast<std::uint8_t>(colour >> 8U);
                    image.pixels[offset + 2] = static_cast<std::uint8_t>(colour >> 16U);
                    image.pixels[offset + 3] = alpha;
                }
        }

        [[nodiscard]] std::expected<void, std::string> print(
            data::LegacyBitmapRGBA8& image, fonts::Runtime& font,
            std::string_view text, int x, int y, std::uint32_t colour)
        {
            if (text.empty()) return {};
            const auto blitted = font.blitText(image, text, x, y, colour);
            if (!blitted) return std::unexpected(blitted.error().detail);
            return {};
        }

        [[nodiscard]] std::expected<void, std::string> printCentered(
            data::LegacyBitmapRGBA8& image, fonts::Runtime& font,
            std::string_view text, int y, std::uint32_t colour)
        {
            const auto metrics = font.measure(text);
            if (!metrics) return std::unexpected(metrics.error().detail);
            return print(image, font, text,
                (static_cast<int>(PanelWidth) - metrics->width) / 2, y, colour);
        }

        void appendKey(std::string& key, std::string_view value)
        {
            key += std::to_string(value.size());
            key.push_back(':');
            key.append(value);
        }

        struct RestoreDefaultFont final
        {
            fonts::Runtime& runtime;
            ~RestoreDefaultFont() { (void)runtime.restoreSettings(0); }
        };
    }

    std::expected<void, std::string> ContractTextPlayback::sync(
        const State& state, const rules::GameState& gameState,
        display::Screen2D desiredView, int city, fonts::Runtime* fontRuntime,
        engine::SequencePlayback& playback)
    {
        const bool desired = desiredView == display::Screen2D::Trade &&
            state.contractDialogVisible;
        if (!desired)
        {
            if (!visible_) return {};
            if (!playback.commands().enqueue(sequence::StopSequenceCommand{
                    *surface_, TradeContractTextPriority, false}))
                return std::unexpected("Trade contract-text stop queue is full");
            visible_ = false;
            return {};
        }
        const auto mode = state.contractDialogMode;
        if (mode > 6 || state.contractDialogSide > 1 ||
            (state.contractDialogKind != rules::TradeItemKind::FutureRent &&
             state.contractDialogKind != rules::TradeItemKind::Immunity))
            return std::unexpected("Trade contract-text state is invalid");
        if (fontRuntime == nullptr || !fontRuntime->ready())
            return std::unexpected("Trade contract-text font runtime is unavailable");
        const auto resources = playback.resources();
        const auto language = resources ? resources->language() : nullptr;
        if (!language || !language->catalog)
            return std::unexpected("Trade contract text has no language catalog");
        if (!visible_ && playback.commands().pendingCount() >=
                sequence::SequenceCommandQueue::Capacity)
            return std::unexpected("Trade contract-text start queue is full");

        const auto& catalog = *language->catalog;
        const auto edition = resources->context().board;
        const auto headingId = state.contractDialogKind == rules::TradeItemKind::FutureRent
            ? FutureHeadingId : ImmunityHeadingId;
        constexpr std::array<std::uint32_t, 7> PromptOffsets{{1, 2, 4, 5, 6, 7, 8}};
        const auto promptId = headingId + PromptOffsets[mode] +
            (mode == 1 ? state.contractDialogSide : 0U);
        const auto heading = messageText(catalog, headingId);
        if (!heading) return std::unexpected(heading.error());
        auto prompt = messageText(catalog, promptId);
        if (!prompt) return std::unexpected(prompt.error());
        const auto okay = messageText(catalog, OkayMessageId);
        if (!okay) return std::unexpected(okay.error());
        std::string playerName;
        if (mode != 0)
        {
            const auto player = state.contractDialogSide ? state.playerA : state.playerB;
            if (player >= gameState.numberOfPlayers || player >= rules::MaxPlayers)
                return std::unexpected("Trade contract recipient is out of range");
            const auto encodedPlayerName = fonts::transcodeUtf8(
                std::wstring_view(gameState.players[player].name));
            if (!encodedPlayerName)
                return std::unexpected(encodedPlayerName.error().detail);
            playerName = *encodedPlayerName;
            if (mode == 3)
            {
                prompt = confirmationText(*prompt, state.contractAmount, player,
                    playerName, gameState, catalog);
                if (!prompt) return std::unexpected(prompt.error());
                playerName.clear();
            }
        }
        std::string amount;
        if (mode == 2)
        {
            amount = std::to_string(state.contractAmount);
            if (amount.size() < 2) amount.insert(amount.begin(), ' '); // %2i.
        }

        std::array<std::string, ContractListRects.size()> rows{};
        std::array<bool, ContractListRects.size()> selected{};
        const auto offset = state.contractList.empty() ? 0 : std::clamp(
            state.contractListOffset, 0, static_cast<int>(state.contractList.size()) - 1);
        for (std::size_t row = 0; mode != 0 && row < rows.size(); ++row)
        {
            const auto index = static_cast<std::size_t>(offset) + row;
            if (index >= state.contractList.size()) break;
            const auto& entry = state.contractList[index];
            auto name = propertyName(catalog, entry.properties, city, edition);
            if (!name) return std::unexpected(name.error());
            if (mode >= 4)
            {
                rows[row] = std::to_string(entry.hitCount);
                if (rows[row].size() < 2) rows[row].insert(rows[row].begin(), '0'); // %02d.
                rows[row].push_back(' ');
            }
            rows[row] += *name;
            selected[row] = entry.selected;
        }
        // The legacy line spacing is measured from PropertyList[0], even
        // when the viewport starts further down the list.
        std::string firstListText = rows.front();
        if (mode != 0 && offset != 0)
        {
            const auto& first = state.contractList.front();
            const auto name = propertyName(catalog, first.properties, city, edition);
            if (!name) return std::unexpected(name.error());
            firstListText.clear();
            if (mode >= 4)
            {
                firstListText = std::to_string(first.hitCount);
                if (firstListText.size() < 2) firstListText.insert(firstListText.begin(), '0');
                firstListText.push_back(' ');
            }
            firstListText += *name;
        }
        std::string key = std::to_string(mode) + ':' +
            std::to_string(static_cast<int>(edition)) + ':';
        appendKey(key, firstListText);
        for (const auto& text : {*heading, *prompt, *okay, playerName, amount})
            appendKey(key, text);
        for (std::size_t row = 0; row < rows.size(); ++row)
        {
            appendKey(key, rows[row]);
            key.push_back(selected[row] ? '1' : '0');
        }

        if (!contentKey_ || *contentKey_ != key)
        {
            RestoreDefaultFont restore{*fontRuntime};
            const auto settings = fontRuntime->restoreSettings(7);
            if (!settings) return std::unexpected(settings.error().detail);
            data::LegacyBitmapRGBA8 image{PanelWidth, PanelHeight, {}};
            image.pixels.assign(PanelWidth * PanelHeight * 4U, 0U);
            const auto titleLines = fontRuntime->wrap(*heading, WrapWidth);
            if (!titleLines) return std::unexpected(titleLines.error().detail);
            const auto promptLines = fontRuntime->wrap(*prompt, WrapWidth);
            if (!promptLines) return std::unexpected(promptLines.error().detail);
            const auto metrics = fontRuntime->measure(
                titleLines->empty() ? std::string_view{} : std::string_view(titleLines->front()));
            if (!metrics) return std::unexpected(metrics.error().detail);
            int y = 15;
            for (const auto& line : *titleLines)
            {
                if (auto result = printCentered(image, *fontRuntime, line, y, TextColour); !result)
                    return result;
                y += metrics->height;
            }
            y += 3;
            for (const auto& line : *promptLines)
            {
                if (auto result = printCentered(image, *fontRuntime, line, y, TextColour); !result)
                    return result;
                y += metrics->height;
            }

            // UDTrade.cpp::DrawButton: 80x22, two-pixel bevel, black caption.
            fillRect(image, {60,187,140,209}, 0x00BEBEBEU);
            fillRect(image, {138,187,140,209}, 0x00646464U);
            fillRect(image, {60,187,140,189}, 0x00DEDEDEU);
            fillRect(image, {60,187,62,209}, 0x00DEDEDEU);
            fillRect(image, {60,207,140,209}, 0x00646464U);
            if (auto result = printCentered(image, *fontRuntime, *okay, 191, 0); !result)
                return result;

            if (mode != 0)
            {
                const bool usa = edition == data::BoardEdition::Usa;
                fillRect(image, {14, usa ? 115 : 113, 166, usa ? 184 : 185}, 0, 0);
                const auto lineMetrics = fontRuntime->measure(firstListText);
                if (!lineMetrics) return std::unexpected(lineMetrics.error().detail);
                y = 113;
                for (std::size_t row = 0; row < rows.size(); ++row)
                {
                    if (static_cast<std::size_t>(offset) + row >= state.contractList.size()) break;
                    auto rect = ContractListRects[row];
                    rect.left -= PanelX;
                    rect.right -= PanelX;
                    fillRect(image, rect, selected[row] ? SelectedColour : 0,
                        selected[row] ? 255 : 0);
                    if (auto result = print(image, *fontRuntime, rows[row], 16, y,
                            selected[row] ? SelectedTextColour : TextColour); !result)
                        return result;
                    y += lineMetrics->height;
                }
                // Retail clears the strip beside the list after printing;
                // long property names must not overlap the arrow controls.
                fillRect(image, {166, usa ? 115 : 113, 216, usa ? 184 : 185}, 0, 0);
            }
            if (auto result = print(image, *fontRuntime, playerName, 42, 101, TextColour); !result)
                return result;
            if (!amount.empty())
            {
                const auto size = fontRuntime->measure(amount);
                if (!size) return std::unexpected(size.error().detail);
                if (auto result = print(image, *fontRuntime, amount,
                        20 + (20 - size->width) / 2, 101, TextColour); !result)
                    return result;
            }
            if (!surface_)
            {
                const auto created = playback.runtimeBitmaps().create(PanelWidth, PanelHeight, true);
                if (!created) return std::unexpected(created.error());
                surface_ = *created;
            }
            const auto updated = playback.runtimeBitmaps().update(*surface_, std::move(image));
            if (!updated) return std::unexpected(updated.error());
            contentKey_ = std::move(key);
        }
        if (visible_) return {};
        auto program = sequence::SequenceProgram::rawBitmap(*surface_, data::LegacyDataType::Native);
        if (!program) return std::unexpected(program.error().detail);
        if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                *program, TradeContractTextPriority, {}, sequence::moveXYTransform(PanelX, 0)}))
            return std::unexpected("Validated Trade contract-text start rejected");
        visible_ = true;
        return {};
    }

    void ContractTextPlayback::reset() noexcept
    {
        surface_.reset();
        contentKey_.reset();
        visible_ = false;
    }
}
