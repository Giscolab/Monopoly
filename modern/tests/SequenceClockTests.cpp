#include "SequenceClock.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
    using namespace monopoly::data;
    using namespace monopoly::sequence;
    int failures{};

    void expect(bool condition, std::string_view description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << description << '\n';
        if (!condition) ++failures;
    }

    LegacySequenceRecord record(std::uint8_t action = 1, bool drop = false)
    {
        // Synthetic grouping record decoded through the physical CNK reader:
        // size=16, id=1, start=3, priority=5, end=12, cadence=4.
        DataBytes bytes{
            std::byte{16}, std::byte{0}, std::byte{0}, std::byte{1},
            std::byte{3}, std::byte{0}, std::byte{0}, std::byte{5},
            std::byte{12}, std::byte{0}, std::byte{0}, std::byte{4},
            std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}
        };
        bytes[11] = static_cast<std::byte>(4U | (drop ? 0x40U : 0U));
        bytes[12] = static_cast<std::byte>(action);
        LegacyChunkReader reader{ std::span<const std::byte>(bytes) };
        const auto decoded = readLegacySequenceRecord(reader);
        expect(decoded.has_value(), "clock fixture uses the real CNK/sequence decoder");
        return decoded.value(); // values survive destruction of reader and bytes
    }

    void testCadence()
    {
        auto clock = SequenceClock::start(record()).value();
        auto update = clock.update(100);
        expect(update && update->updated && !update->previousClock &&
            update->clock == 0 && !update->hitEnd,
            "first update is immediate at zero, not at absolute parent time");
        for (const auto tick : {100, 101, 103})
        {
            update = clock.update(tick);
            expect(update && !update->updated && clock.clock() == 0,
                "sub-cadence updates preserve accumulated parent elapsed time");
        }
        update = clock.update(104);
        expect(update && update->updated && update->previousClock == 0 &&
            update->clock == 4, "exact cadence advances one interval");
        update = clock.update(120);
        expect(update && update->updated && update->clock == 8,
            "keep-frames clamps a delayed update to one interval");
        update = clock.update(120);
        expect(update && !update->updated && clock.clock() == 8,
            "clamped excess is discarded, not accumulated as frame debt");
        update = clock.update(124);
        expect(update && update->hitEnd && update->notifyEnd && update->stopped &&
            clock.stopped() && update->clock == 12,
            "stop occurs on the exact end boundary");
        update = clock.update(200);
        expect(update && !update->updated && !update->hitEnd && update->stopped,
            "a stopped sequence never updates or reports its end again");

        auto dropping = SequenceClock::start(record(1, true)).value();
        (void)dropping.update(100);
        update = dropping.update(120);
        expect(update && update->clock == 20 && update->stopped,
            "drop-frames consumes all elapsed time and may stop beyond end");
    }

    void testEndActions()
    {
        auto looping = SequenceClock::start(record(3, true)).value();
        (void)looping.update(0);
        auto update = looping.update(29);
        expect(update && update->hitEnd && update->notifyEnd &&
            update->restartChildren && update->clock == 0 && !update->stopped,
            "natural loop returns to zero, not 29 modulo 12");
        update = looping.update(33);
        expect(update && update->clock == 4 && !update->restartChildren,
            "loop rebases the parent stamp for the next cycle");
        update = looping.update(41);
        expect(update && update->restartChildren && update->notifyEnd,
            "each loop reports a distinct end and child restart");

        auto held = SequenceClock::start(record(2, true)).value();
        (void)held.update(0);
        update = held.update(20);
        expect(update && update->clock == 12 && update->notifyEnd &&
            !update->stopped && held.timeMultiple() == 255,
            "stay-at-end clamps the clock and uses Monopoly's 255-tick cadence");
        update = held.update(274);
        expect(update && !update->updated, "held-end reduced cadence is respected");
        update = held.update(275);
        expect(update && update->updated && update->hitEnd && !update->notifyEnd &&
            !update->restartChildren && update->clock == 12,
            "held-end watch/callback condition repeats but label notification does not");

        auto suicide = SequenceClock::start(record(0)).value();
        update = suicide.update(50);
        expect(update && update->hitEnd && update->stopped && update->clock == 0,
            "action zero means runtime suicide, not an indefinitely idle sequence");

        auto infiniteRecord = record(1, true);
        infiniteRecord.header.endTime = 0;
        auto infinite = SequenceClock::start(infiniteRecord).value();
        expect(infinite.endTime() == 1'234'567'890,
            "zero disk duration uses the exact source infinity sentinel");
        (void)infinite.update(0);
        update = infinite.update(SequenceClock::InfiniteEndTime - 1);
        expect(update && !update->hitEnd, "infinity sentinel is not reached early");
        update = infinite.update(SequenceClock::InfiniteEndTime, true);
        expect(update && update->stopped, "finite legacy infinity ends at its sentinel");
    }

    void testStartAndOverrides()
    {
        const auto source = record(3, true);
        ClockStartOptions options;
        options.parentClockAtBirth = 10;
        options.initialClockOffset = 2;
        auto child = SequenceClock::start(source, options).value();
        expect(child.clock() == 9, "late dropping child starts at parent-start+offset");
        auto update = child.update(10);
        expect(update && update->clock == 9 && !update->previousClock,
            "birth lateness is not applied twice on first update");
        options.dropFrames = false;
        child = SequenceClock::start(source, options).value();
        expect(child.clock() == 2, "keep-frames override disables birth catch-up");
        options.parentClockAtBirth.reset();
        options.dropFrames.reset();
        child = SequenceClock::start(source, options).value();
        expect(child.clock() == 2, "top-level starts ignore absolute parent time");

        auto invalid = source;
        invalid.header.timeMultiple = 0;
        invalid.header.endingAction = 7;
        options.timeMultiple = 200;
        options.endingAction = 2;
        child = SequenceClock::start(invalid, options).value();
        expect(child.timeMultiple() == 200,
            "command cadence is eight-bit, not limited to six-bit disk field");
        (void)child.update(0);
        update = child.update(200);
        expect(update && update->clock == 12 && !update->stopped,
            "nonzero action replacement supersedes an unsupported disk action");

        options = {};
        options.initialClockOffset = 30;
        auto stop = SequenceClock::start(record(), options).value();
        update = stop.update(0);
        expect(update && update->stopped,
            "a start beyond end stops on the first update before presentation");
        options.initialClockOffset = -4;
        stop = SequenceClock::start(record(), options).value();
        (void)stop.update(0);
        update = stop.update(4);
        expect(update && update->clock == 0 && !update->stopped,
            "signed initial offsets are retained, not silently clamped");
    }

    void testPauseAndForce()
    {
        auto clock = SequenceClock::start(record(3, true)).value();
        (void)clock.update(0);
        auto update = clock.update(2, true);
        expect(update && update->updated && update->clock == 2,
            "forced reevaluation can advance less than one cadence");
        expect(clock.setPaused(true, 2).has_value(), "pause command succeeds");
        update = clock.update(100);
        expect(update && !update->updated && clock.clock() == 2,
            "paused clock ignores elapsed parent time");
        update = clock.update(101, true);
        expect(update && update->updated && update->clock == 2,
            "forced paused update permits reevaluation without advancing");
        expect(clock.setPaused(false, 200).has_value(), "resume command succeeds");
        update = clock.update(203);
        expect(update && !update->updated, "resume discards paused elapsed time");
        update = clock.update(204);
        expect(update && update->clock == 6, "resume advances from the rebased stamp");
        expect(clock.setPaused(false, 205).has_value(), "redundant resume is harmless");
        update = clock.update(208);
        expect(update && update->clock == 10,
            "redundant resume does not reset an already running clock");
    }

    void testErrors()
    {
        for (auto id : std::array<std::uint8_t, 6>{0, 5, 6, 7, 8, 10})
        {
            auto unsupported = record();
            unsupported.chunk.id = id;
            const auto result = SequenceClock::start(unsupported);
            expect(!result && result.error() == ClockError::UnsupportedSequenceType,
                "unsupported and hardware-clock sequence types are refused");
        }
        auto invalid = record();
        invalid.header.scrollingWorld = true;
        auto result = SequenceClock::start(invalid);
        expect(!result && result.error() == ClockError::UnsupportedScrollingWorld,
            "visibility-driven scrolling clocks are not approximated");
        invalid.header.scrollingWorld = false;
        invalid.header.timeMultiple = 0;
        result = SequenceClock::start(invalid);
        expect(!result && result.error() == ClockError::InvalidCadence,
            "zero cadence is rejected if no command replacement is supplied");
        invalid.header.timeMultiple = 4;
        invalid.header.endingAction = 7;
        result = SequenceClock::start(invalid);
        expect(!result && result.error() == ClockError::InvalidEndingAction,
            "unknown end action is never silently reinterpreted");
        invalid.header.endingAction = 3;
        invalid.header.endTime = -1;
        result = SequenceClock::start(invalid);
        expect(!result && result.error() == ClockError::NegativeEndTime,
            "unsupported negative durations are refused by runtime, not decoder");

        auto clock = SequenceClock::start(record(3, true)).value();
        (void)clock.update(100);
        auto update = clock.update(99);
        expect(!update && update.error() == ClockError::ParentClockWentBackwards &&
            clock.clock() == 0, "backwards parent time is a transactional error");
        const auto pause = clock.setPaused(true, 99);
        expect(!pause && !clock.paused(), "failed pause preserves paused state");
        update = clock.update(104);
        expect(update && update->clock == 4, "failed updates preserve the last timestamp");

        ClockStartOptions options;
        options.initialClockOffset = std::numeric_limits<std::int32_t>::max();
        options.parentClockAtBirth = 4;
        result = SequenceClock::start(record(3, true), options);
        expect(!result && result.error() == ClockError::ClockOverflow,
            "birth arithmetic is widened before overflow checks");
        clock = SequenceClock::start(record(3, true)).value();
        (void)clock.update(std::numeric_limits<std::int32_t>::min());
        update = clock.update(std::numeric_limits<std::int32_t>::max());
        expect(!update && update.error() == ClockError::ClockOverflow &&
            clock.clock() == 0, "parent subtraction cannot wrap a signed clock");
        update = clock.update(std::numeric_limits<std::int32_t>::min() + 4);
        expect(update && update->clock == 4,
            "overflow preserves state; INT32_MIN is a time, not an ownership sentinel");
        expect(clockErrorName(ClockError::ClockOverflow) == "ClockOverflow",
            "runtime errors expose stable diagnostic names");
    }
}

int main()
{
    testCadence();
    testEndActions();
    testStartAndOverrides();
    testPauseAndForce();
    testErrors();
    return failures == 0 ? 0 : 1;
}
