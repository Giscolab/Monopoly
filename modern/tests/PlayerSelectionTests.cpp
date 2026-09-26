#include "PlayerSelection.hpp"
#include "ChatRuntime.hpp"
#include "Display.hpp"
#include "ExtendedInitialization.hpp"
#include "IBar.hpp"
#include "LocalPlayers.hpp"
#include "Messaging.hpp"
#include "RuleConfiguration.hpp"
#include "RulesEngine.hpp"
#include "UISound.hpp"
#include "UserInterface.hpp"

#include <SDL3/SDL_scancode.h>
#include <iostream>
#include <string>
#include <string_view>

// Headless boundaries only. Name entry runs through the production selection
// handler and SDL TextInput/scancodes; no replacement name-editing logic.
namespace monopoly::display
{
    State testState;
    State& state() { return testState; }
    const State& stateReadOnly() { return testState; }
    void setBackdrop(Screen2D screen) { testState.desired2DView = screen; }
}
namespace monopoly::startup
{
    std::shared_ptr<const data::ResourceSnapshot> resources() noexcept { return {}; }
}
namespace monopoly::userinterface
{
    rules::GameState testState;
    rules::GameState& ruleState() { return testState; }
    const rules::GameState& ruleStateReadOnly() { return testState; }
}
namespace monopoly::engine
{
    void playWarningSound() noexcept {}
    void playPennybagsVoice(udsound::PennybagsVoice, udsound::TokenVoiceClipPolicy, bool) noexcept {}
}
namespace monopoly::ibar { void restoreRuleTracking() noexcept {} }
namespace monopoly::chat
{
    const State& stateReadOnly() noexcept { static const State state; return state; }
    void toggle() noexcept {}
}
namespace monopoly::messaging
{
    bool serverMode() { return true; }
    bool gameplayNetwork() { return false; }
    bool gameplayReady() { return false; }
    bool startConfiguredNetwork() { return false; }
    void stopNetwork() {}
    bool sendAction(const actions::Message&) { return false; }
    bool sendAction(actions::Type, rules::PlayerNumber, rules::PlayerNumber,
        std::int64_t, std::int64_t, std::int64_t, std::int64_t, std::wstring_view) { return false; }
}
namespace monopoly::rules
{
    bool initialize() { return false; }
    namespace configuration
    {
        bool acceptedConfigurationMessage(const GameOptions&, PlayerNumber, bool, actions::Message&)
        { return false; }
    }
}
namespace monopoly::ui::localplayers
{
    void reset() {}
    std::size_t count() { return 0; }
    std::size_t humanCount() { return 0; }
    bool slotIsLocalHumanPlayer(rules::PlayerNumber) { return false; }
    bool slotIsLocalPlayer(rules::PlayerNumber) { return false; }
    rules::PlayerNumber currentUIPlayer() { return rules::NobodyPlayer; }
    rules::PlayerNumber anyLocalPlayer(const rules::GameState&) { return rules::NobodyPlayer; }
    void setCurrentUIPlayerFromPlayerSet(const rules::GameState&, std::uint32_t) {}
    bool requestAddLocalPlayer(const rules::GameState&, std::wstring_view,
        std::uint8_t, std::uint8_t, std::uint8_t, bool) { return false; }
    bool requestRemoveLocalPlayer(const rules::GameState&, rules::PlayerNumber) { return false; }
}

namespace
{
    using namespace monopoly;
    int failures{};
    void expect(bool condition, std::string_view description)
    {
        if (!condition) { ++failures; std::cerr << "[FAIL] " << description << '\n'; }
    }
    void resetName(std::wstring name = L"_")
    {
        playerselection::state() = {};
        playerselection::state().playerInfo.name = std::move(name);
        display::state() = {};
        display::state().desired2DView = display::Screen2D::PlayerSelect;
        display::state().currentPlayerSetupPhase = display::PlayerSetupPhase::EnterName;
        display::state().desiredPlayerSetupPhase = display::PlayerSetupPhase::EnterName;
    }
    const std::wstring& name() { return playerselection::stateReadOnly().playerInfo.name; }
    void type(std::string text)
    {
        uimsg::Message event{};
        event.type = uimsg::Type::TextInput;
        event.text = std::move(text);
        playerselection::processLibraryMessage(event);
    }
    void key(SDL_Scancode code)
    {
        uimsg::Message event{};
        event.type = uimsg::Type::KeyboardPressed;
        event.numberA = code;
        playerselection::processLibraryMessage(event);
    }
}

