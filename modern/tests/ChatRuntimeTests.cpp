#include "ChatRuntime.hpp"
#include "Messaging.hpp"

#include <SDL3/SDL_scancode.h>

#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
    using namespace monopoly;
    std::vector<actions::Message> sentMessages;

    void require(bool condition, std::string_view message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) throw std::runtime_error(std::string(message));
    }

    uimsg::Message mouse(uimsg::Type type, int x, int y)
    {
        uimsg::Message message{};
        message.type = type;
        message.numberA = x;
        message.numberB = y;
        return message;
    }

    uimsg::Message textInput(std::string_view text)
    {
        uimsg::Message message{};
        message.type = uimsg::Type::TextInput;
        message.text = text;
        return message;
    }
}

namespace monopoly::messaging
{
    bool sendAction(const actions::Message& message)
    {
        sentMessages.push_back(message);
        return true;
    }

    std::size_t queuedActionCount()
    {
        return sentMessages.size();
    }
}

namespace
{
    using namespace monopoly;
    constexpr rules::PlayerNumber Sender = 0;

    void testOptionControls()
    {
        chat::reset();
        require(chat::stateReadOnly().fontSize == 7 &&
                chat::stateReadOnly().textAlphaIndex == 10 &&
                chat::stateReadOnly().backgroundAlphaIndex == 10,
            "chat Options start at retail font/alpha values");

        chat::toggle();
        require(chat::processInput(mouse(uimsg::Type::MouseLeftDown, 198, 12),
                    Sender, 0u, true) && chat::stateReadOnly().optionsOpen,
            "Options hotspot opens the retail panel");
        (void)chat::processInput(mouse(uimsg::Type::MouseLeftDown, 143, 28), Sender, 0u, true);
        (void)chat::processInput(mouse(uimsg::Type::MouseLeftDown, 143, 44), Sender, 0u, true);
        (void)chat::processInput(mouse(uimsg::Type::MouseLeftDown, 213, 60), Sender, 0u, true);
        require(chat::stateReadOnly().backgroundAlphaIndex == 9 &&
                chat::stateReadOnly().textAlphaIndex == 9 &&
                chat::stateReadOnly().fontSize == 8,
            "Options hotspots update the three retail indices");
    }

