#include "LegacyMeshData.hpp"
#include "LegacyDataArchiveBuilder.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>

namespace
{
    using namespace monopoly::data;
    int failures = 0;

    void expect(bool condition, std::string_view description)
    {
        if (condition) std::cout << "[PASS] " << description << '\n';
        else
        {
            ++failures;
            std::cerr << "[FAIL] " << description << '\n';
        }
    }

    void setWord(DataBytes& data, std::size_t index, std::uint32_t value)
    {
        for (unsigned i = 0; i < 4; ++i)
            data[index * 4 + i] = static_cast<std::byte>((value >> (i * 8U)) & 0xFFU);
    }

    DataBytes words(std::initializer_list<std::uint32_t> values)
    {
        DataBytes result(values.size() * 4);
        std::size_t index = 0;
        for (auto value : values) setWord(result, index++, value);
        return result;
    }

    // Entirely synthetic FORMAT fixture, not a replacement for retail content.
    // HMDData.h + NewMesh.cpp:110-204,272-361,415-433:
    // 2 block roots; one three-field header; one one-triangle section;
    // polygon record, then three SVECTOR vertices and one SVECTOR normal.
    DataBytes flatTriangle()
    {
        return words({
            0x01020304, 0, 6, 2, 11, 0,
            1, 3, 0x80000011, 0x80000014, 0x8000001A,
            0xFFFFFFFF, 7, 0x80000001,
            0x00000008, 0x80010002, 0,
            0x00563412, 0, 0x00020001,
            0xFFEC000A, 30, 0x00280014, 60, 0x003C001E, 90,
            0xF8001000, 1024
        });
    }

    auto parse(DataBytes data, MeshParseLimits limits = {})
    {
        return LegacyMeshData::parse(std::make_shared<const DataBytes>(
            std::move(data)), limits);
    }

    template<class T>
    bool hasError(const std::expected<T, MeshDataError>& result,
        MeshDataErrorCode code)
    {
        return !result && result.error().code == code;
    }

    void testFlatTriangleAndOwnership()
    {
        auto owner = std::make_shared<const DataBytes>(flatTriangle());
        const auto original = *owner;
        auto result = LegacyMeshData::parse(owner);
        expect(result.has_value(), "source-defined HMD hierarchy parses");
        expect(*owner == original, "parse does not relocate or mutate disk bytes");
        owner.reset();
        if (!result) return;
        auto moved = std::move(*result);
        expect(moved.versionId() == 0x01020304 && moved.bytes().size() == 112,
            "version stays raw and moved view retains owning bytes");
        expect(moved.blockRoots().size() == 2 && moved.blockRoots()[0] == 44 &&
            !moved.blockRoots()[1], "block word offsets and null root resolve");
        expect(moved.headers().size() == 1 && moved.headers()[0].offset == 28 &&
            moved.headers()[0].fields.size() == 3,
            "primitive header count and field count remain distinct");
        const auto& primitive = moved.primitives()[0];
        expect(primitive.offset == 44 && !primitive.nextOffset &&
            primitive.processRequired && primitive.headerIndex == 0,
            "primitive sentinel, processing flag and header association decode");
        expect(primitive.sections[0].sizeWords == 3 &&
            primitive.sections[0].elementCount == 1 &&
            primitive.sections[0].scanRequired,
            "size excludes first word while count excludes scan flag");
        auto raw = moved.sectionBytes(0, 0);
        expect(raw && raw->size() == 12 && raw->data() == moved.bytes().data() + 56,
            "section span references exactly its validated disk range");
        const auto triangle = moved.triangle(0, 0, 0);
        expect(triangle.has_value(), "flat triangle resolves polygon and vectors");
        if (triangle)
        {
            expect(triangle->vertexIndices == std::array<std::uint16_t, 3>{0, 1, 2} &&
                triangle->normalIndices == std::array<std::uint16_t, 3>{0, 0, 0},
                "flat triangle uses one normal and three packed vertex indices");
            expect(triangle->colours == std::array<std::uint32_t, 3>{
                    0x00563412, 0x00563412, 0x00563412} && triangle->texturePage == 0xFFFF,
                "single colour propagates and absent texture uses sentinel");
            expect(triangle->vertices[0].x == 10 && triangle->vertices[0].y == -20 &&
                triangle->vertices[2].z == 90 && triangle->normals[1].x == 4096 &&
                triangle->normals[1].y == -2048,
                "signed SVECTOR fields remain raw without invented axis conversion");
        }
        expect(hasError(moved.sectionBytes(1, 0), MeshDataErrorCode::IndexOutOfRange) &&
            hasError(moved.sectionBytes(0, 1), MeshDataErrorCode::IndexOutOfRange) &&
            hasError(moved.triangle(0, 0, 1), MeshDataErrorCode::IndexOutOfRange),
            "public indices cannot bypass validated bounds");
    }