int main()
{
    const std::string supplementary = "\xF0\x9F\x98\x80";
    const std::wstring wideSupplementary = L"\U0001F600";
    resetName();
    type("Zo\xC3\xAB");
    expect(name() == L"Zo\u00EB_", "BMP UTF-8 name is preserved");
    key(SDL_SCANCODE_BACKSPACE);
    expect(name() == L"Zo_", "Backspace removes one BMP character");
    type(supplementary);
    expect(name() == L"Zo" + wideSupplementary + L"_", "supplementary scalar is inserted intact");
    key(SDL_SCANCODE_BACKSPACE);
    expect(name() == L"Zo_", "Backspace removes the whole supplementary scalar");
    expect(playerselection::stateReadOnly().forcedRefresh, "name editing refreshes the screen");

    resetName();
    type(supplementary + supplementary);
    key(SDL_SCANCODE_BACKSPACE);
    expect(name() == wideSupplementary + L"_", "adjacent supplementary characters erase individually");
    key(SDL_SCANCODE_BACKSPACE);
    expect(name() == L"_", "last supplementary character leaves only the cursor");
    playerselection::state().forcedRefresh = false;
    key(SDL_SCANCODE_BACKSPACE);
    expect(name() == L"_" && !playerselection::stateReadOnly().forcedRefresh,
        "Backspace on empty name changes nothing");

    const auto units = wideSupplementary.size();
    const std::wstring prefix(10 - units, L'a');
    resetName(prefix + L"_");
    type(supplementary + "z");
    expect(name() == prefix + wideSupplementary + L"_", "whole scalar fits exactly at native name limit");
    key(SDL_SCANCODE_BACKSPACE);
    expect(name() == prefix + L"_", "scalar at capacity erases atomically");
    type(std::string(units, 'b'));
    expect(name() == prefix + std::wstring(units, L'b') + L"_", "erased capacity can be reused");
    type("c");
    expect(name().size() == 11, "ten native name units exclude cursor");
    if constexpr (sizeof(wchar_t) == 2)
    {
        resetName(L"123456789_");
        type(supplementary + "z");
        expect(name() == L"123456789_", "one remaining UTF-16 unit cannot accept half a pair");
        type("z");
        expect(name() == L"123456789z_", "remaining unit still accepts a BMP character");
    }

    resetName();
    type(std::string("\xC0\xAF\xE0\x80\xAF\xF0\x80\x80\xAF") +
        "\xED\xA0\x80\xED\xBF\xBF\xF4\x90\x80\x80\xFF\x80" + "A");
    expect(name() == L"A_", "overlong, surrogate, out-of-range and stray UTF-8 bytes are rejected");
    type(std::string("\xE2") + "B");
    expect(name() == L"A_", "incomplete UTF-8 sequence is not partially inserted");
    type(std::string("\xE2") + "BC");
    expect(name() == L"ABC_", "invalid continuation recovers subsequent valid characters");
    type("\xF0\x9F\x98");
    expect(name() == L"ABC_", "truncated supplementary input leaves name intact");

    resetName();
    type("A\nB");
    expect(name() == L"A_", "legacy control-character rejection stops this input event");
    type("\xC2\x85");
    expect(name() == L"A_", "legacy C1 control rejection is preserved");
    display::state().desired2DView = display::Screen2D::Main;
    type("x");
    key(SDL_SCANCODE_BACKSPACE);
    expect(name() == L"A_", "name input remains inactive outside selection screen");

    resetName();
    key(SDL_SCANCODE_RETURN);
    expect(display::state().desiredPlayerSetupPhase == display::PlayerSetupPhase::EnterName,
        "empty name cannot proceed");
    type(supplementary);
    key(SDL_SCANCODE_RETURN);
    expect(display::state().desiredPlayerSetupPhase == display::PlayerSetupPhase::SelectToken,
        "Unicode name proceeds through the production phase transition");
    return failures == 0 ? 0 : 1;
}
