#pragma once

#include "SequenceRuntime.hpp"
#include "UIMessages.hpp"

#include <optional>

namespace monopoly::sequence
{
    // L_Seqncr.cpp publishes the common 60 Hz TopLevelClock in numberE,
    // regardless of a node's local clock, cadence, or number of render frames.
    [[nodiscard]] inline std::optional<uimsg::Message> lifecycleMessage(
        const SequenceEvent& event, std::int32_t topLevelClock)
    {
        if (event.label == 0) return std::nullopt;
        uimsg::Message message{};
        switch (event.kind)
        {
        case SequenceEventKind::Created:
            message.type = uimsg::Type::SequenceStarted;
            message.numberD = event.sequenceType;
            break;
        case SequenceEventKind::ReachedEnd:
            message.type = uimsg::Type::SequenceReachedEnd;
            message.numberD = event.endingAction;
            break;
        case SequenceEventKind::Destroyed:
            message.type = uimsg::Type::SequenceDeleted;
            message.numberD = event.sequenceType;
            break;
        default:
            return std::nullopt;
        }
        message.numberA = static_cast<std::int64_t>(event.dataId);
        message.numberB = event.priority;
        message.numberC = event.label;
        message.numberE = topLevelClock;
        return message;
    }
}