    void testTexturedGouraudAndTiledColours()
    {
        auto data = flatTriangle();
        data.resize(17 * 4);
        const auto tail = words({
            0x00002211, 0x12344433, 0x00006655, 0, 0x00010001, 0x00020002,
            0xFFEC000A, 30, 0x00280014, 60, 0x003C001E, 90,
            4096, 0, 0x10000000, 0, 0, 4096
        });
        data.insert(data.end(), tail.begin(), tail.end());
        setWord(data, 9, 0x80000017); // vertex word 23
        setWord(data, 10, 0x8000001D); // normal word 29
        setWord(data, 14, 0x0080000D); // initialize + textured gouraud triangle
        const auto result = parse(data);
        const auto triangle = result ? result->triangle(0, 0, 0) :
            std::expected<HmdTriangle, MeshDataError>(std::unexpected(result.error()));
        expect(triangle && triangle->texturePage == 0x1234 &&
            triangle->texturePoints[0].u == 0x11 && triangle->texturePoints[2].v == 0x66 &&
            triangle->colours[2] == 0x00FFFFFF && triangle->normalIndices[2] == 2 &&
            triangle->normals[1].y == 4096,
            "textured gouraud decodes three UVs, texture page, white and distinct normals");

        data = flatTriangle();
        const auto extra = words({0xDEADBEEF, 0x00112233, 0x00445566});
        data.insert(data.begin() + 17 * 4, extra.begin(), extra.end());
        setWord(data, 9, 0x80000017);
        setWord(data, 10, 0x8000001D);
        setWord(data, 14, 0x0000020A); // tiled + separate colours + flat triangle
        const auto tiled = parse(std::move(data));
        const auto tiledTriangle = tiled ? tiled->triangle(0, 0, 0) :
            std::expected<HmdTriangle, MeshDataError>(std::unexpected(tiled.error()));
        expect(tiledTriangle && tiledTriangle->colours[0] == 0x00112233 &&
            tiledTriangle->colours[1] == 0x00445566 &&
            tiledTriangle->colours[2] == 0x00563412 &&
            tiledTriangle->vertices[0].y == -20,
            "tiled descriptor is skipped before three separate colour words");
    }

    void testChainsAndOpaqueSections()
    {
        auto data = flatTriangle();
        const auto tail = words({0xFFFFFFFF, 7, 0});
        data.insert(data.end(), tail.begin(), tail.end());
        setWord(data, 11, 28);
        setWord(data, 5, 28);
        const auto shared = parse(data);
        expect(shared && shared->primitives().size() == 2 &&
            shared->primitives()[0].nextOffset == 112 &&
            shared->blockRoots()[1] == 112,
            "block roots may share a completed primitive-chain suffix");
        setWord(data, 28, 11);
        expect(hasError(parse(data), MeshDataErrorCode::PrimitiveCycle),
            "multi-node cycle terminates with a precise error");
        data = flatTriangle();
        setWord(data, 11, 11);
        expect(hasError(parse(data), MeshDataErrorCode::PrimitiveCycle),
            "self-referential primitive is rejected without recursion");

        data = flatTriangle();
        setWord(data, 13, 1); // already processed section flag, not an address
        setWord(data, 14, 0x03000000); // opaque animation category
        setWord(data, 15, 0x00010002);
        const auto opaque = parse(data);
        expect(opaque && !opaque->primitives()[0].processRequired &&
            !opaque->primitives()[0].sections[0].scanRequired &&
            opaque->sectionBytes(0, 0)->size() == 12 &&
            hasError(opaque->triangle(0, 0, 0), MeshDataErrorCode::UnsupportedSection),
            "unknown categories remain opaque and flags are preserved without fake decoding");
    }

