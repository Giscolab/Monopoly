#pragma once

namespace monopoly::chat { struct State; }
struct SDL_Cursor;

namespace monopoly::mouse
{
    enum class CursorKind { Pointer, Text };
    enum class NativeCursorKind { Arrow, Text, Hidden };
    struct State
    {
        int x{};
        int y{};
        bool initialized{};
        bool enabled{true};
        bool inside{};
        bool leftDown{};
        CursorKind kind{CursorKind::Pointer};
    };

    bool initialize();
    void shutdown();
    // Coordinates already converted by Application's 800x600 letterbox mapping.
    void updatePosition(int x, int y, bool inside) noexcept;
    void setLeftDown(bool down) noexcept;
    void setEnabled(bool enabled) noexcept;
    void updateChatCursor(const chat::State& chat) noexcept;
    [[nodiscard]] const State& stateReadOnly() noexcept;
    [[nodiscard]] bool assetVisible(const State& state) noexcept;
    // Keep a usable native arrow when the DAT pointer failed to publish.
    [[nodiscard]] NativeCursorKind nativeCursorKind(
        const State& state, bool assetReady) noexcept;

    // Application-thread owner. Destroy/reset before SDL_Quit.
    class NativeCursor final
    {
    public:
        NativeCursor() = default;
        ~NativeCursor();
        NativeCursor(const NativeCursor&) = delete;
        NativeCursor& operator=(const NativeCursor&) = delete;
        [[nodiscard]] bool sync(NativeCursorKind kind);
        void reset() noexcept;
    private:
        SDL_Cursor* text_{};
        NativeCursorKind current_{NativeCursorKind::Arrow};
        bool applied_{};
    };
}
