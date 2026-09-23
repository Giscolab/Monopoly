#include "MousePointerPlayback.hpp"
#include "ChatRuntime.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace monopoly;

namespace
{
    void require(bool value, std::string_view message)
    {
        if (!value) throw std::runtime_error(std::string(message));
        std::cout << "[PASS] " << message << '\n';
    }

    const engine::SequenceWorld2DObject* at(
        engine::SequencePlayback& playback, int x, int y)
    {
        for (const auto node : playback.world2D().order())
        {
            const auto* object = playback.world2D().find(node);
            if (object &&
                object->worldTransform.values[6] == static_cast<float>(x) &&
                object->worldTransform.values[7] == static_cast<float>(y))
                return object;
        }
        return nullptr;
    }

    mouse::State pointerState(int x, int y)
    {
        mouse::State state{};
        state.initialized = true;
        state.enabled = true;
        state.inside = true;
        state.kind = mouse::CursorKind::Pointer;
        state.x = x;
        state.y = y;
        return state;
    }
    void testPointerPlayback()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        mouse::Playback owner;

        require(mouse::PointerDataId ==
                data::packDataId(data::LegacyGroupId::Main, 0x0323),
            "mouse pointer uses retail DAT_MAIN TAB_pointer");
        require(mouse::PointerPriority == 0xFFFF,
            "mouse pointer preserves retail grouping priority 0xFFFF");

        auto state = pointerState(10, 20);
        require(owner.sync(state, playback).has_value(),
            "first in-game pointer sync queues the retail pointer sequence");
        require(owner.visible() && playback.commands().pendingCount() == 1,
            "pointer owner becomes visible after one transactional Start");
        require(playback.update(0).has_value(),
            "pointer Start reaches SequencePlayback");

        const auto* first = at(playback, 10, 20);
        require(first && first->priority == mouse::PointerPriority &&
                first->asset,
            "pointer bitmap is published at logical mouse coordinates");
        require(playback.world2D().size() == 1,
            "mouse owns exactly one Overlay2D root");

        state.x = 123;
        state.y = 456;
        require(owner.sync(state, playback).has_value() &&
                playback.commands().pendingCount() == 1,
            "mouse movement queues one Move without restarting pointer");
        require(playback.update(1).has_value() &&
                playback.world2D().size() == 1 &&
                at(playback, 123, 456),
            "Move keeps one pointer root and follows logical coordinates");

        state.kind = mouse::CursorKind::Text;
        require(owner.sync(state, playback).has_value() &&
                playback.commands().pendingCount() == 1,
            "switching to native text cursor queues one pointer Stop");
        require(playback.update(2).has_value() &&
                playback.world2D().size() == 0 && !owner.visible(),
            "text cursor mode removes the ArtLib pointer root");

        state.kind = mouse::CursorKind::Pointer;
        require(owner.sync(state, playback).has_value() &&
                playback.update(3).has_value() && owner.visible(),
            "leaving text field restores TAB_pointer");
        state.enabled = false;
        require(owner.sync(state, playback).has_value() &&
                playback.update(4).has_value() &&
                playback.world2D().size() == 0,
            "disabled mouse state hides the ArtLib pointer");
    }
    void testNativePolicyAndChatCursor()
    {
        mouse::State state{};
        require(mouse::nativeCursorKind(state, false) ==
                mouse::NativeCursorKind::Arrow,
            "uninitialized pointer leaves a usable native arrow");

        state = pointerState(10, 10);
        require(mouse::nativeCursorKind(state, false) ==
                mouse::NativeCursorKind::Arrow &&
                mouse::nativeCursorKind(state, true) ==
                mouse::NativeCursorKind::Hidden,
            "native arrow remains fallback until DAT pointer is ready");

        state.enabled = false;
        require(mouse::nativeCursorKind(state, true) ==
                mouse::NativeCursorKind::Hidden,
            "disabled cursor keeps native cursor hidden");
        state.enabled = true;
        state.kind = mouse::CursorKind::Text;
        require(mouse::nativeCursorKind(state, false) ==
                mouse::NativeCursorKind::Text,
            "chat edit field requests native I-beam");

        require(mouse::initialize(), "mouse runtime initializes");
        chat::State chat{};
        chat.boxActive = true;
        chat.windowX = 10;
        chat.windowY = 10;
        chat.windowWidth = 246;
        chat.windowHeight = 99;
        chat.fontHeight = 12;

        mouse::updatePosition(20, 100, true);
        mouse::updateChatCursor(chat);
        require(mouse::stateReadOnly().kind == mouse::CursorKind::Text,
            "cursor entering measured chat edit box becomes I-beam");

        mouse::setLeftDown(true);
        mouse::updatePosition(200, 40, true);
        mouse::updateChatCursor(chat);
        require(mouse::stateReadOnly().kind == mouse::CursorKind::Text,
            "chat source preserves I-beam while left button remains down");

        mouse::setLeftDown(false);
        mouse::updateChatCursor(chat);
        require(mouse::stateReadOnly().kind == mouse::CursorKind::Pointer,
            "releasing outside edit box restores game pointer");

        chat.moving = true;
        mouse::updatePosition(20, 100, true);
        mouse::updateChatCursor(chat);
        require(mouse::stateReadOnly().kind == mouse::CursorKind::Pointer,
            "window drag preserves current cursor instead of retesting edit field");

        chat.moving = false;
        mouse::updateChatCursor(chat);
        require(mouse::stateReadOnly().kind == mouse::CursorKind::Text,
            "ending drag re-evaluates chat edit field");
        mouse::shutdown();
    }
    void testTransactionalFailures()
    {
        auto state = pointerState(10, 20);

        {
            engine::SequencePlayback playback(nullptr);
            mouse::Playback owner;
            require(!owner.sync(state, playback),
                "missing pointer resources reject first publication");
            require(!owner.visible() &&
                    playback.commands().pendingCount() == 0 &&
                    playback.world2D().size() == 0,
                "missing pointer resource fails before visible mutation");
        }

        {
            SyntheticSequenceResources resources;
            engine::SequencePlayback playback(resources.service.snapshot());
            bool filled = true;
            for (std::size_t index = 0;
                 index < sequence::SequenceCommandQueue::Capacity; ++index)
            {
                if (!playback.commands().enqueue(
                        sequence::StopSequenceCommand{}))
                {
                    filled = false;
                    break;
                }
            }
            require(filled && playback.commands().pendingCount() ==
                    sequence::SequenceCommandQueue::Capacity,
                "saturate pointer failure-test FIFO");
            const auto before = playback.commands().pendingCount();
            mouse::Playback owner;
            require(!owner.sync(state, playback),
                "full FIFO rejects pointer publication");
            require(playback.commands().pendingCount() == before &&
                    !owner.visible(),
                "pointer FIFO preflight is transactional");
        }
    }
}

int main()
{
    try
    {
        testPointerPlayback();
        testNativePolicyAndChatCursor();
        testTransactionalFailures();
        std::cout << "Mouse pointer playback tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
