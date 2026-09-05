#include "LegacyMeshData.hpp"

#include <bit>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace monopoly::data
{
    namespace
    {
        constexpr std::uint32_t Flag = 0x8000'0000U;
        constexpr std::uint32_t EndOfChain = 0xFFFF'FFFFU;

        MeshDataError error(MeshDataErrorCode code, std::size_t offset,
            std::string detail)
        {
            return { code, offset, std::move(detail), std::nullopt };
        }

        bool fits(std::span<const std::byte> bytes, std::size_t offset,
            std::size_t count, std::size_t stride = 1) noexcept
        {
            return offset <= bytes.size() &&
                count <= (bytes.size() - offset) / stride;
        }

        std::uint32_t u32(std::span<const std::byte> bytes,
            std::size_t offset) noexcept
        {
            std::uint32_t result = 0;
            for (unsigned i = 0; i < 4; ++i)
                result |= std::to_integer<std::uint32_t>(bytes[offset + i])
                    << (i * 8U);
            return result;
        }

        std::expected<std::size_t, MeshDataError> wordOffset(
            std::span<const std::byte> bytes, std::uint32_t value,
            std::size_t fieldOffset, std::size_t minimumSize)
        {
            // Check before multiplying, including on a 32-bit target.
            if (value > bytes.size() / 4 ||
                !fits(bytes, static_cast<std::size_t>(value) * 4, minimumSize))
                return std::unexpected(error(MeshDataErrorCode::RangeOutOfBounds,
                    fieldOffset, "HMD word offset leaves the owning payload"));
            return static_cast<std::size_t>(value) * 4;
        }

        HmdShortVector shortVector(std::span<const std::byte> bytes,
            std::size_t offset) noexcept
        {
            const auto a = u32(bytes, offset);
            const auto b = u32(bytes, offset + 4);
            return {
                std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(a)),
                std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(a >> 16)),
                std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(b)),
                std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(b >> 16))
            };
        }
    }

    std::string_view meshDataErrorCodeName(MeshDataErrorCode code) noexcept
    {
        switch (code)
        {
        case MeshDataErrorCode::None: return "None";
        case MeshDataErrorCode::MissingPayload: return "MissingPayload";
        case MeshDataErrorCode::HeaderTruncated: return "HeaderTruncated";
        case MeshDataErrorCode::AlreadyMapped: return "AlreadyMapped";
        case MeshDataErrorCode::RangeOutOfBounds: return "RangeOutOfBounds";
        case MeshDataErrorCode::InvalidPrimitiveHeader: return "InvalidPrimitiveHeader";
        case MeshDataErrorCode::InvalidSectionSize: return "InvalidSectionSize";
        case MeshDataErrorCode::PrimitiveCycle: return "PrimitiveCycle";
        case MeshDataErrorCode::RecordLimitExceeded: return "RecordLimitExceeded";
        case MeshDataErrorCode::IndexOutOfRange: return "IndexOutOfRange";
        case MeshDataErrorCode::UnsupportedSection: return "UnsupportedSection";
        case MeshDataErrorCode::ExpectedOffset: return "ExpectedOffset";
        case MeshDataErrorCode::DataLoadFailed: return "DataLoadFailed";
        case MeshDataErrorCode::TypeMismatch: return "TypeMismatch";
        }
        return "Unknown";
    }

    std::expected<LegacyMeshData, MeshDataError> LegacyMeshData::parse(
        SharedDataBytes owner, MeshParseLimits limits)
    {
        if (!owner)
            return std::unexpected(error(MeshDataErrorCode::MissingPayload, 0,
                "HMD requires an owning DAT payload"));
        const std::span<const std::byte> data(*owner);
        if (!fits(data, 0, 16))
            return std::unexpected(error(MeshDataErrorCode::HeaderTruncated, 0,
                "HMD header requires four little-endian DWORDs"));
        if (u32(data, 4) != 0)
            return std::unexpected(error(MeshDataErrorCode::AlreadyMapped, 4,
                "Relocated process pointers are not an HMD disk payload"));

        LegacyMeshData result;
        result.owner_ = std::move(owner);
        // Neither historical loader checks a magic/version constant; retain it.
        result.versionId_ = u32(data, 0);
        const auto blockCount = u32(data, 12);
        if (!fits(data, 16, blockCount, 4))
            return std::unexpected(error(MeshDataErrorCode::RangeOutOfBounds,
                12, "HMD block table is truncated"));
        if (blockCount > limits.maximumPrimitives)
            return std::unexpected(error(MeshDataErrorCode::RecordLimitExceeded,
                12, "HMD block table exceeds configured record budget"));

        const auto headerSection = wordOffset(data, u32(data, 8), 8, 4);
        if (!headerSection) return std::unexpected(headerSection.error());
        const auto headerCount = u32(data, *headerSection);
        std::size_t cursor = *headerSection + 4;
        if (!fits(data, cursor, headerCount, 4))
            return std::unexpected(error(MeshDataErrorCode::RangeOutOfBounds,
                *headerSection, "HMD primitive header table is truncated"));
        if (headerCount > limits.maximumHeaderFields)
            return std::unexpected(error(MeshDataErrorCode::RecordLimitExceeded,
                *headerSection, "HMD header count exceeds configured budget"));

        // NewMesh.cpp:129-153: count, then [fieldCount, field...] headers.
        std::unordered_map<std::size_t, std::size_t> headerIndices;
        std::size_t totalFields = 0;
        for (std::uint32_t i = 0; i < headerCount; ++i)
        {
            if (!fits(data, cursor, 4))
                return std::unexpected(error(MeshDataErrorCode::RangeOutOfBounds,
                    cursor, "Missing HMD primitive header field count"));
            HmdPrimitiveHeader header;
            header.offset = cursor;
            const auto count = u32(data, cursor);
            cursor += 4;
            if (!fits(data, cursor, count, 4))
                return std::unexpected(error(MeshDataErrorCode::RangeOutOfBounds,
                    header.offset, "HMD primitive header fields are truncated"));
            if (count > limits.maximumHeaderFields - totalFields)
                return std::unexpected(error(MeshDataErrorCode::RecordLimitExceeded,
                    header.offset, "HMD header fields exceed configured budget"));
            totalFields += count;
            for (std::uint32_t j = 0; j < count; ++j, cursor += 4)
            {
                const auto value = u32(data, cursor);
                if ((value & Flag) != 0)
                {
                    const auto resolved = wordOffset(data, value & ~Flag, cursor, 1);
                    if (!resolved) return std::unexpected(resolved.error());
                }
                header.fields.push_back({ value });
            }
            headerIndices.emplace(header.offset, result.headers_.size());
            result.headers_.push_back(std::move(header));
        }

        std::unordered_map<std::size_t, std::size_t> primitiveIndices;
        std::size_t totalSections = 0;
        for (std::uint32_t block = 0; block < blockCount; ++block)
        {
            const auto fieldOffset = 16 + static_cast<std::size_t>(block) * 4;
            const auto rootWord = u32(data, fieldOffset);
            if (rootWord == 0)
            {
                result.blockRoots_.push_back(std::nullopt);
                continue;
            }
            const auto root = wordOffset(data, rootWord, fieldOffset, 12);
            if (!root) return std::unexpected(root.error());
            result.blockRoots_.push_back(*root);
            std::optional<std::size_t> next = *root;
            std::unordered_set<std::size_t> activeChain;
            while (next)
            {
                const auto position = *next;
                if (!activeChain.insert(position).second)
                    return std::unexpected(error(MeshDataErrorCode::PrimitiveCycle,
                        position, "Cycle in HMD primitive next chain"));
                // A completed chain shared by another block needs no reparse.
                if (primitiveIndices.contains(position)) break;
                if (result.primitives_.size() >= limits.maximumPrimitives)
                    return std::unexpected(error(MeshDataErrorCode::RecordLimitExceeded,
                        position, "HMD primitives exceed configured budget"));

                HmdPrimitive primitive;
                primitive.offset = position;
                const auto nextWord = u32(data, position);
                if (nextWord != EndOfChain)
                {
                    const auto resolved = wordOffset(data, nextWord, position, 12);
                    if (!resolved) return std::unexpected(resolved.error());
                    primitive.nextOffset = *resolved;
                }
                const auto headerOffset = wordOffset(data,
                    u32(data, position + 4), position + 4, 4);
                if (!headerOffset) return std::unexpected(headerOffset.error());
                const auto found = headerIndices.find(*headerOffset);
                if (found == headerIndices.end())
                    return std::unexpected(error(MeshDataErrorCode::InvalidPrimitiveHeader,
                        position + 4, "Primitive does not reference a declared header"));
                primitive.headerIndex = found->second;
                const auto countWord = u32(data, position + 8);
                primitive.processRequired = (countWord & Flag) != 0;
                const auto sectionCount = countWord & ~Flag;
                cursor = position + 12;
                if (!fits(data, cursor, sectionCount, 8))
                    return std::unexpected(error(MeshDataErrorCode::RangeOutOfBounds,
                        position + 8, "HMD data section count exceeds available bytes"));
                if (sectionCount > limits.maximumSections - totalSections)
                    return std::unexpected(error(MeshDataErrorCode::RecordLimitExceeded,
                        position + 8, "HMD sections exceed configured budget"));
                totalSections += sectionCount;
                for (std::uint32_t i = 0; i < sectionCount; ++i)
                {
                    if (!fits(data, cursor, 8))
                        return std::unexpected(error(MeshDataErrorCode::RangeOutOfBounds,
                            cursor, "HMD data section header is truncated"));
                    const auto countSize = u32(data, cursor + 4);
                    const auto words = (countSize & 0xFFFFU) + 1U;
                    if (words < 2)
                        return std::unexpected(error(MeshDataErrorCode::InvalidSectionSize,
                            cursor + 4, "HMD section cannot be shorter than its header"));
                    if (!fits(data, cursor, words, 4))
                        return std::unexpected(error(MeshDataErrorCode::RangeOutOfBounds,
                            cursor + 4, "HMD data section extends beyond payload"));
                    primitive.sections.push_back({ cursor, u32(data, cursor), words,
                        static_cast<std::uint16_t>((countSize >> 16) & 0x7FFFU),
                        (countSize & Flag) != 0 });
                    cursor += static_cast<std::size_t>(words) * 4;
                }
                next = primitive.nextOffset;
                primitiveIndices.emplace(position, result.primitives_.size());
                result.primitives_.push_back(std::move(primitive));
            }
        }
        return result;
    }

    std::uint32_t LegacyMeshData::versionId() const noexcept { return versionId_; }
    std::span<const std::optional<std::size_t>> LegacyMeshData::blockRoots() const noexcept
    { return blockRoots_; }
    std::span<const HmdPrimitiveHeader> LegacyMeshData::headers() const noexcept
    { return headers_; }
    std::span<const HmdPrimitive> LegacyMeshData::primitives() const noexcept
    { return primitives_; }
    std::span<const std::byte> LegacyMeshData::bytes() const noexcept
    { return owner_ ? std::span<const std::byte>(*owner_) : std::span<const std::byte>{}; }

    std::expected<std::span<const std::byte>, MeshDataError>
    LegacyMeshData::sectionBytes(std::size_t primitiveIndex,
        std::size_t sectionIndex) const
    {
        if (primitiveIndex >= primitives_.size() ||
            sectionIndex >= primitives_[primitiveIndex].sections.size())
            return std::unexpected(error(MeshDataErrorCode::IndexOutOfRange, 0,
                "HMD primitive/section index is out of range"));
        const auto& section = primitives_[primitiveIndex].sections[sectionIndex];
        return bytes().subspan(section.offset,
            static_cast<std::size_t>(section.sizeWords) * 4);
    }

    std::expected<HmdTriangle, MeshDataError> LegacyMeshData::triangle(
        std::size_t primitiveIndex, std::size_t sectionIndex,
        std::size_t triangleIndex) const
    {
        const auto rawSection = sectionBytes(primitiveIndex, sectionIndex);
        if (!rawSection) return std::unexpected(rawSection.error());
        const auto& primitive = primitives_[primitiveIndex];
        const auto& section = primitive.sections[sectionIndex];
        const auto type = section.type;
        // Category 0, shape 1, normal-bearing triangles. Other categories,
        // no-normal variants and preset packets remain bounded opaque sections.
        if (((type >> 24) & 0xFU) != 0 || ((type >> 3) & 7U) != 1 ||
            (type & (0x40U | 0x100U)) != 0)
            return std::unexpected(error(MeshDataErrorCode::UnsupportedSection,
                section.offset, "Section is not a supported normal-bearing triangle"));
        if (triangleIndex >= section.elementCount)
            return std::unexpected(error(MeshDataErrorCode::IndexOutOfRange,
                section.offset, "Triangle index exceeds section polygon count"));
        if (rawSection->size() < 12)
            return std::unexpected(error(MeshDataErrorCode::InvalidSectionSize,
                section.offset, "Triangle section requires a polygon word offset"));
        const auto& header = headers_[primitive.headerIndex];
        if (header.fields.size() < 3)
            return std::unexpected(error(MeshDataErrorCode::InvalidPrimitiveHeader,
                header.offset, "Triangle header needs polygon, vertex and normal fields"));
        std::array<std::size_t, 3> bases{};
        for (std::size_t i = 0; i < bases.size(); ++i)
        {
            if (!header.fields[i].isOffset())
                return std::unexpected(error(MeshDataErrorCode::ExpectedOffset,
                    header.offset + 4 + i * 4, "Triangle field is a literal, not an offset"));
            bases[i] = static_cast<std::size_t>(header.fields[i].rawValue & ~Flag) * 4;
        }
        const bool textured = (type & 1U) != 0;
        const bool separateColours = (type & 2U) != 0;
        const bool gouraud = (type & 4U) != 0;
        const bool tiled = (type & 0x200U) != 0;
        const std::size_t colours = textured && !separateColours ? 0 :
            (separateColours ? 3 : 1);
        const std::size_t recordWords = (tiled ? 1U : 0U) + colours +
            (textured ? 3U : 0U) + (gouraud ? 3U : 2U);
        const auto polygonWord = u32(*rawSection, 8);
        const auto data = bytes();
        // Offset is relative to the polygon field, unlike all header pointers.
        if (polygonWord > (data.size() - bases[0]) / 4)
            return std::unexpected(error(MeshDataErrorCode::RangeOutOfBounds,
                section.offset + 8, "Polygon offset leaves owning HMD payload"));
        const auto polygonStart = bases[0] + static_cast<std::size_t>(polygonWord) * 4;
        if (!fits(data, polygonStart, section.elementCount, recordWords * 4))
            return std::unexpected(error(MeshDataErrorCode::RangeOutOfBounds,
                polygonStart, "Triangle count extends beyond owning HMD payload"));
        auto cursor = polygonStart + triangleIndex * recordWords * 4;
        HmdTriangle result;
        if (tiled) cursor += 4;
        if (colours == 0) result.colours.fill(0x00FF'FFFFU);
        else
        {
            for (std::size_t i = 0; i < colours; ++i, cursor += 4)
                result.colours[i] = u32(data, cursor);
            if (colours == 1) result.colours.fill(result.colours[0]);
        }
        if (textured)
        {
            for (std::size_t i = 0; i < 3; ++i, cursor += 4)
            {
                const auto value = u32(data, cursor);
                result.texturePoints[i] = { static_cast<std::uint8_t>(value),
                    static_cast<std::uint8_t>(value >> 8) };
                if (i == 1) result.texturePage = static_cast<std::uint16_t>(value >> 16);
            }
        }
        if (gouraud)
        {
            for (std::size_t i = 0; i < 3; ++i, cursor += 4)
            {
                const auto value = u32(data, cursor);
                result.vertexIndices[i] = static_cast<std::uint16_t>(value >> 16);
                result.normalIndices[i] = static_cast<std::uint16_t>(value);
            }
        }
        else
        {
            const auto first = u32(data, cursor);
            const auto second = u32(data, cursor + 4);
            result.vertexIndices = { static_cast<std::uint16_t>(first >> 16),
                static_cast<std::uint16_t>(second), static_cast<std::uint16_t>(second >> 16) };
            result.normalIndices.fill(static_cast<std::uint16_t>(first));
        }
        for (std::size_t i = 0; i < 3; ++i)
        {
            const auto vertexIndex = static_cast<std::size_t>(result.vertexIndices[i]);
            const auto normalIndex = static_cast<std::size_t>(result.normalIndices[i]);
            if (!fits(data, bases[1], vertexIndex + 1, 8) ||
                !fits(data, bases[2], normalIndex + 1, 8))
                return std::unexpected(error(MeshDataErrorCode::RangeOutOfBounds,
                    polygonStart, "Triangle references an out-of-payload SVECTOR"));
            result.vertices[i] = shortVector(data, bases[1] + vertexIndex * 8);
            result.normals[i] = shortVector(data, bases[2] + normalIndex * 8);
        }
        return result;
    }

    std::expected<LegacyMeshData, MeshDataError> openLegacyMeshData(
        const DataBankRegistry& registry, DataId id, MeshParseLimits limits)
    {
        const auto metadata = registry.metadata(id);
        if (!metadata)
            return std::unexpected(MeshDataError{ MeshDataErrorCode::DataLoadFailed,
                0, "Cannot inspect HMD DAT item", metadata.error() });
        if (metadata->type != LegacyDataType::Hmd)
            return std::unexpected(error(MeshDataErrorCode::TypeMismatch,
                0, "DAT item is not an unmapped HMD payload"));
        const auto payload = registry.load(id);
        if (!payload)
            return std::unexpected(MeshDataError{ MeshDataErrorCode::DataLoadFailed,
                0, "Cannot load HMD DAT item", payload.error() });
        return LegacyMeshData::parse(*payload, limits);
    }
}