    void testMalformedHierarchy()
    {
        expect(hasError(LegacyMeshData::parse(nullptr), MeshDataErrorCode::MissingPayload),
            "missing ownership is rejected");
        for (std::size_t size = 0; size < 16; ++size)
            expect(hasError(parse(DataBytes(size)), MeshDataErrorCode::HeaderTruncated),
                "truncated fixed header is rejected");
        auto data = flatTriangle();
        setWord(data, 1, 1);
        expect(hasError(parse(data), MeshDataErrorCode::AlreadyMapped),
            "already-relocated 32-bit process data is rejected");
        for (const auto index : {2U, 3U, 4U, 6U, 7U, 8U, 11U, 12U, 13U})
        {
            data = flatTriangle();
            setWord(data, index, 0xFFFFFFFE);
            expect(!parse(data), "huge count or offset cannot overflow into a valid range");
        }
        data = flatTriangle();
        setWord(data, 12, 8);
        expect(hasError(parse(data), MeshDataErrorCode::InvalidPrimitiveHeader),
            "pointer into middle of a header is not a declared header");
        data = flatTriangle();
        setWord(data, 15, 0x80010000);
        expect(hasError(parse(data), MeshDataErrorCode::InvalidSectionSize),
            "section smaller than two-word header is rejected");
        setWord(data, 15, 0x8001FFFF);
        expect(hasError(parse(data), MeshDataErrorCode::RangeOutOfBounds),
            "section length is checked before creating its span");
        data = flatTriangle();
        data.resize(17 * 4 - 1);
        // Point fields at valid words so the intended failure is the section.
        setWord(data, 8, 0x80000000);
        setWord(data, 9, 0x80000000);
        setWord(data, 10, 0x80000000);
        expect(hasError(parse(data), MeshDataErrorCode::RangeOutOfBounds),
            "a section truncated by one byte is rejected");
        MeshParseLimits limits;
        limits.maximumPrimitives = 1;
        expect(hasError(parse(flatTriangle(), limits), MeshDataErrorCode::RecordLimitExceeded),
            "block budget bounds work before allocation");
        limits = {};
        limits.maximumSections = 0;
        expect(hasError(parse(flatTriangle(), limits), MeshDataErrorCode::RecordLimitExceeded),
            "section budget bounds overlapping-record amplification");
        limits = {};
        limits.maximumHeaderFields = 2;
        expect(hasError(parse(flatTriangle(), limits), MeshDataErrorCode::RecordLimitExceeded),
            "aggregate header field budget is enforced");
    }

