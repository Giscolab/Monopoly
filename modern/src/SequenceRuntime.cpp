#include "SequenceRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <utility>

namespace monopoly::sequence
{
    namespace
    {
        RuntimeError error(RuntimeErrorCode code, data::DataId id,
            std::size_t offset, std::string detail)
        { return {code, id, offset, std::move(detail), {}}; }

        template<class E>
        RuntimeError caused(RuntimeErrorCode code, data::DataId id,
            std::size_t offset, E cause)
        {
            auto result = error(code, id, offset, "sequence dependency failed");
            result.cause = std::move(cause);
            return result;
        }

        SequenceTransform applyTweekerBeforeLocal(const SequenceTransform& tweeker,
            bool applied, const SequenceTransform& local, std::uint8_t dimensionality)
        {
            if (!applied) return local;
            if (dimensionality == 2)
                return multiply(std::get<Matrix2D>(tweeker), std::get<Matrix2D>(local));
            if (dimensionality == 3)
                return multiply(std::get<Matrix3D>(tweeker), std::get<Matrix3D>(local));
            return local;
        }
        SequenceMeshChoice3D initialMeshChoice(
            const data::LegacySequenceAttributes& attributes,
            std::uint8_t dimensionality) noexcept
        {
            SequenceMeshChoice3D result{};
            if (dimensionality != 3) return result;
            for (const auto& attribute : attributes.values)
                if (const auto* choice =
                    std::get_if<data::Sequence3DMeshChoiceAttribute>(&attribute))
                    result = {choice->meshIndexA, choice->meshIndexB,
                        choice->meshProportion};
            return result;
        }

        std::optional<SequenceBounds3D> initialBounds3D(
            const data::LegacySequenceAttributes& attributes,
            std::uint8_t dimensionality) noexcept
        {
            if (dimensionality != 3) return std::nullopt;
            std::optional<SequenceBounds3D> result;
            for (const auto& attribute : attributes.values)
            {
                if (const auto* box =
                    std::get_if<data::Sequence3DBoundingBoxAttribute>(&attribute))
                {
                    SequenceBounds3D bounds{box->points.front(), box->points.front()};
                    for (const auto& point : box->points)
                        for (std::size_t axis = 0; axis < 3; ++axis)
                        {
                            bounds.minimum[axis] =
                                std::min(bounds.minimum[axis], point[axis]);
                            bounds.maximum[axis] =
                                std::max(bounds.maximum[axis], point[axis]);
                        }
                    result = bounds;
                }
                else if (const auto* sphere =
                    std::get_if<data::Sequence3DBoundingSphereAttribute>(&attribute))
                {
                    const float radius = std::abs(sphere->radius);
                    result = SequenceBounds3D{
                        {-radius, -radius, -radius},
                        {radius, radius, radius}};
                }
            }
            return result;
        }

        struct SequenceAudioState
        {
            std::uint16_t pitch{};
            std::uint8_t volume{100};
            std::int8_t panning{};
        };

        SequenceAudioState initialAudioState(
            const data::LegacySequenceRecord& record,
            const data::LegacySequenceAttributes& attributes) noexcept
        {
            SequenceAudioState result;
            if (!std::holds_alternative<data::SequenceSoundData>(record.data) &&
                !std::holds_alternative<data::SequenceVideoData>(record.data))
                return result;

            // Monopoly's C_ArtLib.h sets CE_ARTLIB_SeqncrDefaultSoundLevelPercent
            // to 32. L_Seqncr.cpp applies that default to sound and video nodes
            // before any SET_SOUND_VOLUME attribute overrides it.
            result.volume = 32;

            for (const auto& attribute : attributes.values)
            {
                if (const auto* pitch =
                    std::get_if<data::SequenceSoundPitchAttribute>(&attribute))
                    result.pitch = pitch->pitch;
                else if (const auto* volume =
                    std::get_if<data::SequenceSoundVolumeAttribute>(&attribute))
                    result.volume = std::min<std::uint8_t>(volume->volume, 100U);
                else if (const auto* pan =
                    std::get_if<data::SequenceSoundPanningAttribute>(&attribute))
                    result.panning = std::clamp<std::int8_t>(
                        pan->panning, -100, 100);
            }
            return result;
        }

        std::uint8_t initialSequenceLabel(
            const data::LegacySequenceRecord& record,
            const data::LegacySequenceAttributes& attributes) noexcept
        {
            std::uint8_t result{};
            for (const auto& attribute : attributes.values)
                if (const auto* label =
                    std::get_if<data::SequenceLabelAttribute>(&attribute))
                    result = label->labelNumber;

            // LI_SEQNCR_StartUpSequence processes private chunks first, then
            // overwrites that value with the camera's built-in label.
            if (const auto* camera =
                std::get_if<data::SequenceCameraData>(&record.data))
                result = camera->cameraLabel;
            return result;
        }

        std::string videoFileName(
            const data::LegacySequenceAttributes& attributes)
        {
            for (const auto& attribute : attributes.values)
                if (const auto* file =
                    std::get_if<data::SequenceFileName5Attribute>(&attribute))
                    return file->fileName;
            return {};
        }

        std::optional<data::Sequence2DBoundingBoxAttribute> boundingBox2D(
            const data::LegacySequenceAttributes& attributes)
        {
            for (const auto& attribute : attributes.values)
                if (const auto* box =
                    std::get_if<data::Sequence2DBoundingBoxAttribute>(&attribute))
                    return *box;
            return std::nullopt;
        }

        std::int32_t legacyRound2D(float value) noexcept
        {
            const auto rounded = std::nearbyint(static_cast<double>(value));
            return static_cast<std::int32_t>(std::clamp(
                rounded,
                static_cast<double>(std::numeric_limits<std::int32_t>::min()),
                static_cast<double>(std::numeric_limits<std::int32_t>::max())));
        }

        std::int32_t transformPointX(
            const Matrix2D& matrix, std::int32_t x, std::int32_t y) noexcept
        {
            const auto& m = matrix.values;
            const float newX = m[0] * static_cast<float>(x) +
                m[3] * static_cast<float>(y) + m[6];
            const float newW = m[2] * static_cast<float>(x) +
                m[5] * static_cast<float>(y) + m[8];
            return newW == 0.0F ? 0 : legacyRound2D(newX / newW);
        }

        std::int32_t soundCenterX2D(
            const data::LegacySequenceAttributes& attributes,
            const Matrix2D& worldTransform) noexcept
        {
            const auto box = boundingBox2D(attributes);
            const std::int32_t left = box ? box->left : 0;
            const std::int32_t right = box ? box->right : 0;
            const std::int32_t top = box ? box->top : 0;
            const std::int32_t bottom = box ? box->bottom : 0;
            const std::int64_t sum =
                static_cast<std::int64_t>(transformPointX(worldTransform, left, top)) +
                transformPointX(worldTransform, right, top) +
                transformPointX(worldTransform, left, bottom) +
                transformPointX(worldTransform, right, bottom);
            return static_cast<std::int32_t>(sum / 4);
        }

        float initialCameraFieldOfView(const data::LegacySequenceRecord& record,
            const data::LegacySequenceAttributes& attributes,
            std::uint8_t dimensionality) noexcept
        {
            if (!std::holds_alternative<data::SequenceCameraData>(record.data)) return 0.0F;
            float result = dimensionality == 3 ? 0.7853981633974F : 1.0F;
            for (const auto& attribute : attributes.values)
                if (const auto* field =
                    std::get_if<data::SequenceCameraFieldOfViewAttribute>(&attribute))
                    result = field->fieldOfView;
            return result;
        }