    void testFluffWindowControls()
    {
        chat::reset();
        chat::toggle();
        require(chat::processInput(mouse(uimsg::Type::MouseLeftDown, 220, 14),
                    Sender, 0u, true) && chat::stateReadOnly().fluffOpen,
            "Messages button opens the independent Fluff window");
        const auto& initial = chat::stateReadOnly();
        require(initial.fluffWindowX == 265 && initial.fluffWindowY == 10 &&
                initial.fluffWindowWidth == 246 && initial.fluffWindowHeight == 99 &&
                initial.fluffCategory == 0,
            "Fluff window starts at retail geometry and Greetings category");
        require(chat::FluffCategoryLineCounts == std::array<int, 6>{19, 21, 18, 19, 14, 8},
            "Fluff categories use exact retail line counts");

        (void)chat::processInput(mouse(uimsg::Type::MouseLeftDown, 500, 80), Sender, 0u, true);
        require(chat::stateReadOnly().fluffLineOffset == 1,
            "Fluff down arrow advances one line");
        (void)chat::processInput(mouse(uimsg::Type::MouseLeftDown, 500, 30), Sender, 0u, true);
        require(chat::stateReadOnly().fluffLineOffset == 0,
            "Fluff up arrow retreats one line");
        require(chat::processInput(mouse(uimsg::Type::MouseLeftDown, 500, 50),
                    Sender, 0u, true) && chat::stateReadOnly().fluffScrolling,
            "Fluff scrollbar begins retail drag tracking");
        (void)chat::processInput(mouse(uimsg::Type::MouseMoved, 500, 100), Sender, 0u, true);
        require(chat::stateReadOnly().fluffLineOffset == 18,
            "Fluff scrollbar clamps to the last Greetings line");
        (void)chat::processInput(mouse(uimsg::Type::MouseLeftUp, 500, 100), Sender, 0u, true);
        require(!chat::stateReadOnly().fluffScrolling,
            "Fluff scrollbar drag ends on left-button release");

        (void)chat::processInput(textInput("draft"), Sender, 0u, true);
        require(chat::processInput(mouse(uimsg::Type::MouseLeftDown, 438, 16),
                    Sender, 0u, true) && chat::stateReadOnly().fluffCategory == 3 &&
                chat::stateReadOnly().fluffLineOffset == 0 &&
                chat::stateReadOnly().draft.empty(),
            "changing Fluff category resets scroll and edit draft like GetCategory");
        (void)chat::processInput(textInput("x"), Sender, 0u, true);
        (void)chat::processInput(mouse(uimsg::Type::MouseLeftDown, 438, 16), Sender, 0u, true);
        require(chat::stateReadOnly().draft == u"x",
            "reselecting the current Fluff category preserves the draft");

        (void)chat::processInput(mouse(uimsg::Type::MouseLeftDown, 500, 16), Sender, 0u, true);
        require(chat::stateReadOnly().fluffShaded,
            "Fluff shade hotspot hides the body without closing the window");
        (void)chat::processInput(mouse(uimsg::Type::MouseLeftDown, 500, 16), Sender, 0u, true);
        require(!chat::stateReadOnly().fluffShaded,
            "Fluff shade hotspot restores the body");

        require(chat::processInput(mouse(uimsg::Type::MouseLeftDown, 500, 100),
                    Sender, 0u, true) && chat::stateReadOnly().fluffSizing,
            "Fluff resize hotspot starts sizing");
        (void)chat::processInput(mouse(uimsg::Type::MouseMoved, 600, 200), Sender, 0u, true);
        require(chat::stateReadOnly().fluffWindowWidth == 346 &&
                chat::stateReadOnly().fluffWindowHeight == 199,
            "Fluff resize uses retail bounds and five-pixel quantization");
        (void)chat::processInput(mouse(uimsg::Type::MouseLeftUp, 600, 200), Sender, 0u, true);
        require(!chat::stateReadOnly().fluffSizing,
            "Fluff resize ends on left-button release");

        require(chat::processInput(mouse(uimsg::Type::MouseLeftDown, 300, 15),
                    Sender, 0u, true) && chat::stateReadOnly().fluffMoving,
            "Fluff title bar starts window dragging");
        (void)chat::processInput(mouse(uimsg::Type::MouseMoved, 350, 50), Sender, 0u, true);
        require(chat::stateReadOnly().fluffWindowX == 315 &&
                chat::stateReadOnly().fluffWindowY == 45,
            "Fluff drag preserves the retail pointer offset without clamp");
        (void)chat::processInput(mouse(uimsg::Type::MouseLeftUp, 350, 50), Sender, 0u, true);

        require(chat::processInput(mouse(uimsg::Type::MouseLeftDown, 320, 50),
                    Sender, 0u, true) && !chat::stateReadOnly().fluffOpen,
            "Fluff close hotspot closes only the Fluff window");
    }

    void testFluffPersistence()
    {
        chat::reset();
        chat::toggle();
        (void)chat::processInput(mouse(uimsg::Type::MouseLeftDown, 220, 14), Sender, 0u, true);
        chat::toggle();
        require(!chat::stateReadOnly().boxActive && chat::stateReadOnly().fluffOpen,
            "FLUFF_BoxActive persists when the main Chat window closes");
        require(!chat::processInput(mouse(uimsg::Type::MouseLeftDown, 398, 16),
                    Sender, 0u, true) && chat::stateReadOnly().fluffCategory == 0,
            "hidden Fluff controls do not process input while Chat is closed");
        chat::toggle();
        require(chat::stateReadOnly().boxActive && chat::stateReadOnly().fluffOpen &&
                chat::processInput(mouse(uimsg::Type::MouseLeftDown, 398, 16),
                    Sender, 0u, true) && chat::stateReadOnly().fluffCategory == 1,
            "reopening Chat restores the persisted Fluff window and controls");
    }

    uimsg::Message key(SDL_Scancode scancode)
    {
        uimsg::Message message{};
        message.type = uimsg::Type::KeyboardPressed;
        message.numberA = scancode;
        return message;
    }

