#pragma once

#include "DataBanks.hpp"
#include "IBarBankPlayback.hpp"
#include "IBarCameraButtonPlayback.hpp"
#include "IBarCurrentPlayerPlayback.hpp"
#include "RuleTypes.hpp"
#include "SequencePlayback.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::ibar
{
    inline constexpr data::DataTag PlayerBackdropBaseTag = 0x015B;
    inline constexpr data::DataTag BankBackdropTag = 0x0162;
    inline constexpr std::uint16_t BackdropPriority = 11;
    inline constexpr std::int32_t BackdropX = 0;
    inline constexpr std::int32_t BackdropY = 450;
    inline constexpr std::uint8_t PlayerColourCount = 6;

    [[nodiscard]] std::expected<data::DataId, std::string> desiredBackdrop(
        const rules::GameState& state,
        bool visible,
        rules::PlayerNumber activePlayer);

    class BackdropPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const rules::GameState& state,
            bool visible,
            rules::PlayerNumber activePlayer,
            engine::SequencePlayback& playback);

        void reset() noexcept
        {
            currentBackdrop_ = data::EmptyDataId;
            cameraButton_.reset();
            bank_.reset();
            currentPlayer_.reset();
        }

        [[nodiscard]] data::DataId currentBackdrop() const noexcept
        {
            return currentBackdrop_;
        }

        [[nodiscard]] data::DataId currentPlayerToken() const noexcept
        {
            return currentPlayer_.currentToken();
        }

        [[nodiscard]] bool bankVisible() const noexcept
        {
            return bank_.visible();
        }

        [[nodiscard]] CameraButtonVisualState cameraButtonState() const noexcept
        {
            return cameraButton_.visualState();
        }

    private:
        data::DataId currentBackdrop_{data::EmptyDataId};
        CameraButtonPlayback cameraButton_;
        BankPlayback bank_;
        CurrentPlayerPlayback currentPlayer_;
    };
}