        std::expected<std::optional<float>, TweekerTransformError>
        evaluateTweekerCameraFieldOfView(
            const data::LegacySequenceAttributes& attributes,
            std::uint8_t interpolationType, std::int32_t clock,
            std::int32_t endTime, bool parentIsCamera) noexcept
        {
            if (interpolationType > 2)
                return std::unexpected(TweekerTransformError::InvalidInterpolation);
            if (interpolationType == 0) return std::nullopt;
            const data::SequenceCameraFieldOfViewAttribute* first{};
            const data::SequenceCameraFieldOfViewAttribute* second{};
            for (const auto& attribute : attributes.values)
                if (const auto* field =
                    std::get_if<data::SequenceCameraFieldOfViewAttribute>(&attribute))
                {
                    if (!first) first = field;
                    else { second = field; break; }
                }
            if (!first) return std::nullopt;
            if (!parentIsCamera)
                return std::unexpected(TweekerTransformError::DimensionalityMismatch);
            float result = first->fieldOfView;
            if (interpolationType == 2 && second && endTime < 1'234'567'890)
            {
                const float proportion = static_cast<float>(clock) /
                    static_cast<float>(endTime);
                result += proportion * (second->fieldOfView - result);
            }
            return result;
        }

        struct TweekerAudioUpdate
        {
            std::optional<std::uint16_t> pitch;
            std::optional<std::uint8_t> volume;
            std::optional<std::int8_t> panning;

            [[nodiscard]] bool empty() const noexcept
            { return !pitch && !volume && !panning; }
        };

        int interpolateInteger(float proportion, int first, int second) noexcept
        {
            return static_cast<int>(
                (1.0F - proportion) * static_cast<float>(first) +
                proportion * static_cast<float>(second));
        }

        std::expected<TweekerAudioUpdate, TweekerTransformError>
        evaluateTweekerAudio(
            const data::LegacySequenceAttributes& attributes,
            std::uint8_t interpolationType, std::int32_t clock,
            std::int32_t endTime, bool parentIsAudio) noexcept
        {
            TweekerAudioUpdate result;
            // ArtLib identity tweekers only clear transform tweeks; they do
            // not touch sound/video properties.
            if (interpolationType == 0)
                return result;
            const data::SequenceSoundPitchAttribute* pitchA{};
            const data::SequenceSoundPitchAttribute* pitchB{};
            const data::SequenceSoundVolumeAttribute* volumeA{};
            const data::SequenceSoundVolumeAttribute* volumeB{};
            const data::SequenceSoundPanningAttribute* panA{};
            const data::SequenceSoundPanningAttribute* panB{};

            for (const auto& attribute : attributes.values)
            {
                if (const auto* value =
                    std::get_if<data::SequenceSoundPitchAttribute>(&attribute))
                {
                    if (!pitchA) pitchA = value;
                    else if (!pitchB) pitchB = value;
                }
                else if (const auto* value =
                    std::get_if<data::SequenceSoundVolumeAttribute>(&attribute))
                {
                    if (!volumeA) volumeA = value;
                    else if (!volumeB) volumeB = value;
                }
                else if (const auto* value =
                    std::get_if<data::SequenceSoundPanningAttribute>(&attribute))
                {
                    if (!panA) panA = value;
                    else if (!panB) panB = value;
                }
            }

            if (!pitchA && !volumeA && !panA)
                return result;
            if (!parentIsAudio)
                return std::unexpected(
                    TweekerTransformError::DimensionalityMismatch);

            const bool linear = interpolationType == 2 &&
                endTime < 1'234'567'890;
            const float proportion = linear
                ? static_cast<float>(clock) / static_cast<float>(endTime)
                : 0.0F;

            if (pitchA)
            {
                const int value = linear && pitchB
                    ? interpolateInteger(
                        proportion, pitchA->pitch, pitchB->pitch)
                    : pitchA->pitch;
                result.pitch = static_cast<std::uint16_t>(
                    std::clamp(value, 0, 65'535));
            }
            if (volumeA)
            {
                const int value = linear && volumeB
                    ? interpolateInteger(
                        proportion, volumeA->volume, volumeB->volume)
                    : volumeA->volume;
                result.volume = static_cast<std::uint8_t>(
                    std::clamp(value, 0, 100));
            }
            if (panA)
            {
                const int value = linear && panB
                    ? interpolateInteger(
                        proportion, panA->panning, panB->panning)
                    : panA->panning;
                result.panning = static_cast<std::int8_t>(
                    std::clamp(value, -100, 100));
            }
            return result;
        }

        std::expected<std::optional<SequenceMeshChoice3D>, TweekerTransformError>
        evaluateTweekerMeshChoice(const data::LegacySequenceAttributes& attributes,
            std::uint8_t interpolationType, std::int32_t clock,
            std::int32_t endTime, std::uint8_t parentDimensionality) noexcept
        {
            if (interpolationType == 0) return std::nullopt;
            const data::Sequence3DMeshChoiceAttribute* first{};
            const data::Sequence3DMeshChoiceAttribute* second{};
            for (const auto& attribute : attributes.values)
                if (const auto* choice =
                    std::get_if<data::Sequence3DMeshChoiceAttribute>(&attribute))
                {
                    if (!first) first = choice;
                    else { second = choice; break; }
                }
            if (!first) return std::nullopt;
            if (parentDimensionality != 3)
                return std::unexpected(TweekerTransformError::DimensionalityMismatch);
            SequenceMeshChoice3D result{
                first->meshIndexA, first->meshIndexB, first->meshProportion};
            if (interpolationType == 2 && second && endTime < 1'234'567'890)
            {
                const float proportion = static_cast<float>(clock) /
                    static_cast<float>(endTime);
                result.meshProportion = first->meshProportion + proportion *
                    (second->meshProportion - first->meshProportion);
            }
            return result;
        }
    }

    std::expected<std::shared_ptr<const SequenceProgram>, RuntimeError>
    SequenceProgram::rawBitmap(data::DataId id, data::LegacyDataType sourceType,
        DescriptionLimits limits)
    {
        if (limits.maximumDepth == 0 || limits.maximumDepth > 128 ||
            limits.maximumDescriptions == 0)
            return std::unexpected(error(RuntimeErrorCode::InvalidLimits, id, 0,
                "description depth must be 1..128 and node budget nonzero"));
        if (sourceType != data::LegacyDataType::Uap &&
            sourceType != data::LegacyDataType::Native)
            return std::unexpected(error(RuntimeErrorCode::UnsupportedType, id, 0,
                "raw bitmap sequence supports only DataUAP and DataNative"));

        auto program = std::shared_ptr<SequenceProgram>(new SequenceProgram);
        data::LegacySequenceHeader header{};
        header.timeMultiple = 60;
        header.endingAction = 2;
        data::LegacySequenceRecord record{
            data::ChunkInfo{3, 0, 0, 0, 0}, header,
            data::SequenceBitmapData{id}, 0};
        auto children = SequenceChildSchedule::read({}, id, limits.maximumReferences);
        if (!children)
            return std::unexpected(std::visit([&](const auto& cause) {
                return caused(RuntimeErrorCode::DecodeFailure, id, 0, cause);
            }, children.error()));
        program->descriptions_.push_back({id, std::move(record),
            std::move(*children), {}, id, {}});
        return std::shared_ptr<const SequenceProgram>(std::move(program));
    }

    std::expected<std::shared_ptr<const SequenceProgram>, RuntimeError>
    SequenceProgram::runtimeVideo(data::DataId id, std::string fileName,
        data::SequenceVideoData options, data::Sequence2DBoundingBoxAttribute bounds,
        bool binkDoubleSize)
    {
        if (id == data::EmptyDataId || fileName.empty() ||
            fileName.find('\0') != std::string::npos ||
            bounds.right <= bounds.left || bounds.bottom <= bounds.top)
            return std::unexpected(error(RuntimeErrorCode::DataFailure, id, 0,
                "runtime video requires an ID, filename and positive destination rectangle"));
        auto program = std::shared_ptr<SequenceProgram>(new SequenceProgram);
        data::LegacySequenceHeader header{};
        header.timeMultiple = 1;
        header.endingAction = 1;
        // Video scheduling follows elapsed media time, never one decoded frame
        // per UI event. The backend chooses/drops frames and reports real EOF.
        header.dropFrames = true;
        data::LegacySequenceRecord record{
            data::ChunkInfo{6, 0, 0, 0, 0}, header, options, 0};
        auto children = SequenceChildSchedule::read({}, id, 0);
        if (!children)
            return std::unexpected(error(RuntimeErrorCode::DecodeFailure, id, 0,
                "cannot construct empty runtime video child schedule"));
        data::LegacySequenceAttributes attributes;
        attributes.values.push_back(data::SequenceDimensionalityAttribute{{}, 2});
        attributes.values.push_back(bounds);
        attributes.values.push_back(data::SequenceFileName5Attribute{{}, std::move(fileName)});
        program->descriptions_.push_back({id, std::move(record),
            std::move(*children), std::move(attributes), std::nullopt, {}, binkDoubleSize});
        return std::shared_ptr<const SequenceProgram>(std::move(program));
    }

    std::expected<std::shared_ptr<const SequenceProgram>, RuntimeError>
    SequenceProgram::load(const data::DataBankRegistry& registry, data::DataId id,
        std::size_t offset, DescriptionLimits limits)
    {
        if (limits.maximumDepth == 0 || limits.maximumDepth > 128 ||
            limits.maximumDescriptions == 0)
            return std::unexpected(error(RuntimeErrorCode::InvalidLimits, id, offset,
                "description depth must be 1..128 and node budget nonzero"));

        // L_Seqncr.cpp:3637-3663, 4260-4275. LE_SEQNCR_Start accepts a raw
        // raw bitmap/mesh DataID by synthesizing the corresponding infinite
        // sequence. DataUAP is handled below; MESHX/HMD uses a 3D mesh with
        // the ArtLib basic cadence (60 Hz), StayAtEnd, and modelDataID=DataID.
        // These are the paths used by UDBoard for raw overlays and CurrentBoard.
        auto metadata = registry.metadata(id);
        if (!metadata)
            return std::unexpected(caused(RuntimeErrorCode::DataFailure,
                id, offset, metadata.error()));
        if (metadata->type == data::LegacyDataType::Uap)
        {
            // L_Seqncr.cpp:3648-3658. Raw DataUAP starts as an infinite
            // 2D bitmap sequence at the ArtLib basic 60 Hz cadence.
            if (offset != 0)
                return std::unexpected(error(RuntimeErrorCode::DecodeFailure,
                    id, offset, "raw UAP sequence must start at offset zero"));
            return rawBitmap(id, metadata->type, limits);
        }

        if (metadata->type == data::LegacyDataType::Hmd)
        {
            if (offset != 0)
                return std::unexpected(error(RuntimeErrorCode::DecodeFailure,
                    id, offset, "raw HMD sequence must start at offset zero"));
            auto program = std::shared_ptr<SequenceProgram>(new SequenceProgram);
            data::LegacySequenceHeader header{};
            header.timeMultiple = 60;
            header.endingAction = 2; // LE_SEQNCR_EndingActionStayAtEnd.
            data::LegacySequenceRecord record{
                data::ChunkInfo{9, 0, 0, 0, 0}, header,
                data::SequenceMeshData{id}, 0};
            auto children = SequenceChildSchedule::read({}, id,
                limits.maximumReferences);
            if (!children)
                return std::unexpected(std::visit([&](const auto& cause) {
                    return caused(RuntimeErrorCode::DecodeFailure, id, offset, cause);
                }, children.error()));
            program->descriptions_.push_back({id, std::move(record),
                std::move(*children), {}, id, {}});
            return std::shared_ptr<const SequenceProgram>(std::move(program));
        }

        auto program = std::shared_ptr<SequenceProgram>(new SequenceProgram);
        struct Entry { std::size_t index; bool active; std::size_t height; };
        std::map<std::pair<data::DataId, std::size_t>, Entry> visited;
        std::size_t references{};
        std::function<std::expected<std::size_t, RuntimeError>(data::DataId, std::size_t, std::size_t)> visit;
        visit = [&](data::DataId currentId, std::size_t currentOffset,
            std::size_t depth) -> std::expected<std::size_t, RuntimeError>
        {
            if (depth > limits.maximumDepth)
                return std::unexpected(error(RuntimeErrorCode::DepthLimit, currentId,
                    currentOffset, "description path exceeds configured depth"));
            const auto key = std::pair{currentId, currentOffset};
            if (const auto found = visited.find(key); found != visited.end())
            {
                if (found->second.active)
                    return std::unexpected(error(RuntimeErrorCode::IndirectCycle,
                        currentId, currentOffset, "recursive sequence description reference"));
                if (found->second.height > limits.maximumDepth - depth + 1)
                    return std::unexpected(error(RuntimeErrorCode::DepthLimit, currentId,
                        currentOffset, "shared description exceeds depth on this path"));
                return found->second.index;
            }
            if (program->descriptions_.size() >= limits.maximumDescriptions)
                return std::unexpected(error(RuntimeErrorCode::DescriptionLimit,
                    currentId, currentOffset, "description count budget exceeded"));
            auto reader = data::openLegacyChunkReader(registry, currentId);
            if (!reader) return std::unexpected(caused(RuntimeErrorCode::DataFailure,
                currentId, currentOffset, reader.error()));
            const auto positioned = reader->seek(currentOffset);
            if (!positioned) return std::unexpected(caused(RuntimeErrorCode::DecodeFailure,
                currentId, currentOffset, positioned.error()));
            auto record = data::readLegacySequenceRecord(*reader);
            if (!record) return std::unexpected(caused(RuntimeErrorCode::DecodeFailure,
                currentId, currentOffset, record.error()));
            if (record->chunk.id != 1 && record->chunk.id != 2 &&
                record->chunk.id != 3 && record->chunk.id != 5 &&
                record->chunk.id != 6 && record->chunk.id != 7 &&
                record->chunk.id != 9 && record->chunk.id != 10)
                return std::unexpected(error(RuntimeErrorCode::UnsupportedType,
                    currentId, currentOffset,
                    "runtime currently executes grouping, indirect, 2D bitmap, sound, video intent, camera, 3D mesh and tweeker records only"));
            auto attributes = data::readLegacySequenceAttributes(*reader);
            if (!attributes) return std::unexpected(caused(RuntimeErrorCode::DecodeFailure,
                currentId, currentOffset, attributes.error()));
            for (const auto& attribute : attributes->values)
                if (const auto* unsupported =
                    std::get_if<data::SequenceUnsupportedAttribute>(&attribute))
                    return std::unexpected(error(RuntimeErrorCode::UnsupportedAttribute,
                        currentId, unsupported->chunk.headerOffset,
                        "sequence attribute is decoded but its effect is not executed"));
            auto schedule = openSequenceChildSchedule(registry, currentId, currentOffset,
                limits.maximumReferences - references);
            if (!schedule)
                return std::unexpected(std::visit([&](const auto& cause) {
                    return caused(RuntimeErrorCode::DecodeFailure, currentId, currentOffset, cause);
                }, schedule.error()));
            if (schedule->records().size() > limits.maximumReferences - references)
                return std::unexpected(error(RuntimeErrorCode::ReferenceLimit,
                    currentId, currentOffset, "description reference budget exceeded"));
            references += schedule->records().size();
            std::optional<data::DataId> contentsDataId;
            if (const auto* bitmap = std::get_if<data::SequenceBitmapData>(&record->data))
                contentsDataId = data::resolveSequenceDataId(record->header,
                    bitmap->bitmapDataId, currentId);
            else if (const auto* sound = std::get_if<data::SequenceSoundData>(&record->data))
                contentsDataId = data::resolveSequenceDataId(record->header,
                    sound->soundDataId, currentId);
            else if (const auto* mesh = std::get_if<data::SequenceMeshData>(&record->data))
                contentsDataId = data::resolveSequenceDataId(record->header,
                    mesh->modelDataId, currentId);
            const auto index = program->descriptions_.size();
            program->descriptions_.push_back({currentId, *record, *schedule,
                std::move(*attributes), contentsDataId, {}});
            visited.emplace(key, Entry{index, true, 1});
            std::size_t height = 1;
            for (const auto& child : schedule->records())
            {
                auto target = visit(schedule->containingDataId(), child.chunk.headerOffset, depth + 1);
                if (!target) return std::unexpected(target.error());
                program->descriptions_[index].childDescriptions.push_back(*target);
                const auto childHeight = visited.at(
                    {schedule->containingDataId(), child.chunk.headerOffset}).height;
                height = std::max(height, childHeight + 1);
            }
            visited.at(key) = {index, false, height};
            return index;
        };
        const auto root = visit(id, offset, 1);
        if (!root) return std::unexpected(root.error());
        return std::shared_ptr<const SequenceProgram>(std::move(program));
    }

    std::expected<std::shared_ptr<const SequenceProgram>, RuntimeError>
    SequenceProgram::load(std::shared_ptr<const data::ResourceSnapshot> resources,
        data::DataId id, std::size_t offset, DescriptionLimits limits)
    {
        if (!resources) return std::unexpected(error(RuntimeErrorCode::DataFailure,
            id, offset, "resource snapshot is null"));
        auto result = load(resources->banks(), id, offset, limits);
        if (!result) return result;
        // Created privately above; no mutable alias is exposed to callers.
        auto program = std::const_pointer_cast<SequenceProgram>(*result);
        const auto metadata = resources->banks().metadata(id);
        if (!metadata)
            return std::unexpected(caused(RuntimeErrorCode::DataFailure,
                id, offset, metadata.error()));

        const auto preload = [&](data::DataId contents, std::size_t recordOffset)
            -> std::expected<void, RuntimeError>
        {
            // Absolute EmptyItem is legal; relative tag zero was already
            // resolved with the containing group while building descriptions.
            if (contents == data::EmptyDataId) return {};
            auto bytes = resources->banks().load(contents);
            if (!bytes)
            {
                auto failure = caused(RuntimeErrorCode::DataFailure,
                    contents, recordOffset, bytes.error());
                failure.detail = "sequence preload failed: " + bytes.error().detail;
                return std::unexpected(std::move(failure));
            }
            // Main.cpp requests Load without AddRef. Release this local lease
            // now; the archive cache retains the item subject to its LRU budget.
            return {};
        };

        // Main.cpp:342 enables PreloadData for Start. PrepareSequenceData in
        // L_Seqncr.cpp:9257-9353 loads raw data, or only direct bitmap/sound
        // children of a top-level grouping. It deliberately does not preload
        // video files or recursively traverse indirect/grouping dependencies.
        // Do not pin all prepared assets: UseReferenceCounts is false, so cache
        // pressure may evict one before the first render/audio update, as in Source.
        if (metadata->type != data::LegacyDataType::Chunky)
        {
            if (auto loaded = preload(id, offset); !loaded)
                return std::unexpected(loaded.error());
        }
        else if (program->descriptions_.front().record.chunk.id == 1)
        {
            for (const auto childIndex : program->descriptions_.front().childDescriptions)
            {
                const auto& child = program->descriptions_[childIndex];
                if ((std::holds_alternative<data::SequenceBitmapData>(child.record.data) ||
                     std::holds_alternative<data::SequenceSoundData>(child.record.data)) &&
                    child.contentsDataId)
                {
                    if (auto loaded = preload(*child.contentsDataId,
                            child.record.chunk.headerOffset); !loaded)
                        return std::unexpected(loaded.error());
                }
            }
            if (auto loaded = preload(id, offset); !loaded)
                return std::unexpected(loaded.error());
        }
        program->resources_ = std::move(resources);
        return result;
    }
    std::span<const SequenceDescription> SequenceProgram::descriptions() const noexcept
    { return descriptions_; }
    std::shared_ptr<const data::ResourceSnapshot> SequenceProgram::resources() const noexcept
    { return resources_; }

    struct SequenceRuntime::Node
    {
        SequenceNodeId id;
        Node* parent;
        std::shared_ptr<const SequenceProgram> program;
        std::size_t description;
        std::uint16_t priority;
        SequenceClock clock;
        SequenceChildSchedule schedule;
        Nodes children;
        bool reevaluate{true};
        bool redrawRequested{true};
        bool needsRedraw{};
        std::uint8_t dimensionality{};
        bool explicitlyPositioned{};
        SequenceTransform localTransform;
        bool tweekerTransformApplied{};
        SequenceTransform tweekerTransform;
        SequenceTransform worldTransform;
        SequenceMeshChoice3D meshChoice{};
        float cameraFieldOfView{};
        std::uint8_t labelNumber{};
        std::uint16_t pitch{};
        std::uint8_t volume{100};
        std::int8_t panning{};
        bool scrollingOnScreen{true};
        bool scrollingHibernating{};
        const SequenceDescription& definition() const
        { return program->descriptions()[description]; }
    };

    SequenceRuntime::SequenceRuntime(RuntimeLimits limits) : limits_(limits) {}
    SequenceRuntime::~SequenceRuntime() = default;
    std::size_t SequenceRuntime::liveNodeCount() const noexcept { return liveNodes_; }
    std::span<const SequenceEvent> SequenceRuntime::events() const noexcept { return events_; }
    void SequenceRuntime::emit(SequenceEventKind kind, const Node& node)
    {
        const auto& def = node.definition();
        events_.push_back({kind, node.id, node.parent ? node.parent->id : 0,
            def.dataId, def.record.chunk.headerOffset, node.priority,
            node.labelNumber, def.record.chunk.id, node.clock.endingAction(),
            node.clock.clock()});
    }
    void SequenceRuntime::insert(Nodes& siblings, std::unique_ptr<Node> node)
    {
        // InsertRuntimeChild: before first GREATER OR EQUAL priority.
        const auto position = std::lower_bound(siblings.begin(), siblings.end(), node->priority,
            [](const auto& child, std::uint16_t priority) { return child->priority < priority; });
        siblings.insert(position, std::move(node));
    }
    std::expected<std::unique_ptr<SequenceRuntime::Node>, RuntimeError>
    SequenceRuntime::create(std::shared_ptr<const SequenceProgram> program,
        std::size_t description, Node* parent, std::uint16_t priority,
        ClockStartOptions options, std::uint8_t labelOverride)
    {
        const auto& def = program->descriptions()[description];
        const auto id = def.dataId;
        const auto offset = def.record.chunk.headerOffset;
        if (liveNodes_ >= limits_.maximumLiveNodes)
            return std::unexpected(error(RuntimeErrorCode::LiveNodeLimit, id, offset, "live node budget exceeded"));
        if (births_ >= limits_.maximumBirthsPerOperation)
            return std::unexpected(error(RuntimeErrorCode::BirthLimit, id, offset, "birth budget exceeded"));
        if (nextId_ == std::numeric_limits<SequenceNodeId>::max())
            return std::unexpected(error(RuntimeErrorCode::IdentifierExhausted, id, offset, "node identifiers exhausted"));
        if (parent) options.parentClockAtBirth = parent->clock.clock();
        else options.parentClockAtBirth.reset();
        auto clock = SequenceClock::start(def.record, options);
        if (!clock) return std::unexpected(caused(RuntimeErrorCode::ClockFailure, id, offset, clock.error()));
        const auto initial = initialSequenceTransform(def.record, def.attributes,
            parent ? parent->dimensionality : 0);
        const auto tweeker = initial.dimensionality == 2 ? SequenceTransform(identity2D()) :
            initial.dimensionality == 3 ? SequenceTransform(identity3D()) :
            SequenceTransform(std::monostate{});
        const auto world = composeSequenceWorld(initial.local, initial.dimensionality,
            parent ? parent->worldTransform : SequenceTransform(std::monostate{}),
            parent ? parent->dimensionality : 0);
        auto node = std::make_unique<Node>(Node{nextId_++, parent, std::move(program),
            description, priority, *clock, def.children, {}, true, true, false,
            initial.dimensionality, initial.explicitlyPositioned,
            initial.local, false, tweeker, world});
        node->meshChoice = initialMeshChoice(def.attributes, initial.dimensionality);
        node->cameraFieldOfView = initialCameraFieldOfView(
            def.record, def.attributes, initial.dimensionality);
        node->labelNumber = initialSequenceLabel(def.record, def.attributes);
        const auto audio = initialAudioState(def.record, def.attributes);
        node->pitch = audio.pitch;
        node->volume = audio.volume;
        node->panning = audio.panning;
        if (labelOverride != 0)
            node->labelNumber = labelOverride;
        if (node->labelNumber != 0)
            labelOwners_[node->labelNumber] = node->id;
        ++liveNodes_; ++births_;
        emit(SequenceEventKind::Created, *node);
        return node;
    }
    std::expected<SequenceNodeId, RuntimeError> SequenceRuntime::start(
        std::shared_ptr<const SequenceProgram> program, std::uint16_t priority,
        ClockStartOptions options, std::optional<SequenceTransform> initialTransform,
        std::uint8_t labelOverride)
    {
        events_.clear(); births_ = 0;
        if (!program || program->descriptions().empty())
            return std::unexpected(error(RuntimeErrorCode::DataFailure, 0, 0, "no sequence program"));
        auto node = create(
            std::move(program), 0, nullptr, priority, options, labelOverride);
        if (!node) return std::unexpected(node.error());
        if (initialTransform) move(**node, *initialTransform);
        const auto id = (*node)->id;
        insert(roots_, std::move(*node));
        return id;
    }
    void SequenceRuntime::destroyChildren(Node& node)
    {
        for (auto& child : node.children) destroy(child);
        node.children.clear();
    }
    void SequenceRuntime::destroy(std::unique_ptr<Node>& node)
    {
        destroyChildren(*node);
        if (node->labelNumber != 0 &&
            labelOwners_[node->labelNumber] == node->id)
            labelOwners_[node->labelNumber] = 0;
        emit(SequenceEventKind::Destroyed, *node);
        --liveNodes_;
        node.reset();
    }
    void SequenceRuntime::clearForest()
    {
        for (auto& node : roots_) destroy(node);
        roots_.clear();
    }
    void SequenceRuntime::stopAll() { events_.clear(); clearForest(); }
    void SequenceRuntime::erase(Node& node)
    {
        auto& siblings = node.parent ? node.parent->children : roots_;
        const auto position = std::find_if(siblings.begin(), siblings.end(),
            [&](const auto& current) { return current.get() == &node; });
        destroy(*position);
        siblings.erase(position);
    }
    SequenceRuntime::Node* SequenceRuntime::find(SequenceNodeId id) const
    {
        const auto visit = [&](const auto& self, const Nodes& nodes) -> Node* {
            for (const auto& node : nodes)
            {
                if (node->id == id) return node.get();
                if (auto* child = self(self, node->children)) return child;
            }
            return nullptr;
        };
        return visit(visit, roots_);
    }
    void SequenceRuntime::forceAncestors(Node& node)
    { for (auto* current = &node; current; current = current->parent) current->reevaluate = true; }
    void SequenceRuntime::forceDescendants(Node& node)
    {
        node.reevaluate = true;
        node.redrawRequested = true;
        for (auto& child : node.children) forceDescendants(*child);
    }
    void SequenceRuntime::clearRedrawFlags()
    {
        const auto visit = [&](const auto& self, Nodes& nodes) -> void {
            for (auto& node : nodes)
            {
                node->needsRedraw = false;
                self(self, node->children);
            }
        };
        visit(visit, roots_);
    }
    void SequenceRuntime::move(Node& node, const SequenceTransform& transform)
    {
        bool changed = true;
        if (node.dimensionality == 2)
        {
            if (const auto* matrix = std::get_if<Matrix2D>(&transform))
            {
                const auto& current = std::get<Matrix2D>(node.localTransform);
                changed = std::memcmp(current.values.data(), matrix->values.data(),
                    sizeof(current.values)) != 0;
                if (changed)
                {
                    node.localTransform = *matrix;
                    node.explicitlyPositioned = true;
                }
            }
            else
            {
                node.localTransform = identity2D();
                node.explicitlyPositioned = false;
            }
        }
        else if (node.dimensionality == 3)
        {
            if (const auto* matrix = std::get_if<Matrix3D>(&transform))
            {
                const auto& current = std::get<Matrix3D>(node.localTransform);
                changed = std::memcmp(current.values.data(), matrix->values.data(),
                    sizeof(current.values)) != 0;
                if (changed)
                {
                    node.localTransform = *matrix;
                    node.explicitlyPositioned = true;
                }
            }
            else
            {
                node.localTransform = identity3D();
                node.explicitlyPositioned = false;
            }
        }
        if (!changed) return;

        // MarkAsNeedingPositionRecalc marks this sequence and its ancestors;
        // descendants are then reevaluated by the recursive update. Preserve
        // that observable effect even when a descendant cadence would gate it.
        forceAncestors(node);
        forceDescendants(node);
    }
    std::expected<void, RuntimeError> SequenceRuntime::stop(SequenceNodeId id)
    {
        events_.clear();
        auto* node = find(id);
        if (!node) return std::unexpected(error(RuntimeErrorCode::InvalidHandle, 0, 0, "node no longer exists"));
        erase(*node);
        return {};
    }
    std::expected<void, RuntimeError> SequenceRuntime::requestVideoClock(
        SequenceNodeId id, std::int32_t mediaClock, std::int32_t duration, bool ended)
    {
        auto* node = find(id);
        if (!node) return std::unexpected(error(RuntimeErrorCode::InvalidHandle, 0, 0,
            "video clock references an inactive sequence"));
        const auto supplied = node->clock.supplyVideoClock(mediaClock, duration, ended);
        if (!supplied) return std::unexpected(caused(RuntimeErrorCode::ClockFailure,
            node->definition().dataId, node->definition().record.chunk.headerOffset, supplied.error()));
        // Do not emit/clear events here. The unique update publishes lifecycle
        // after queued commands, preserving ReachedEnd/Destroyed notifications.
        forceAncestors(*node);
        return {};
    }
    std::expected<void, RuntimeError> SequenceRuntime::setPaused(SequenceNodeId id, bool paused)
    {
        events_.clear();
        auto* node = find(id);
        if (!node) return std::unexpected(error(RuntimeErrorCode::InvalidHandle, 0, 0, "node no longer exists"));
        auto result = node->clock.setPaused(paused, node->parent ? node->parent->clock.clock() : parentClock_);
        if (!result) return std::unexpected(caused(RuntimeErrorCode::ClockFailure,
            node->definition().dataId, node->definition().record.chunk.headerOffset, result.error()));
        return {};
    }
    std::expected<void, RuntimeError> SequenceRuntime::setEndingAction(SequenceNodeId id, std::uint8_t action)
    {
        events_.clear();
        auto* node = find(id);
        if (!node) return std::unexpected(error(RuntimeErrorCode::InvalidHandle, 0, 0, "node no longer exists"));
        const auto result = node->clock.setEndingAction(action);
        if (!result) return std::unexpected(caused(RuntimeErrorCode::ClockFailure,
            node->definition().dataId, node->definition().record.chunk.headerOffset, result.error()));
        forceAncestors(*node);
        return {};
    }

    std::expected<void, RuntimeError> SequenceRuntime::setVolume(
        SequenceNodeId id, std::uint8_t volume)
    {
        events_.clear();
        auto* node = find(id);
        if (!node)
            return std::unexpected(error(
                RuntimeErrorCode::InvalidHandle, 0, 0,
                "node no longer exists"));

        const auto& data = node->definition().record.data;
        if (std::holds_alternative<data::SequenceSoundData>(data) ||
            std::holds_alternative<data::SequenceVideoData>(data))
            node->volume = std::min<std::uint8_t>(volume, 100U);
        return {};
    }
    std::expected<void, RuntimeError>
    SequenceRuntime::setScrollingWorldVisibility(
        SequenceNodeId id, bool onScreen)
    {
        auto* node = find(id);
        if (!node)
            return std::unexpected(error(
                RuntimeErrorCode::InvalidHandle, 0, 0,
                "scrolling-world visibility references an inactive sequence"));
        if (!node->definition().record.header.scrollingWorld)
            return {};
        if (node->scrollingOnScreen == onScreen)
            return {};

        node->scrollingOnScreen = onScreen;
        node->scrollingHibernating = false;
        node->reevaluate = true;
        forceAncestors(*node);
        return {};
    }

    std::expected<void, RuntimeError> SequenceRuntime::birthChildren(Node& node,
        std::optional<std::int32_t> previous)
    {
        const auto selected = node.schedule.select(previous, node.clock.clock());
        if (!selected) return std::unexpected(caused(RuntimeErrorCode::ClockFailure,
            node.definition().dataId, node.definition().record.chunk.headerOffset, selected.error()));
        for (const auto index : *selected)
        {
            const auto description = node.definition().childDescriptions[index];
            const auto priority = node.program->descriptions()[description].record.header.priority;
            auto child = create(node.program, description, &node, priority, {});
            if (!child) return std::unexpected(child.error());
            insert(node.children, std::move(*child));
        }
        return {};
    }
    std::expected<void, RuntimeError> SequenceRuntime::rebuildChildren(Node& node)
    {
        destroyChildren(node);
        node.schedule.rewind();
        auto born = birthChildren(node, std::nullopt);
        if (!born) return born;
        for (auto iterator = node.children.begin(); iterator != node.children.end();)
        {
            auto& child = **iterator;
            const auto time = static_cast<std::int64_t>(node.clock.clock()) -
                child.definition().record.header.parentStartTime;
            if (time < std::numeric_limits<std::int32_t>::min() || time > std::numeric_limits<std::int32_t>::max())
                return std::unexpected(caused(RuntimeErrorCode::ClockFailure,
                    child.definition().dataId, child.definition().record.chunk.headerOffset, ClockError::ClockOverflow));
            const auto alive = seekNode(child, static_cast<std::int32_t>(time));
            if (!alive) return std::unexpected(alive.error());
            if (!*alive) { destroy(*iterator); iterator = node.children.erase(iterator); }
            else ++iterator;
        }
        return {};
    }
    std::expected<bool, RuntimeError> SequenceRuntime::seekNode(Node& node, std::int32_t time)
    {
        const auto result = node.clock.seek(time, node.parent ? node.parent->clock.clock() : parentClock_);
        if (result.stopped) return false;
        node.reevaluate = true;
        emit(SequenceEventKind::Rewound, node);
        const auto rebuilt = rebuildChildren(node);
        if (!rebuilt) return std::unexpected(rebuilt.error());
        return true;
    }
    std::expected<void, RuntimeError> SequenceRuntime::seek(SequenceNodeId id, std::int32_t time)
    {
        events_.clear(); births_ = 0;
        auto* node = find(id);
        if (!node) return std::unexpected(error(RuntimeErrorCode::InvalidHandle, 0, 0, "node no longer exists"));
        forceAncestors(*node);
        const auto alive = seekNode(*node, time);
        if (!alive) { clearForest(); return std::unexpected(alive.error()); }
        if (!*alive) erase(*node);
        return {};
    }
    std::expected<bool, RuntimeError> SequenceRuntime::updateNode(Node& node, std::int32_t parentClock)
    {
        node.needsRedraw = node.redrawRequested;
        node.redrawRequested = false;

        if (node.definition().record.header.scrollingWorld &&
            !node.scrollingOnScreen)
        {
            if (!node.scrollingHibernating)
            {
                destroyChildren(node);
                node.schedule.rewind();
                node.scrollingHibernating = true;
            }
            node.clock.hibernateScrollingWorld(parentClock);
            node.needsRedraw = false;
            node.reevaluate = false;
            return true;
        }

        const auto tick = node.clock.update(parentClock, node.reevaluate);
        if (!tick) return std::unexpected(caused(RuntimeErrorCode::ClockFailure,
            node.definition().dataId, node.definition().record.chunk.headerOffset, tick.error()));
        if (!tick->updated) return true; // gated parent gates descendants too
        emit(SequenceEventKind::Updated, node);
        if (tick->hitEnd) emit(SequenceEventKind::ReachedEnd, node);
        if (tick->stopped) return false; // children do not get a final tick
        const auto children = tick->restartChildren ? rebuildChildren(node) : birthChildren(node, tick->previousClock);
        if (!children) return std::unexpected(children.error());
        if (node.definition().record.chunk.id == 10)
        {
            const auto applied = applyTweeker(node);
            if (!applied) return std::unexpected(applied.error());
        }

        // Tweeker children run before their parent's position calculation.
        for (auto iterator = node.children.begin(); iterator != node.children.end();)
        {
            if ((*iterator)->definition().record.chunk.id != 10) { ++iterator; continue; }
            const auto alive = updateNode(**iterator, node.clock.clock());
            if (!alive) return std::unexpected(alive.error());
            if (!*alive) { destroy(*iterator); iterator = node.children.erase(iterator); }
            else ++iterator;
        }
        const auto effectiveLocal = applyTweekerBeforeLocal(node.tweekerTransform,
            node.tweekerTransformApplied, node.localTransform, node.dimensionality);
        node.worldTransform = composeSequenceWorld(effectiveLocal,
            node.dimensionality,
            node.parent ? node.parent->worldTransform : SequenceTransform(std::monostate{}),
            node.parent ? node.parent->dimensionality : 0);
        for (auto iterator = node.children.begin(); iterator != node.children.end();)
        {
            if ((*iterator)->definition().record.chunk.id == 10) { ++iterator; continue; }
            const auto alive = updateNode(**iterator, node.clock.clock());
            if (!alive) return std::unexpected(alive.error());
            if (!*alive) { destroy(*iterator); iterator = node.children.erase(iterator); }
            else
            {
                if ((*iterator)->needsRedraw) node.needsRedraw = true;
                ++iterator;
            }
        }
        node.reevaluate = false;
        return true;
    }
    std::expected<void, RuntimeError> SequenceRuntime::applyTweeker(Node& node)
    {
        if (!node.parent) return std::unexpected(error(RuntimeErrorCode::TweekerFailure,
            node.definition().dataId, node.definition().record.chunk.headerOffset,
            "a tweeker must be a child of the sequence it modifies"));
        const auto& tweeker = std::get<data::SequenceTweekerData>(node.definition().record.data);
        const auto evaluated = evaluateTweekerTransform(node.definition().attributes,
            tweeker.interpolationType, node.clock.clock(), node.clock.endTime(),
            node.parent->dimensionality);
        if (!evaluated)
            return std::unexpected(error(RuntimeErrorCode::TweekerFailure,
                node.definition().dataId, node.definition().record.chunk.headerOffset,
                evaluated.error() == TweekerTransformError::InvalidInterpolation ?
                    "tweeker interpolation type is not implemented" :
                    "tweeker transform dimensionality does not match its parent"));

        const auto meshChoice = evaluateTweekerMeshChoice(node.definition().attributes,
            tweeker.interpolationType, node.clock.clock(), node.clock.endTime(),
            node.parent->dimensionality);
        if (!meshChoice)
            return std::unexpected(error(RuntimeErrorCode::TweekerFailure,
                node.definition().dataId, node.definition().record.chunk.headerOffset,
                "3D mesh-choice tweeker dimensionality does not match its parent"));
        if (evaluated->changed)
        {
            node.parent->tweekerTransformApplied = !evaluated->identity;
            node.parent->tweekerTransform = evaluated->transform;
        }
        if (*meshChoice)
            node.parent->meshChoice = **meshChoice;
        const auto cameraFov = evaluateTweekerCameraFieldOfView(
            node.definition().attributes, tweeker.interpolationType,
            node.clock.clock(), node.clock.endTime(),
            node.parent->definition().record.chunk.id == 7);
        if (!cameraFov)
            return std::unexpected(error(RuntimeErrorCode::TweekerFailure,
                node.definition().dataId, node.definition().record.chunk.headerOffset,
                "camera field-of-view tweeker must target a camera sequence"));
        if (*cameraFov) node.parent->cameraFieldOfView = **cameraFov;

        const auto& parentData = node.parent->definition().record.data;
        const bool parentIsAudio =
            std::holds_alternative<data::SequenceSoundData>(parentData) ||
            std::holds_alternative<data::SequenceVideoData>(parentData);
        const auto audio = evaluateTweekerAudio(
            node.definition().attributes, tweeker.interpolationType,
            node.clock.clock(), node.clock.endTime(), parentIsAudio);
        if (!audio)
            return std::unexpected(error(RuntimeErrorCode::TweekerFailure,
                node.definition().dataId,
                node.definition().record.chunk.headerOffset,
                "sound tweeker must target a sound or video sequence"));
        if (audio->pitch) node.parent->pitch = *audio->pitch;
        if (audio->volume) node.parent->volume = *audio->volume;
        if (audio->panning) node.parent->panning = *audio->panning;
        return {};
    }
    std::expected<void, RuntimeError> SequenceRuntime::update(std::int32_t parentClock)
    {
        events_.clear(); births_ = 0;
        if (clockStarted_ && parentClock < parentClock_)
            return std::unexpected(caused(RuntimeErrorCode::ClockFailure, 0, 0, ClockError::ParentClockWentBackwards));
        clearRedrawFlags();
        parentClock_ = parentClock; clockStarted_ = true;
        for (auto iterator = roots_.begin(); iterator != roots_.end();)
        {
            const auto alive = updateNode(**iterator, parentClock);
            if (!alive) { clearForest(); return std::unexpected(alive.error()); }
            if (!*alive) { destroy(*iterator); iterator = roots_.erase(iterator); }
            else ++iterator;
        }
        return {};
    }
    std::vector<SequenceNodeId> SequenceRuntime::matching(data::DataId id,
        std::uint16_t priority, bool wholeTree) const
    {
        std::vector<SequenceNodeId> matches;
        const auto visit = [&](const auto& self, const Nodes& nodes) -> void {
            for (const auto& node : nodes)
            {
                if (node->definition().dataId == id && node->priority == priority &&
                    node->definition().record.chunk.headerOffset == 0) matches.push_back(node->id);
                if (wholeTree) self(self, node->children);
            }
        };
        visit(visit, roots_);
        return matches;
    }
    std::size_t SequenceRuntime::stopMatching(data::DataId id,
        std::uint16_t priority, bool wholeTree)
    {
        events_.clear();
        const auto matches = matching(id, priority, wholeTree);
        std::size_t stopped{};
        for (const auto match : matches)
        {
            // A matching ancestor deletes matching descendants with it.
            // FindNextSequence likewise resumes from the surviving tree.
            if (auto* node = find(match))
            {
                erase(*node);
                ++stopped;
            }
        }
        return stopped;
    }
    std::expected<std::size_t, RuntimeError> SequenceRuntime::setEndingActionMatching(
        data::DataId id, std::uint16_t priority, std::uint8_t action, bool wholeTree)
    {
        events_.clear();
        if (action == 0 || action > 3)
            return std::unexpected(caused(RuntimeErrorCode::ClockFailure, id, 0, ClockError::InvalidEndingAction));
        const auto matches = matching(id, priority, wholeTree);
        for (const auto match : matches)
        {
            auto* node = find(match);
            (void)node->clock.setEndingAction(action);
            forceAncestors(*node);
        }
        return matches.size();
    }

    std::size_t SequenceRuntime::setVolumeMatching(
        data::DataId id, std::uint16_t priority,
        std::uint8_t volume, bool wholeTree)
    {
        events_.clear();
        const auto matches = matching(id, priority, wholeTree);
        const auto clamped = std::min<std::uint8_t>(volume, 100U);
        for (const auto match : matches)
        {
            auto* node = find(match);
            if (!node) continue;
            const auto& payload = node->definition().record.data;
            if (std::holds_alternative<data::SequenceSoundData>(payload) ||
                std::holds_alternative<data::SequenceVideoData>(payload))
                node->volume = clamped;
        }
        return matches.size();
    }

    std::size_t SequenceRuntime::moveMatching(data::DataId id,
        std::uint16_t priority, const SequenceTransform& transform, bool wholeTree)
    {
        events_.clear();
        const auto matches = matching(id, priority, wholeTree);
        for (const auto match : matches)
            if (auto* node = find(match)) move(*node, transform);
        // FindNextSequence counts a target even when MoveSequence's exact
        // matrix comparison makes the individual mutation a no-op.
        return matches.size();
    }
    std::size_t SequenceRuntime::forceRedrawMatching(data::DataId id,
        std::uint16_t priority, bool wholeTree)
    {
        events_.clear();
        const auto matches = matching(id, priority, wholeTree);
        for (const auto match : matches)
            if (auto* node = find(match))
            {
                node->redrawRequested = true;
                forceAncestors(*node);
            }
        return matches.size();
    }

    std::optional<SequenceNodeView> SequenceRuntime::inspect(SequenceNodeId id) const
    {
        const auto* node = find(id);
        if (!node) return std::nullopt;
        SequenceNodeView view{node->id, node->parent ? node->parent->id : 0,
            node->definition().dataId, node->definition().record.chunk.headerOffset, node->priority,
            node->labelNumber, node->clock.clock(), node->clock.endTime(),
            node->clock.timeMultiple(), node->clock.paused(),
            node->needsRedraw, node->dimensionality, node->definition().contentsDataId, node->explicitlyPositioned,
            node->localTransform, node->tweekerTransformApplied,
            node->tweekerTransform, node->worldTransform, {}};
        view.meshChoice = node->meshChoice;
        view.pitch = node->pitch;
        view.volume = node->volume;
        view.panning = node->panning;
        for (const auto& child : node->children) view.children.push_back(child->id);
        return view;
    }
    std::optional<SequenceInfoView> SequenceRuntime::info(data::DataId id,
        std::uint16_t priority, bool wholeTree) const
    {
        const auto matches = matching(id, priority, wholeTree);
        if (matches.empty()) return std::nullopt;
        const auto* node = find(matches.front());
        if (!node) return std::nullopt;

        SequenceInfoView result{node->id, node->clock.clock(), node->clock.endTime(),
            node->dimensionality, std::nullopt};
        if (node->dimensionality == 3)
            if (const auto* matrix = std::get_if<Matrix3D>(&node->worldTransform))
                result.sequenceToWorldTransformation = *matrix;
        return result;
    }

    bool SequenceRuntime::isSequenceFinished(
        data::DataId id, std::uint16_t priority, bool wholeTree) const
    {
        const auto matches = matching(id, priority, wholeTree);
        if (matches.empty()) return true;

        const auto* node = find(matches.front());
        if (!node) return true;
        return node->clock.clock() >= node->clock.endTime();
    }


    std::optional<Matrix3D> SequenceRuntime::childMeshWorldMatrix(
        data::DataId id, std::uint16_t priority) const
    {
        const auto roots = matching(id, priority, false);
        if (roots.empty()) return std::nullopt;
        const auto* selected = find(roots.front());
        if (!selected) return std::nullopt;

        const auto visit = [&](const auto& self, const Node& node) -> std::optional<Matrix3D>
        {
            if (node.definition().record.chunk.id == 9 && node.dimensionality == 3)
                if (const auto* matrix = std::get_if<Matrix3D>(&node.worldTransform))
                    return *matrix;
            for (const auto& child : node.children)
                if (auto found = self(self, *child)) return found;
            return std::nullopt;
        };
        return visit(visit, *selected);
    }

    std::vector<SequenceBitmapInstanceView> SequenceRuntime::bitmapInstances() const
    {
        std::vector<SequenceBitmapInstanceView> result;
        const auto visit = [&](const auto& self, const Nodes& nodes) -> void {
            for (const auto& node : nodes)
            {
                const auto& definition = node->definition();
                if (definition.contentsDataId && node->dimensionality == 2 &&
                    std::holds_alternative<data::SequenceBitmapData>(definition.record.data) &&
                    std::holds_alternative<Matrix2D>(node->worldTransform))
                    result.push_back({node->id, *definition.contentsDataId,
                        node->priority, node->clock.clock(),
                        std::get<Matrix2D>(node->worldTransform)});
                self(self, node->children);
            }
        };
        visit(visit, roots_);
        return result;
    }


    std::vector<SequenceSoundInstanceView> SequenceRuntime::soundInstances() const
    {
        std::vector<SequenceSoundInstanceView> result;
        const auto visit = [&](const auto& self, const Nodes& nodes) -> void {
            for (const auto& node : nodes)
            {
                const auto& definition = node->definition();
                if (definition.contentsDataId &&
                    std::holds_alternative<data::SequenceSoundData>(definition.record.data))
                {
                    std::optional<std::int32_t> centerX;
                    if (node->dimensionality == 2 &&
                        std::holds_alternative<Matrix2D>(node->worldTransform))
                        centerX = soundCenterX2D(
                            definition.attributes,
                            std::get<Matrix2D>(node->worldTransform));
                    result.push_back({node->id, *definition.contentsDataId,
                        node->priority, node->clock.clock(),
                        node->clock.endingAction(), node->dimensionality,
                        node->pitch, node->volume, node->panning, centerX});
                }
                self(self, node->children);
            }
        };
        visit(visit, roots_);
        return result;
    }

    std::vector<SequenceVideoInstanceView> SequenceRuntime::videoInstances() const
    {
        std::vector<SequenceVideoInstanceView> result;
        const auto visit = [&](const auto& self, const Nodes& nodes) -> void {
            for (const auto& node : nodes)
            {
                const auto& definition = node->definition();
                if (std::holds_alternative<data::SequenceVideoData>(
                        definition.record.data) &&
                    node->dimensionality == 2 &&
                    std::holds_alternative<Matrix2D>(node->worldTransform))
                {
                    result.push_back({
                        node->id,
                        node->priority,
                        node->clock.clock(),
                        std::get<data::SequenceVideoData>(definition.record.data),
                        videoFileName(definition.attributes),
                        boundingBox2D(definition.attributes),
                        std::get<Matrix2D>(node->worldTransform),
                        node->clock.endingAction(), definition.binkDoubleSize,
                        node->clock.elapsedParentClock(), node->pitch,
                        node->volume, node->panning});
                }
                self(self, node->children);
            }
        };
        visit(visit, roots_);
        return result;
    }

    std::vector<SequenceMeshInstanceView> SequenceRuntime::meshInstances() const
    {
        std::vector<SequenceMeshInstanceView> result;
        const auto visit = [&](const auto& self, const Nodes& nodes) -> void {
            for (const auto& node : nodes)
            {
                const auto& definition = node->definition();
                if (definition.record.chunk.id == 9 && definition.contentsDataId &&
                    node->dimensionality == 3 &&
                    std::holds_alternative<Matrix3D>(node->worldTransform))
                {
                    result.push_back({node->id, *definition.contentsDataId,
                        node->priority, node->clock.clock(),
                        std::get<Matrix3D>(node->worldTransform), node->meshChoice,
                        initialBounds3D(definition.attributes, node->dimensionality)});
                }
                self(self, node->children);
            }
        };
        visit(visit, roots_);
        return result;
    }

    std::vector<SequenceScrollingWorldView>
    SequenceRuntime::scrollingWorldInstances() const
    {
        std::vector<SequenceScrollingWorldView> result;
        const auto visit = [&](const auto& self, const Nodes& nodes) -> void {
            for (const auto& node : nodes)
            {
                const auto& definition = node->definition();
                if (definition.record.header.scrollingWorld)
                {
                    result.push_back({
                        node->id,
                        node->dimensionality,
                        node->worldTransform,
                        node->dimensionality == 2
                            ? boundingBox2D(definition.attributes)
                            : std::nullopt,
                        initialBounds3D(
                            definition.attributes, node->dimensionality),
                        node->scrollingOnScreen});
                }
                self(self, node->children);
            }
        };
        visit(visit, roots_);
        return result;
    }

    std::vector<SequenceCamera3DView> SequenceRuntime::cameraInstances() const
    {
        std::vector<SequenceCamera3DView> result;
        const auto visit = [&](const auto& self, const Nodes& nodes) -> void {
            for (const auto& node : nodes)
            {
                const auto& definition = node->definition();
                if (definition.record.chunk.id == 7 && node->dimensionality == 3 &&
                    std::holds_alternative<Matrix3D>(node->worldTransform))
                {
                    const auto& camera = std::get<data::SequenceCameraData>(
                        definition.record.data);
                    result.push_back({node->id, camera.cameraLabel, node->priority,
                        node->clock.clock(), std::get<Matrix3D>(node->worldTransform),
                        node->cameraFieldOfView, camera.nearClipPlaneDistance,
                        camera.farClipPlaneDistance});
                }
                self(self, node->children);
            }
        };
        visit(visit, roots_);
        return result;
    }

    std::optional<SequenceCamera3DView> SequenceRuntime::cameraForLabel(
        std::uint8_t label) const
    {
        if (label == 0) return std::nullopt;
        const auto owner = labelOwners_[label];
        const auto* node = owner != 0 ? find(owner) : nullptr;
        if (!node || node->definition().record.chunk.id != 7 ||
            node->dimensionality != 3 ||
            !std::holds_alternative<Matrix3D>(node->worldTransform))
            return std::nullopt;
        const auto& camera = std::get<data::SequenceCameraData>(
            node->definition().record.data);
        return SequenceCamera3DView{node->id, node->labelNumber, node->priority,
            node->clock.clock(), std::get<Matrix3D>(node->worldTransform),
            node->cameraFieldOfView, camera.nearClipPlaneDistance,
            camera.farClipPlaneDistance};
    }

    std::vector<SequenceNodeId> SequenceRuntime::roots() const
    {
        std::vector<SequenceNodeId> result;
        for (const auto& root : roots_) result.push_back(root->id);
        return result;
    }
}
