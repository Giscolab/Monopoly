#include "Display.hpp"
#include "ExtendedInitialization.hpp"
#include "Game.hpp"
#include "LegacyDataArchiveBuilder.hpp"
#include "Messaging.hpp"
#include "MousePointer.hpp"
#include "PlayerSelection.hpp"
#include "RenderSlots.hpp"
#include "RulesEngine.hpp"
#include "RuntimeState.hpp"
#include "TimeStep.hpp"
#include "Timers.hpp"
#include "UDUtils.hpp"
#include "UIMessages.hpp"
#include "UserInterface.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Game, ExtendedInitialization, DATA, LANG, timers et file UI sont reels.
// Seules les frontieres de presentation et de services non-DATA sont doubles,
// pour injecter les echecs sans GPU ni lancement du jeu.
namespace
{
    using namespace monopoly;
    using namespace monopoly::data;

    int failures = 0;
    enum class Failure { None, Messaging, Display, Rules };
    Failure injectedFailure = Failure::None;
    std::optional<ResourcePaths> configuredPaths;
    std::vector<std::string> events;
    bool messagingActive = false;
    bool displayActive = false;
    bool mouseActive = false;
    bool slotsActive = false;
    bool expectResourcesDuringCleanup = false;

    void expect(bool condition, std::string_view description)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "[FAIL] " << description << '\n';
        }
    }

    void event(std::string_view name)
    {
        events.emplace_back(name);
    }

    bool before(std::string_view first, std::string_view second)
    {
        const auto a = std::find(events.begin(), events.end(), first);
        const auto b = std::find(events.begin(), events.end(), second);
        return a != events.end() && b != events.end() && a < b;
    }

    void observeResources()
    {
        const auto snapshot = startup::resources();
        expect(static_cast<bool>(snapshot), "consumer sees published resources");
        if (!snapshot)
        {
            return;
        }
        expectResourcesDuringCleanup = true;
        expect(snapshot->banks().mountedCount() == 8,
            "consumer sees all five core and three LANG banks");
        const auto language = snapshot->language();
        expect(language && language->catalog,
            "consumer sees a valid language catalog");
        if (!language || !language->catalog)
        {
            return;
        }
        const auto textArchive = snapshot->banks().archive(9);
        const auto mediaArchive = snapshot->banks().archive(5);
        const auto dialogArchive = snapshot->banks().archive(10);
        expect(textArchive && *textArchive == language->textArchive &&
            mediaArchive && *mediaArchive == language->mediaArchive &&
            dialogArchive && *dialogArchive == language->dialogArchive,
            "consumer shares all three LANG instances with the DATA registry");
        const auto text = language->catalog->message(0);
        expect(text && **text == u"ok", "consumer can read real LANG fixture");
    }

    void observeCleanup()
    {
        if (expectResourcesDuringCleanup)
        {
            observeResources();
        }
    }

    class Fixture final
    {
    public:
        Fixture()
        {
            const auto stamp = std::chrono::steady_clock::now()
                .time_since_epoch().count();
            const auto name = "MonopolyModern-ResourceLifecycle-" +
                std::to_string(stamp);
            std::error_code error;
            auto temporaryRoot = std::filesystem::temp_directory_path(error);
            if (error)
            {
                temporaryRoot = std::filesystem::current_path();
            }
            root = temporaryRoot / name;
            created = std::filesystem::create_directory(root, error) && !error;
            if (!created)
            {
                error.clear();
                root = std::filesystem::current_path() / name;
                created = std::filesystem::create_directory(root, error) && !error;
            }
            if (!created)
            {
                expect(false, "isolated fixture directory can be created");
                return;
            }
            std::filesystem::create_directory(root / "Dat_Mon", error);
            if (error)
            {
                expect(false, "fixture DAT directory can be created");
                return;
            }

            const std::array<ArchiveBuildItem, 2> ordinary{{
                { LegacyDataType::Native, { std::byte{0x41} } },
                { LegacyDataType::Native, { std::byte{0x62}, std::byte{0x63} } }
            }};
            // L_Data packed index: uint32 message 0 -> uint16 tag 1.
            // String payload uses explicit UTF-16LE code units and terminator.
            const std::array<ArchiveBuildItem, 2> text{{
                { LegacyDataType::IndexTable, {
                    std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                    std::byte{1}, std::byte{0} } },
                { LegacyDataType::String, {
                    std::byte{0x6f}, std::byte{0}, std::byte{0x6b}, std::byte{0},
                    std::byte{0}, std::byte{0} } }
            }};
            for (const auto& bank : coreBanks(BoardEdition::Usa))
            {
                if (!writeLegacyDataArchive(root / bank.legacyPath, ordinary))
                {
                    expect(false, "core fixture archive can be written");
                    return;
                }
            }
            const auto* lang = findLanguageBankTriplet(LanguageId::EnglishUs);
            if (!writeLegacyDataArchive(root / lang->text.legacyPath, text) ||
                !writeLegacyDataArchive(root / lang->graphics.legacyPath, ordinary) ||
                !writeLegacyDataArchive(root / lang->dialog.legacyPath, ordinary))
            {
                expect(false, "LANG fixture archives can be written");
                return;
            }
            ready = true;
        }

        ~Fixture()
        {
            if (created)
            {
                std::error_code error;
                std::filesystem::remove_all(root, error);
                expect(!error, "isolated lifecycle fixture is removed");
            }
        }

        std::filesystem::path root;
        bool ready = false;

    private:
        bool created = false;
    };

    bool setPaths(const std::filesystem::path& root)
    {
        const std::array roots{ root };
        auto paths = ResourcePaths::create(roots);
        expect(static_cast<bool>(paths), "explicit fixture resource root is valid");
        if (!paths)
        {
            return false;
        }
        configuredPaths = std::move(*paths);
        return true;
    }

    void prepare()
    {
        startup::releaseResources();
        events.clear();
        injectedFailure = Failure::None;
        expectResourcesDuringCleanup = false;
        messagingActive = displayActive = mouseActive = slotsActive = false;
        expect(uimsg::initialize() && timers::initialize(),
            "real UI queue and timer service initialize");
    }

    void expectStopped()
    {
        expect(!startup::resources(), "game releases its active resource snapshot");
        expect(!messagingActive && !displayActive && !mouseActive && !slotsActive,
            "game has no active consumer after shutdown or rollback");
        // No reset here: this observes the real state left by the production path.
        expect(uimsg::size() == 0, "cleanup leaves no pending timer event");
        timers::advanceTicks(20);
        expect(uimsg::size() == 0,
            "game timers produce no new events after shutdown or rollback");
        expectResourcesDuringCleanup = false;
    }

    void testSuccessAndShutdown(const Fixture& fixture)
    {
        prepare();
        if (!setPaths(fixture.root)) return;
        const bool started = game::startup();
        expect(started, "Game startup accepts complete real DAT/LANG fixtures");
        if (!started) return;
        expect(before("slots.initialize", "mouse.initialize") &&
            before("mouse.initialize", "messaging.initialize") &&
            before("messaging.initialize", "display.initialize") &&
            before("display.initialize", "rules.initialize") &&
            before("rules.initialize", "playerselection.hiscore"),
            "Game initializes source-ordered consumers and initial phase");

        timers::advanceTicks(2);
        uimsg::Message message;
        expect(uimsg::receive(message) &&
            message.type == uimsg::Type::TimerReachedZero && message.numberA == 0,
            "successful startup activates the real main game timer");
        while (uimsg::receive(message)) {}
        expect(timers::configure(1, 1, 1, true, 1),
            "secondary timer can be armed before game shutdown");
        expect(uimsg::send({uimsg::Type::KeyboardPressed, 123}),
            "unrelated input queued before shutdown");
        timers::advanceTicks(2);
        expect(uimsg::send({uimsg::Type::TimerReachedZero, 2}),
            "unrelated timer event queued before shutdown");
        expect(uimsg::size() > 2, "game timer events are pending at shutdown");

        auto held = startup::resources();
        const auto main = held->banks().archive(2);
        expect(main && (*main)->cachedItemCount() == 0,
            "held core archive has no cached payload before shutdown");
        events.clear();
        game::shutdown();
        expect(before("display.shutdown", "mouse.shutdown") &&
            before("mouse.shutdown", "slots.shutdown"),
            "shutdown releases display then mouse then render slots");
        expect(uimsg::receive(message) &&
            message.type == uimsg::Type::KeyboardPressed && message.numberA == 123,
            "shutdown preserves unrelated input in FIFO order");
        expect(uimsg::receive(message) &&
            message.type == uimsg::Type::TimerReachedZero && message.numberA == 2,
            "shutdown removes only timers belonging to the game");
        expectStopped();
        const auto loaded = held->banks().load(packDataId(2, 1));
        expect(loaded && **loaded == DataBytes{std::byte{0x62}, std::byte{0x63}},
            "held snapshot loads previously uncached DATA after game shutdown");
        held.reset();
        game::shutdown();
        expectStopped();
    }

    void testRollbackAndRetry(const Fixture& fixture, Failure failure)
    {
        prepare();
        if (!setPaths(fixture.root)) return;
        injectedFailure = failure;
        expect(!game::startup(), "injected consumer failure rejects Game startup");
        if (failure == Failure::Rules)
        {
            expect(before("display.shutdown", "mouse.shutdown"),
                "rules failure unwinds display while resources are still available");
        }
        expect(before("mouse.shutdown", "slots.shutdown"),
            "failed extended initialization unwinds mouse before render slots");
        expectStopped();

        // Retry without reinitializing timers, queue, or runtime under test.
        injectedFailure = Failure::None;
        events.clear();
        expect(game::startup(), "startup succeeds immediately after failed attempt");
        game::shutdown();
        expectStopped();
    }

    void testMissingResources(const Fixture& fixture)
    {
        prepare();
        std::error_code error;
        const auto empty = fixture.root / "empty";
        std::filesystem::create_directory(empty, error);
        expect(!error, "empty resource root can be created");
        if (error || !setPaths(empty)) return;
        expect(!game::startup(), "missing retail banks reject Game startup");
        expect(std::find(events.begin(), events.end(), "messaging.initialize") ==
            events.end(), "missing banks prevent consumer initialization");
        expectStopped();
        if (!setPaths(fixture.root)) return;
        expect(game::startup(), "missing-bank failure permits a corrected-root retry");
        game::shutdown();
        expectStopped();
    }
}

