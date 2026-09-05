#include "LegacySequence.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <variant>

namespace
{
    using namespace monopoly::data;

    int failures = 0;

    void expect(bool condition, std::string_view description)
    {
        if (condition)
        {
            std::cout << "[PASS] " << description << '\n';
            return;
        }
        ++failures;
        std::cerr << "[FAIL] " << description << '\n';
    }

    // Hand-authored Win32 bytes from the fields in L_Seqncr.h:473-558.
    // start=-2, priority=5, end=120, multiple=4, loop, relative DataIDs.
    const DataBytes CommonHeader{
        std::byte{ 0xFE }, std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0x05 },
        std::byte{ 0x78 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x04 },
        std::byte{ 0x03 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 }
    };

    void appendU32(DataBytes& bytes, std::uint32_t value)
    {
        for (unsigned shift = 0; shift != 32; shift += 8)
        {
            bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
        }
    }

    DataBytes chunk(std::uint8_t id, std::span<const std::byte> payload)
    {
        const auto size = static_cast<std::uint32_t>(payload.size() + 4);
        DataBytes result{
            static_cast<std::byte>(size & 0xFFU),
            static_cast<std::byte>((size >> 8U) & 0xFFU),
            static_cast<std::byte>((size >> 16U) & 0xFFU),
            static_cast<std::byte>(id)
        };
        result.insert(result.end(), payload.begin(), payload.end());
        return result;
    }

    void testPackedHeader()
    {
        const DataBytes limits{
            std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x80 }, std::byte{ 0x7F },
            std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0x7F }, std::byte{ 0xFF },
            std::byte{ 0xFB }, std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0xFF }
        };
        const auto header = decodeLegacySequenceHeader(limits);
        expect(header && header->parentStartTime == -8'388'608 &&
            header->priority == 127 && header->endTime == 8'388'607,
            "signed 24-bit clocks do not include the following priority/rate bits");
        expect(header && header->timeMultiple == 63 && header->dropFrames &&
            header->lastUse && header->endingAction == 3 &&
            header->scrollingWorld && header->absoluteDataIds &&
            header->reserved == 0x07FF'FFFFU,
            "all three packed words retain their documented bit boundaries");

        const DataBytes negativeEnd{
            std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0x7F }, std::byte{ 0xFF },
            std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x80 }, std::byte{ 0x01 },
            std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 }
        };
        const auto other = decodeLegacySequenceHeader(negativeEnd);
        expect(other && other->parentStartTime == 8'388'607 &&
            other->priority == 255 && other->endTime == -8'388'608 &&
            other->timeMultiple == 1 && !other->dropFrames && !other->lastUse,
            "both signed clocks sign-extend independently at their extrema");

        DataBytes unnormalised(12, std::byte{ 0 });
        unnormalised[8] = std::byte{ 0x07 };
        const auto raw = decodeLegacySequenceHeader(unnormalised);
        expect(raw && raw->endTime == 0 && raw->timeMultiple == 0 &&
            raw->endingAction == 7,
            "decoding retains infinite-time, replacement-rate and unknown action values");

        for (const auto [byteIndex, value, flag] : std::array{
            std::array<unsigned, 3>{ 7, 0x40, 0 },
            std::array<unsigned, 3>{ 7, 0x80, 1 },
            std::array<unsigned, 3>{ 8, 0x08, 2 },
            std::array<unsigned, 3>{ 8, 0x10, 3 } })
        {
            DataBytes oneFlag(12, std::byte{ 0 });
            oneFlag[byteIndex] = static_cast<std::byte>(value);
            const auto parsed = decodeLegacySequenceHeader(oneFlag);
            expect(parsed && parsed->dropFrames == (flag == 0) &&
                parsed->lastUse == (flag == 1) &&
                parsed->scrollingWorld == (flag == 2) &&
                parsed->absoluteDataIds == (flag == 3) &&
                parsed->timeMultiple == 0 && parsed->endingAction == 0 &&
                parsed->reserved == 0,
                "an isolated flag does not contaminate adjacent fields");
        }

        for (std::size_t length = 0; length < 12; ++length)
        {
            const auto shortHeader = decodeLegacySequenceHeader(
                std::span<const std::byte>(CommonHeader).first(length));
            expect(!shortHeader && shortHeader.error().code ==
                SequenceErrorCode::HeaderTruncated,
                "each incomplete common header is rejected before reading fields");
        }
    }

    void testFixedRecords()
    {
        for (const auto id : std::array<std::uint8_t, 8>{ 1, 2, 3, 4, 5, 7, 9, 10 })
        {
            DataBytes payload = CommonHeader;
            if (id == 10)
            {
                payload.push_back(std::byte{2});
            }
            else if (id == 7)
            {
                appendU32(payload, 0x4000'0000U); // near = 2.0f
                appendU32(payload, 0x4480'0000U); // far = 1024.0f
                payload.push_back(std::byte{7});
            }
            else if (id != 1)
            {
                appendU32(payload, 0x1234'BCDEU);
            }
            if (id == 4)
            {
                appendU32(payload, 0x4567'89ABU);
                appendU32(payload, 0xFFFF'0000U);
            }
            const auto bytes = chunk(id, payload);
            LegacyChunkReader reader(bytes);
            const auto record = readLegacySequenceRecord(reader);
            expect(record && record->chunk.id == id &&
                record->header.parentStartTime == -2 &&
                record->header.priority == 5 && record->header.endTime == 120 &&
                record->header.timeMultiple == 4 &&
                record->subchunksOffset == bytes.size() && reader.remaining() == 0,
                "supported fixed record consumes exactly its documented size");
            if (!record)
            {
                continue;
            }
            switch (id)
            {
            case 1:
                expect(std::holds_alternative<SequenceGroupingData>(record->data),
                    "grouping has no extra fixed payload");
                break;
            case 2:
                expect(std::get<SequenceIndirectData>(record->data).subsequenceDataId ==
                    0x1234'BCDEU, "indirect subsequence DataID retains its raw group and tag");
                break;
            case 3:
                expect(std::get<SequenceBitmapData>(record->data).bitmapDataId ==
                    0x1234'BCDEU, "bitmap DataID is decoded little-endian");
                break;
            case 4:
            {
                const auto& model = std::get<SequenceModelData>(record->data);
                expect(model.modelDataId == 0x1234'BCDEU &&
                    model.textureMapDataId == 0x4567'89ABU &&
                    model.jointPositionsDataId == 0xFFFF'0000U,
                    "model geometry, texture table and joint DataIDs remain distinct");
                break;
            }
            case 5:
                expect(std::get<SequenceSoundData>(record->data).soundDataId ==
                    0x1234'BCDEU, "sound DataID is decoded little-endian");
                break;
            case 7:
            {
                const auto& camera = std::get<SequenceCameraData>(record->data);
                expect(camera.nearClipPlaneDistance == 2.0F &&
                    camera.farClipPlaneDistance == 1024.0F && camera.cameraLabel == 7,
                    "camera near/far planes and packed label decode from the 21-byte record");
                break;
            }
            case 9:
                expect(std::get<SequenceMeshData>(record->data).modelDataId ==
                    0x1234'BCDEU, "MESHX sequence retains its model DataID");
                break;
            case 10:
                expect(std::get<SequenceTweekerData>(record->data).interpolationType == 2,
                    "tweeker interpolation ID is decoded without executing it");
                break;
            }

            for (std::size_t length = 0; length < payload.size(); ++length)
            {
                const auto shortBytes = chunk(id,
                    std::span<const std::byte>(payload).first(length));
                LegacyChunkReader shortReader(shortBytes);
                const auto shortRecord = readLegacySequenceRecord(shortReader);
                expect(!shortRecord && shortRecord.error().code ==
                    SequenceErrorCode::FixedRecordTruncated &&
                    shortRecord.error().offset == 4 &&
                    shortReader.level() == 0 && shortReader.currentOffset() == 0,
                    "truncated fixed payload leaves the caller's reader unchanged");
            }
        }
    }

    void testTraversalOwnershipAndErrors()
    {
        DataBytes bitmapPayload = CommonHeader;
        appendU32(bitmapPayload, 0x0002'000AU);
        DataBytes groupingPayload = CommonHeader;
        const auto bitmap = chunk(3, bitmapPayload);
        groupingPayload.insert(groupingPayload.end(), bitmap.begin(), bitmap.end());
        const auto bytes = chunk(1, groupingPayload);
        SharedDataBytes owner = std::make_shared<const DataBytes>(bytes);
        std::weak_ptr<const DataBytes> weakOwner = owner;
        LegacyChunkReader reader(owner);
        owner.reset();
        const auto grouping = readLegacySequenceRecord(reader);
        expect(grouping && grouping->subchunksOffset == 16 &&
            reader.currentOffset() == 16 && reader.level() == 1 && !weakOwner.expired(),
            "owning reader keeps bytes alive and grouping stops before its first child");
        const auto child = readLegacySequenceRecord(reader);
        expect(child && child->chunk.headerOffset == 16 &&
            child->subchunksOffset == bytes.size() && reader.level() == 2,
            "sequence records can be decoded through nested chunk traversal");
        expect(reader.ascend().has_value(), "child can be ascended through the existing chunk API");
        const auto position = reader.currentOffset();
        const auto end = readLegacySequenceRecord(reader);
        expect(!end && end.error().code == SequenceErrorCode::ChunkFailure &&
            end.error().chunkError && end.error().chunkError->code ==
                ChunkErrorCode::EndOfParent &&
            reader.currentOffset() == position && reader.level() == 1,
            "end-of-parent error is preserved without changing traversal state");

        for (const auto id : std::array<std::uint8_t, 4>{ 6, 8, 20, 129 })
        {
            const auto unsupportedBytes = chunk(id, CommonHeader);
            LegacyChunkReader unsupportedReader(unsupportedBytes);
            const auto unsupported = readLegacySequenceRecord(unsupportedReader);
            expect(!unsupported && unsupported.error().code ==
                SequenceErrorCode::UnsupportedRecord &&
                unsupportedReader.level() == 0 && unsupportedReader.currentOffset() == 0,
                "unported sequence and non-sequence records are explicitly unsupported");
        }

        auto overrunBytes = bytes;
        overrunBytes[0] = std::byte{ 0xFF };
        LegacyChunkReader overrunReader(overrunBytes);
        const auto overrun = readLegacySequenceRecord(overrunReader);
        expect(!overrun && overrun.error().chunkError &&
            overrun.error().chunkError->code == ChunkErrorCode::ChunkPastParent &&
            overrunReader.currentOffset() == 0,
            "physical chunk bounds are enforced by LegacyChunkReader");
    }

    void testMeshChoiceAttribute()
    {
        const DataBytes meshChoicePayload{
            std::byte{0xFE}, std::byte{0xFF}, // meshIndexA = -2
            std::byte{0x07}, std::byte{0x00}, // meshIndexB = 7
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xBF} // -0.5f
        };
        DataBytes payload = CommonHeader;
        const auto attribute = chunk(139, meshChoicePayload);
        payload.insert(payload.end(), attribute.begin(), attribute.end());
        const auto bytes = chunk(1, payload);
        LegacyChunkReader reader(bytes);
        expect(readLegacySequenceRecord(reader).has_value(),
            "grouping fixture positions reader before private mesh-choice attribute");
        const auto attributes = readLegacySequenceAttributes(reader);
        const auto* choice = attributes && attributes->values.size() == 1 ?
            std::get_if<Sequence3DMeshChoiceAttribute>(&attributes->values.front()) : nullptr;
        expect(choice && choice->meshIndexA == -2 && choice->meshIndexB == 7 &&
            choice->meshProportion == -0.5F,
            "private chunk 139 decodes signed pose indices and unclamped float proportion");

        DataBytes shortPayload(meshChoicePayload.begin(), meshChoicePayload.end() - 1);
        DataBytes shortParent = CommonHeader;
        const auto shortAttribute = chunk(139, shortPayload);
        shortParent.insert(shortParent.end(), shortAttribute.begin(), shortAttribute.end());
        const auto shortBytes = chunk(1, shortParent);
        LegacyChunkReader shortReader(shortBytes);
        (void)readLegacySequenceRecord(shortReader);
        const auto truncated = readLegacySequenceAttributes(shortReader);
        expect(!truncated && truncated.error().code == SequenceErrorCode::AttributeTruncated,
            "seven-byte mesh-choice payload is rejected before reading its float");
    }
    void testCameraFieldOfViewAttribute()
    {
        DataBytes cameraPayload = CommonHeader;
        appendU32(cameraPayload, 0x3F80'0000U); // near = 1.0f
        appendU32(cameraPayload, 0x459C'4000U); // far = 5000.0f
        cameraPayload.push_back(std::byte{3});
        const DataBytes fovPayload{
            std::byte{0x00}, std::byte{0x00}, std::byte{0x40}, std::byte{0x3F} // 0.75f
        };
        const auto fov = chunk(144, fovPayload);
        cameraPayload.insert(cameraPayload.end(), fov.begin(), fov.end());
        const auto bytes = chunk(7, cameraPayload);
        LegacyChunkReader reader(bytes);
        expect(readLegacySequenceRecord(reader).has_value(),
            "camera fixture positions reader before private FOV attribute");
        const auto attributes = readLegacySequenceAttributes(reader);
        const auto* value = attributes && attributes->values.size() == 1 ?
            std::get_if<SequenceCameraFieldOfViewAttribute>(&attributes->values.front()) : nullptr;
        expect(value && value->fieldOfView == 0.75F,
            "private chunk 144 decodes camera field of view as an unclamped float");

        DataBytes shortParent = CommonHeader;
        appendU32(shortParent, 0x3F80'0000U);
        appendU32(shortParent, 0x459C'4000U);
        shortParent.push_back(std::byte{3});
        const DataBytes shortFovPayload{std::byte{0}, std::byte{0}, std::byte{0x40}};
        const auto shortFov = chunk(144, shortFovPayload);
        shortParent.insert(shortParent.end(), shortFov.begin(), shortFov.end());
        const auto shortBytes = chunk(7, shortParent);
        LegacyChunkReader shortReader(shortBytes);
        (void)readLegacySequenceRecord(shortReader);
        const auto truncated = readLegacySequenceAttributes(shortReader);
        expect(!truncated && truncated.error().code == SequenceErrorCode::AttributeTruncated,
            "three-byte camera FOV payload is rejected before reading its float");
    }

    void testDataIdResolution()
    {
        auto header = *decodeLegacySequenceHeader(CommonHeader);
        expect(resolveSequenceDataId(header, 0x1234'BCDEU, 0x0008'0001U) ==
            0x0008'BCDEU, "relative reference takes its group from the containing DAT item");
        expect(resolveSequenceDataId(header, EmptyDataId, 0x0008'0001U) ==
            0x0008'0000U,
            "relative zero follows the source helper; callers retain the raw external-file sentinel");
        header.absoluteDataIds = true;
        expect(resolveSequenceDataId(header, 0x1234'BCDEU, 0x0008'0001U) ==
            0x1234'BCDEU && resolveSequenceDataId(header, EmptyDataId, 0x0008'0001U) == 0,
            "absolute references retain the complete ID including the empty sentinel");
    }
}

int main()
{
    testPackedHeader();
    testFixedRecords();
    testTraversalOwnershipAndErrors();
    testMeshChoiceAttribute();
    testCameraFieldOfViewAttribute();
    testDataIdResolution();
    return failures == 0 ? 0 : 1;
}
