#include "ChatRuntime.hpp"
#include "Messaging.hpp"

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

        (void)chat::processInput(textInput("draft"), Sender, 0u, true);
        require(chat::processInput(mouse(uimsg::Type::MouseLeftDown, 438, 16),
                    Sender, 0u, true) && chat::stateReadOnly().fluffCategory == 3 &&
                chat::stateReadOnly().draft.empty(),
            "changing Fluff category clears the edit draft like GetCategory");
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

    void testFluffIndependence()
    {
        chat::reset();
        chat::toggle();
        (void)chat::processInput(mouse(uimsg::Type::MouseLeftDown, 220, 14), Sender, 0u, true);
        chat::toggle();
        require(!chat::stateReadOnly().boxActive && chat::stateReadOnly().fluffOpen,
            "Fluff window remains active when the main chat window closes");
        require(chat::processInput(mouse(uimsg::Type::MouseLeftDown, 398, 16),
                    Sender, 0u, true) && chat::stateReadOnly().fluffCategory == 1,
            "Fluff category controls remain interactive while chat is closed");
    }
}

int main()
{
    try
    {
        testOptionControls();
        testFluffWindowControls();
        testFluffIndependence();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