namespace monopoly::udutils
{
    bool generateINIFile() { return configuredPaths.has_value(); }
    const data::ResourcePaths* resourcePaths() noexcept
    {
        return configuredPaths ? &*configuredPaths : nullptr;
    }
}

namespace monopoly::engine
{
    bool initializeRenderSlots()
    {
        event("slots.initialize");
        slotsActive = true;
        return true;
    }
    void shutdownRenderSlots()
    {
        event("slots.shutdown");
        observeCleanup();
        slotsActive = false;
    }
}

namespace monopoly::mouse
{
    bool initialize()
    {
        event("mouse.initialize");
        mouseActive = true;
        return true;
    }
    void shutdown()
    {
        event("mouse.shutdown");
        observeCleanup();
        mouseActive = false;
    }
}

namespace monopoly::messaging
{
    bool initialize()
    {
        event("messaging.initialize");
        observeResources();
        messagingActive = injectedFailure != Failure::Messaging;
        return messagingActive;
    }
    void shutdown()
    {
        event("messaging.shutdown");
        observeCleanup();
        messagingActive = false;
    }
}

namespace monopoly::display
{
    bool initialize()
    {
        event("display.initialize");
        observeResources();
        displayActive = injectedFailure != Failure::Display;
        return displayActive;
    }
    void shutdown()
    {
        event("display.shutdown");
        observeCleanup();
        displayActive = false;
    }
    void setBackdrop(Screen2D screen)
    {
        expect(screen == Screen2D::PlayerSelect,
            "extended startup chooses source-defined player selection backdrop");
    }
    void tickActions(std::uint64_t) {}
}

