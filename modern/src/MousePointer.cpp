#include "MousePointer.hpp"
#include "ChatRuntime.hpp"
#include <SDL3/SDL.h>
#include <cstdint>

namespace monopoly::mouse
{
    namespace { State pointer; }

    bool initialize()
    {
        pointer = {};
        pointer.initialized = true;
        return true;
    }
    void shutdown() { pointer = {}; }
    void updatePosition(int x, int y, bool inside) noexcept
    {
        pointer.x = x;
        pointer.y = y;
        pointer.inside = inside;
        if (!inside) pointer.kind = CursorKind::Pointer;
    }
    void setLeftDown(bool down) noexcept { pointer.leftDown = down; }
    void setEnabled(bool enabled) noexcept { pointer.enabled = enabled; }
    const State& stateReadOnly() noexcept { return pointer; }
    bool assetVisible(const State& state) noexcept
    {
        return state.initialized && state.enabled && state.inside &&
            state.kind == CursorKind::Pointer;
    }
    NativeCursorKind nativeCursorKind(const State& state, bool assetReady) noexcept
    {
        if (!state.initialized || !state.inside) return NativeCursorKind::Arrow;
        if (!state.enabled) return NativeCursorKind::Hidden;
        if (state.kind == CursorKind::Text) return NativeCursorKind::Text;
        return assetReady ? NativeCursorKind::Hidden : NativeCursorKind::Arrow;
    }
    void updateChatCursor(const chat::State& chat) noexcept
    {
        if (!pointer.inside || !chat.boxActive || chat.shaded)
        {
            pointer.kind = CursorKind::Pointer;
            return;
        }
        // UDChat.cpp:1000 preserves the current type during either window drag.
        if (chat.moving || chat.sizing || chat.scrolling ||
            chat.fluffMoving || chat.fluffSizing || chat.fluffScrolling) return;
        const std::int64_t left = static_cast<std::int64_t>(chat.windowX) + 5;
        const std::int64_t right = static_cast<std::int64_t>(chat.windowX) + chat.windowWidth - 20;
        const std::int64_t bottom = static_cast<std::int64_t>(chat.windowY) + chat.windowHeight - 4;
        const auto top = bottom - chat.fontHeight;
        if (chat.fontHeight > 0 && pointer.x >= left && pointer.x < right &&
            pointer.y >= top && pointer.y < bottom)
            pointer.kind = CursorKind::Text;
        else if (!pointer.leftDown)
            pointer.kind = CursorKind::Pointer;
    }

    NativeCursor::~NativeCursor() { reset(); }
    bool NativeCursor::sync(NativeCursorKind kind)
    {
        if (applied_ && current_ == kind) return true;
        if (kind == NativeCursorKind::Hidden)
        {
            if (!SDL_HideCursor()) return false;
        }
        else
        {
            SDL_Cursor* cursor = SDL_GetDefaultCursor();
            if (kind == NativeCursorKind::Text)
            {
                if (!text_) text_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
                cursor = text_;
            }
            if (!cursor || !SDL_SetCursor(cursor) || !SDL_ShowCursor()) return false;
        }
        current_ = kind;
        applied_ = true;
        return true;
    }
    void NativeCursor::reset() noexcept
    {
        if (SDL_WasInit(SDL_INIT_VIDEO))
        {
            SDL_SetCursor(SDL_GetDefaultCursor());
            SDL_ShowCursor();
            if (text_) SDL_DestroyCursor(text_);
        }
        text_ = nullptr;
        applied_ = false;
    }
}
