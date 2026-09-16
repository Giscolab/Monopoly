from pathlib import Path
root = Path(r'C:\Users\cadet\Documents\GitHub\Monopoly')

def rep(rel, old, new):
    p = root / rel
    s = p.read_text(encoding='utf-8')
    if old not in s:
        raise SystemExit(f'missing pattern in {rel}: {old[:90]!r}')
    p.write_text(s.replace(old, new, 1), encoding='utf-8', newline='\n')

rep('modern/src/ChatRuntime.hpp',
'''        int fontSize{7};
        int textAlphaIndex{10};
        int backgroundAlphaIndex{10};
        bool boxActive{};
''',
'''        int fontSize{7};
        int textAlphaIndex{10};
        int backgroundAlphaIndex{10};
        int fluffWindowX{265};
        int fluffWindowY{10};
        int fluffWindowWidth{246};
        int fluffWindowHeight{99};
        int fluffDragOffsetX{};
        int fluffDragOffsetY{};
        int fluffResizeOffsetX{};
        int fluffResizeOffsetY{};
        std::size_t fluffCategory{};
        bool boxActive{};
''')