namespace monopoly::rules
{
    bool initialize()
    {
        event("rules.initialize");
        observeResources();
        return injectedFailure != Failure::Rules;
    }
    void shutdown() { observeCleanup(); }
}

namespace monopoly::runtime
{
    void reset() {}
}

namespace monopoly::userinterface
{
    void resetRuleProjection() {}
    void resetTimeStep() {}
    bool processUIMessage(const uimsg::Message&) { return true; }
}

namespace monopoly::playerselection
{
    void switchPhase(display::PlayerSetupPhase phase)
    {
        expect(phase == display::PlayerSetupPhase::HiScore,
            "extended startup chooses source-defined HiScore phase");
        event("playerselection.hiscore");
    }
}

int main()
{
    {
        Fixture fixture;
        if (fixture.ready)
        {
            testSuccessAndShutdown(fixture);
            testRollbackAndRetry(fixture, Failure::Messaging);
            testRollbackAndRetry(fixture, Failure::Display);
            testRollbackAndRetry(fixture, Failure::Rules);
            testMissingResources(fixture);
        }
        startup::releaseResources();
        configuredPaths.reset();
        timers::shutdown();
        uimsg::shutdown();
    }
    if (failures != 0)
    {
        std::cerr << failures << " lifecycle assertions failed.\n";
        return 1;
    }
    std::cout << "All resource lifecycle integration assertions passed.\n";
    return 0;
}
