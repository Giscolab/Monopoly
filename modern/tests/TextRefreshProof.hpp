#pragma once

#include "SequencePlayback.hpp"
#include <algorithm>

// Node IDs are never reused: preserving the complete root set proves that a
// bitmap refresh did not silently stop/start a visually identical replacement.
inline bool textRefreshPreservesRoots(monopoly::engine::SequencePlayback& playback,
    const std::vector<monopoly::sequence::SequenceNodeId>& roots, std::int32_t tick)
{
    if (!playback.update(tick) || playback.runtime().roots() != roots) return false;
    for (const auto node : playback.world2D().order())
    {
        const auto* object = playback.world2D().find(node);
        if (!object || object->asset != playback.runtimeBitmaps().asset(object->contentsDataId)) return false;
    }
    const auto outcomes = playback.commands().outcomes();
    return !outcomes.empty() && std::all_of(outcomes.begin(), outcomes.end(),
        [](const auto& outcome)
        {
            return outcome.kind == monopoly::sequence::SequenceCommandKind::ForceRedraw &&
                !outcome.error && !outcome.startedNode && outcome.matched > 0;
        });
}

// A rejected refresh must remain retryable: neither immutable bitmap revisions
// nor node ownership may be committed before there is room for redraw commands.
template<class Refresh>
bool textRefreshRejectsFullQueue(monopoly::engine::SequencePlayback& playback, Refresh refresh)
{
    const auto roots = playback.runtime().roots();
    std::vector<std::shared_ptr<const monopoly::data::BitmapRuntimeAsset>> assets;
    for (const auto node : playback.world2D().order()) assets.push_back(playback.world2D().find(node)->asset);
    while (playback.commands().pendingCount() < monopoly::sequence::SequenceCommandQueue::Capacity)
        if (!playback.commands().enqueue(monopoly::sequence::ForceRedrawSequenceCommand{0, 0, false})) return false;
    if (refresh() || playback.commands().pendingCount() != monopoly::sequence::SequenceCommandQueue::Capacity ||
        playback.runtime().roots() != roots) return false;
    for (const auto& asset : assets)
        if (playback.runtimeBitmaps().asset(asset->dataId) != asset) return false;
    return playback.update(0).has_value() && playback.runtime().roots() == roots;
}