    void testMalformedTriangleReferences()
    {
        for (const auto index : {16U, 19U})
        {
            auto data = flatTriangle();
            setWord(data, index, 0xFFFFFFFF);
            const auto result = parse(data);
            expect(result && hasError(result->triangle(0, 0, 0),
                MeshDataErrorCode::RangeOutOfBounds),
                "triangle relative offsets and SVECTOR indices stay in payload");
        }
        auto data = flatTriangle();
        setWord(data, 8, 17); // no relocation flag: scalar header field
        const auto literal = parse(data);
        expect(literal && !literal->headers()[0].fields[0].isOffset() &&
            hasError(literal->triangle(0, 0, 0), MeshDataErrorCode::ExpectedOffset),
            "literal header words are not silently reinterpreted as pointers");
        data = flatTriangle();
        setWord(data, 15, 0x800A0002); // ten triangles cannot fit
        const auto excessive = parse(data);
        expect(excessive && hasError(excessive->triangle(0, 0, 0),
            MeshDataErrorCode::RangeOutOfBounds),
            "whole polygon run is bounded before returning its first triangle");
        data = flatTriangle();
        setWord(data, 14, 0x00000048); // no-normal variant unported
        const auto noNormals = parse(data);
        expect(noNormals && hasError(noNormals->triangle(0, 0, 0),
            MeshDataErrorCode::UnsupportedSection),
            "unsupported no-normal triangle does not read a fabricated normal");
    }

    void testRegistryTypeAndLease()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto directory = std::filesystem::current_path() /
            ("HmdFormatFixture-" + std::to_string(stamp));
        std::error_code filesystemError;
        const bool created = std::filesystem::create_directory(directory, filesystemError);
        expect(created && !filesystemError, "unique synthetic DAT directory is created");
        if (!created || filesystemError) return;
        struct Cleanup
        {
            std::filesystem::path directory;
            ~Cleanup()
            {
                std::error_code ignored;
                std::filesystem::remove(directory / "format.dat", ignored);
                std::filesystem::remove(directory, ignored);
            }
        } cleanup{ directory };

        const std::array items{
            ArchiveBuildItem{LegacyDataType::Hmd, flatTriangle()},
            ArchiveBuildItem{LegacyDataType::MeshX, flatTriangle()},
            ArchiveBuildItem{LegacyDataType::Hmd, DataBytes(3)}
        };
        const auto written = writeLegacyDataArchive(directory / "format.dat", items);
        expect(written.has_value(), "proven DAT writer stores synthetic HMD payload");
        if (!written) return;
        DataBankRegistry registry;
        auto archive = registry.mount(directory / "format.dat", 8);
        expect(archive.has_value(), "HMD format test DAT mounts in group 8");
        if (!archive) return;
        const auto mesh = openLegacyMeshData(registry, packDataId(8, 0));
        expect(mesh.has_value(), "registry adapter loads and validates HMD bytes");
        expect(hasError(openLegacyMeshData(registry, packDataId(8, 1)),
            MeshDataErrorCode::TypeMismatch),
            "runtime MESHX type is rejected even with HMD-shaped bytes");
        expect(hasError(openLegacyMeshData(registry, packDataId(8, 2)),
            MeshDataErrorCode::HeaderTruncated),
            "registry adapter retains precise HMD parsing errors");
        const auto missing = openLegacyMeshData(registry, packDataId(9, 0));
        expect(hasError(missing, MeshDataErrorCode::DataLoadFailed) &&
            missing.error().dataError &&
            missing.error().dataError->code == DataErrorCode::GroupNotMounted,
            "registry adapter retains underlying DATA error");
        registry.clear();
        (*archive)->clearCache();
        (*archive)->close();
        archive->reset();
        const auto triangle = mesh ? mesh->triangle(0, 0, 0) :
            std::expected<HmdTriangle, MeshDataError>(std::unexpected(mesh.error()));
        expect(triangle && triangle->vertices[0].x == 10,
            "HMD lease survives registry clear, cache eviction and archive close");
    }
}

int main()
{
    std::cout << "Monopoly HMD CPU format tests (synthetic fixtures)\n";
    testFlatTriangleAndOwnership();
    testTexturedGouraudAndTiledColours();
    testChainsAndOpaqueSections();
    testMalformedHierarchy();
    testMalformedTriangleReferences();
    testRegistryTypeAndLease();
    if (failures != 0)
    {
        std::cerr << failures << " HMD test(s) failed.\n";
        return 1;
    }
    std::cout << "All HMD format tests passed.\n";
    return 0;
}