    void testWrappedOutputScrolling()
    {
        chat::reset();
        actions::Message incoming;
        incoming.action = actions::Type::NotifyTextChat;
        incoming.numberA = rules::AllPlayers;
        incoming.numberB = 0;
        incoming.stringA[0] = L'x';
        require(chat::processRuleMessage(incoming) && chat::processRuleMessage(incoming),
            "two logical messages enter the public history");
        chat::setOutputLayoutMetrics(12, 18, 3);
        require(chat::stateReadOnly().count == 2 && chat::stateReadOnly().outputOffset == 9 &&
            chat::stateReadOnly().wrappedOutputLines == 12 && chat::stateReadOnly().fontHeight == 18,
            "new messages scroll to the final visible wrapped lines, not logical-message count");
        require(chat::processInput(key(SDL_SCANCODE_PAGEUP), Sender, 0u, true) &&
            chat::stateReadOnly().outputOffset == 8, "PageUp advances by one wrapped output line");
        chat::setOutputLayoutMetrics(12, 18, 3);
        require(chat::stateReadOnly().outputOffset == 8,
            "unchanged measured layout preserves explicit scroll position");
        for (int i = 0; i < 6; ++i)
            (void)chat::processInput(key(SDL_SCANCODE_PAGEDOWN), Sender, 0u, true);
        require(chat::stateReadOnly().outputOffset == 11,
            "PageDown clamps to the last wrapped line even with only two history entries");
        chat::setOutputLayoutMetrics(4, 18, 3);
        require(chat::stateReadOnly().outputOffset == 3, "rewrapping a wider window clamps old scroll offset");
        require(chat::processRuleMessage(incoming), "new message follows explicit scrolling");
        chat::setOutputLayoutMetrics(15, 18, 3);
        require(chat::stateReadOnly().outputOffset == 12,
            "new arrival restores the retail follow-latest wrapped offset");
    }

    void testPublicPrivateSpectatorRouting()
    {
        chat::reset();
        sentMessages.clear();
        chat::toggle();
        constexpr std::uint32_t Eligible = (1u << 1) | (1u << 2);
        (void)chat::processInput(textInput("hello"), Sender, Eligible, true);
        (void)chat::processInput(key(SDL_SCANCODE_RETURN), Sender, Eligible, true);
        require(sentMessages.size() == 1 && sentMessages[0].numberA == rules::AllPlayers &&
            sentMessages[0].fromPlayer == Sender && sentMessages[0].toPlayer == rules::BankPlayer,
            "selecting all eligible recipients sends one public TextChat to RULE");
        sentMessages.clear();
        chat::setRecipientMask(1u << 2);
        (void)chat::processInput(textInput("private"), Sender, Eligible, true);
        (void)chat::processInput(key(SDL_SCANCODE_RETURN), Sender, Eligible, true);
        require(sentMessages.size() == 1 && sentMessages[0].numberA == 2 &&
            chat::stateReadOnly().count == 1 && chat::entryAt(0)->privateMessage &&
            chat::entryAt(0)->from == Sender && chat::entryAt(0)->text == u"private",
            "private selection addresses only that player and preserves sender's private history echo");
        sentMessages.clear();
        chat::setRecipientMask(Eligible);
        (void)chat::processInput(textInput("watching"), rules::SpectatorPlayer, Eligible, true);
        (void)chat::processInput(key(SDL_SCANCODE_RETURN), rules::SpectatorPlayer, Eligible, true);
        require(sentMessages.size() == 1 && sentMessages[0].fromPlayer == rules::SpectatorPlayer &&
            sentMessages[0].numberA == rules::AllPlayers,
            "spectator text retains its source identity and public destination");
    }

    void testMeasuredFluffSelection()
    {
        chat::reset();
        sentMessages.clear();
        chat::toggle();
        (void)chat::processInput(mouse(uimsg::Type::MouseLeftDown, 220, 14), Sender, 0u, true);
        chat::setOutputLayoutMetrics(0, 18, 3);
        require(chat::processInput(mouse(uimsg::Type::MouseLeftDown, 275, 65), Sender, 0u, true) &&
            chat::stateReadOnly().fluffSelectedLine == 2 && sentMessages.empty(),
            "first Fluff click selects row using measured eighteen-pixel font height");
        const auto id = chat::selectedFluffMessageId();
        chat::setFluffSelectionText(id + 1, u"stale layout");
        require(chat::stateReadOnly().draft.empty(), "stale translated Fluff selection cannot replace the edit text");
        chat::setFluffSelectionText(id, u"Selected localized message");
        require(chat::stateReadOnly().draft == u"Selected localized message",
            "current Fluff selection copies its resolved LANG text into the edit line");
        require(chat::processInput(mouse(uimsg::Type::MouseLeftDown, 275, 65), Sender, 0u, true) &&
            sentMessages.size() == 1 && sentMessages[0].numberC == id &&
            sentMessages[0].binaryDataA.empty() && chat::stateReadOnly().fluffSelectedLine == -1,
            "second click sends the selected canned ID rather than fabricating transmitted text");
    }
}

int main()
{
    try
    {
        testOptionControls();
        testFluffWindowControls();
        testFluffPersistence();
        testWrappedOutputScrolling();
        testPublicPrivateSpectatorRouting();
        testMeasuredFluffSelection();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
