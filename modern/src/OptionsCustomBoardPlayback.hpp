#pragma once
#include "OptionsCustomBoardRuntime.hpp"
#include "SequencePlayback.hpp"
namespace monopoly::fonts { class Runtime; }
namespace monopoly::optionsui
{
    [[nodiscard]] constexpr data::DataId customBoardTitle(data::BoardEdition edition) noexcept
    {
        return data::packDataId(data::LegacyGroupId::LanguageGraphics,
            edition == data::BoardEdition::Usa ? 0x01C3 : 0x034D);
    }
    class CustomBoardPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(CustomBoardState& state,
            display::Screen2D view, fonts::Runtime* fontRuntime, engine::SequencePlayback& playback);
        void reset() noexcept;
    private:
        struct Object
        {
            data::DataId id{};
            std::uint16_t priority{};
            int x{}, y{};
            bool operator==(const Object&) const = default;
        };
        std::array<std::optional<data::DataId>, 9> textSurfaces_{};
        std::vector<Object> published_;
        std::optional<std::uint64_t> revision_;
    };
}
