#include "MeshRuntime.hpp"
#include "BoardRules.hpp"
#include "BoardTextureRuntime.hpp"
#include "FontRuntime.hpp"
#include "MoneyFormat.hpp"
#include "ModernGltfMesh.hpp"
#include "ModernEnvironment.hpp"
#include "ModernTokenCatalog.hpp"
#include "ResourcePaths.hpp"
#include "ResourceRuntime.hpp"
#include "SequenceRuntime.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <optional>
#include <set>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
    using namespace monopoly::data;

    struct Dimensions
    {
        std::array<float, 3> size{};
        std::array<float, 3> center{};
    };

    [[nodiscard]] Dimensions dimensions(const MeshBounds& bounds) noexcept
    {
        Dimensions result;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            result.size[axis] = bounds.maximum[axis] - bounds.minimum[axis];
            result.center[axis] =
                (bounds.maximum[axis] + bounds.minimum[axis]) * 0.5F;
        }
        return result;
    }

    void printVector(const std::array<float, 3>& value)
    {
        std::cout << std::fixed << std::setprecision(3)
                  << value[0] << ',' << value[1] << ',' << value[2];
    }

    bool parseFiniteFloat(std::string_view argument, float& value) noexcept
    {
        const auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == argument.data() + argument.size() &&
            std::isfinite(value);
    }

    std::string_view meshErrorName(MeshRuntimeErrorCode code) noexcept
    {
        switch (code)
        {
        case MeshRuntimeErrorCode::MissingSource: return "MissingSource";
        case MeshRuntimeErrorCode::MissingResources: return "MissingResources";
        case MeshRuntimeErrorCode::SourceLoadFailed: return "SourceLoadFailed";
        case MeshRuntimeErrorCode::ModernAssetInvalid: return "ModernAssetInvalid";
        case MeshRuntimeErrorCode::ModernAssetUnsupported: return "ModernAssetUnsupported";
        case MeshRuntimeErrorCode::TriangleDecodeFailed: return "TriangleDecodeFailed";
        case MeshRuntimeErrorCode::TextureDecodeFailed: return "TextureDecodeFailed";
        case MeshRuntimeErrorCode::MimeDecodeFailed: return "MimeDecodeFailed";
        case MeshRuntimeErrorCode::InvalidPose: return "InvalidPose";
        case MeshRuntimeErrorCode::InvalidTextureRegion: return "InvalidTextureRegion";
        case MeshRuntimeErrorCode::VertexLimitExceeded: return "VertexLimitExceeded";
        case MeshRuntimeErrorCode::GroupLimitExceeded: return "GroupLimitExceeded";
        case MeshRuntimeErrorCode::IndexLimitExceeded: return "IndexLimitExceeded";
        case MeshRuntimeErrorCode::NoRenderableGeometry: return "NoRenderableGeometry";
        }
        return "Unknown";
    }

    std::uint64_t environmentSignature(const monopoly::sequence::SequenceMeshRenderItem& item)
    {
        std::uint64_t hash=14695981039346656037ULL;
        auto word=[&](std::uint64_t value){for(unsigned shift=0;shift<64;shift+=8){hash^=(value>>shift)&255;hash*=1099511628211ULL;}};
        auto floats=[&](const auto& values){for(const float value:values)word(std::bit_cast<std::uint32_t>(value));};
        word(item.node);word(item.contentsDataId);floats(item.worldTransform.values);
        const auto& mesh=*item.renderData;
        floats(mesh.bounds.minimum);floats(mesh.bounds.maximum);
        word(mesh.vertices.size());word(mesh.indices.size());word(mesh.batches.size());
        for(const auto& vertex:mesh.vertices){floats(vertex.position);floats(vertex.normal);floats(vertex.uv);floats(vertex.tangent);}
        for(const auto index:mesh.indices)word(index);
        for(const auto& batch:mesh.batches)
        {
            word(batch.firstIndex);word(batch.indexCount);const auto& material=batch.material;
            word(unsigned(material.model));word(material.rawDiffuse);floats(material.diffuse);floats(material.emissive);
            floats(std::array{material.metallic,material.roughness,material.emissiveStrength,material.alphaCutoff});
            word(material.doubleSided);word(unsigned(material.alphaMode));
            for(const auto* map:std::array{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.emissiveTexture,&material.occlusionTexture})
            {
                word(map->has_value());if(!*map)continue;const auto& binding=**map;
                word(binding.texCoord);word(unsigned(binding.colorSpace));floats(std::array{binding.scale});
                word(unsigned(binding.sampler.wrapS));word(unsigned(binding.sampler.wrapT));
                word(unsigned(binding.sampler.minFilter));word(unsigned(binding.sampler.magFilter));
                word(bool(binding.image));if(!binding.image)continue;
                word(binding.image->width);word(binding.image->height);
                for(const auto byte:binding.image->rgba){hash^=byte;hash*=1099511628211ULL;}
            }
        }
        return hash;
    }
    int benchmarkEnvironment(int argc,char** argv)
    {
        if(argc!=5){std::cerr<<"usage: MonopolyModernAssetProbe --environment-benchmark <assets-root> <workers:1|4|8> <runs:1..3>\n";return 2;}
        unsigned workers{},runs{};
        auto parse=[](const char* text,unsigned& result){const std::string_view value{text};const auto p=std::from_chars(value.data(),value.data()+value.size(),result);return p.ec==std::errc{}&&p.ptr==value.data()+value.size();};
        if(!parse(argv[3],workers)||(workers!=1&&workers!=4&&workers!=8)||!parse(argv[4],runs)||runs<1||runs>3)return 2;
        std::vector<std::uint64_t> baseline;
        for(unsigned run=0;run<runs;++run)
        {
            monopoly::engine::ModernEnvironment environment(argv[2],true);
            environment.setDecodeWorkers(workers);
            const auto started=std::chrono::steady_clock::now();
            const auto items=environment.items(monopoly::sequence::identity3D(),0,true,false,true);
            const double milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            if(items.size()!=monopoly::engine::ModernCityCount+2){std::cerr<<"incomplete environment benchmark\n";return 1;}
            std::vector<std::uint64_t> signatures;
            std::size_t vertices=0,triangles=0;
            for(const auto& item:items)
            {
                const auto signature=environmentSignature(item);signatures.push_back(signature);
                vertices+=item.renderData->vertices.size();triangles+=item.renderData->indices.size()/3;
                std::cout<<"asset_signature\trun\t"<<run<<"\tnode\t"<<item.node<<"\tid\t"<<item.contentsDataId
                    <<"\tfnv1a64\t"<<std::hex<<signature<<std::dec<<'\n';
            }
            if(run==0)baseline=signatures;else if(signatures!=baseline){std::cerr<<"environment signatures changed between runs\n";return 1;}
            std::cout<<"environment_decode_cpu\tworkers\t"<<workers<<"\trun\t"<<run<<"\telapsed_ms\t"<<milliseconds
                <<"\titems\t"<<items.size()<<"\tvertices\t"<<vertices<<"\ttriangles\t"<<triangles
                <<"\tscope\tdecode and ordered publish only; signature hashing excluded; no GPU\n";
        }
        return 0;
    }

    int probeGltf(int argc, char** argv)
    {
        const bool keepGround = argc > 2 && std::string_view(argv[argc - 1]) == "--keep-ground";
        const int positionalCount = argc - (keepGround ? 1 : 0);
        if (positionalCount != 4 && positionalCount != 5 && positionalCount != 8)
        {
            std::cerr << "usage: MonopolyModernAssetProbe --gltf <path> <unitsPerMeter> "
                "[yawDegrees [offsetX offsetY offsetZ]] [--keep-ground]\n";
            return 2;
        }
        ModernGltfLoadOptions options;
        options.groundToZero = !keepGround;
        if (!parseFiniteFloat(argv[3], options.unitsPerMeter) || options.unitsPerMeter <= 0.0F)
        {
            std::cerr << "unitsPerMeter must be a finite positive number\n";
            return 2;
        }
        if (positionalCount >= 5 && !parseFiniteFloat(argv[4], options.yawDegrees))
        {
            std::cerr << "yawDegrees must be a finite number\n";
            return 2;
        }
        if (positionalCount == 8)
            for (std::size_t axis = 0; axis < options.localOffset.size(); ++axis)
                if (!parseFiniteFloat(argv[5 + axis], options.localOffset[axis]))
                {
                    std::cerr << "offsetX, offsetY and offsetZ must be finite numbers\n";
                    return 2;
                }
        auto loaded = loadModernGltfMesh(std::filesystem::path(argv[2]), options);
        if (!loaded)
        {
            std::cerr << "glTF error_code=" << meshErrorName(loaded.error().code)
                << '(' << static_cast<unsigned>(loaded.error().code) << "): "
                << loaded.error().detail << '\n';
            return 1;
        }
        const auto& mesh = **loaded;
        const auto measured = dimensions(mesh.bounds);
        std::array<std::size_t, 5> mapCounts{};
        std::set<const ModernTextureImage*> images;
        for (const auto& batch : mesh.batches)
        {
            const std::array<const std::optional<ModernTextureBinding>*, 5> maps{
                &batch.material.baseColorTexture, &batch.material.metallicRoughnessTexture,
                &batch.material.normalTexture, &batch.material.emissiveTexture,
                &batch.material.occlusionTexture};
            for (std::size_t index = 0; index < maps.size(); ++index)
                if (*maps[index])
                {
                    ++mapCounts[index];
                    if ((**maps[index]).image) images.insert((**maps[index]).image.get());
                }
        }
        std::cout.imbue(std::locale::classic());
        std::cout << std::defaultfloat << std::setprecision(std::numeric_limits<float>::max_digits10);
        const auto vector = [](const std::array<float, 3>& value)
        { std::cout << value[0] << ',' << value[1] << ',' << value[2]; };
        std::cout << "gltf\t" << argv[2] << "\nunits_per_meter\t" << options.unitsPerMeter
            << "\nyaw_degrees\t" << options.yawDegrees << "\nlocal_offset\t";
        vector(options.localOffset);
        std::cout << "\nground_to_zero\t" << (options.groundToZero ? 1 : 0)
            << "\nminimum_xyz\t";
        vector(mesh.bounds.minimum);
        std::cout << "\nmaximum_xyz\t"; vector(mesh.bounds.maximum);
        std::cout << "\nsize_xyz\t"; vector(measured.size);
        std::cout << "\ncenter_xyz\t"; vector(measured.center);
        std::cout << "\nvertices\t" << mesh.vertices.size()
            << "\ntriangles\t" << mesh.indices.size() / 3U
            << "\nbatches\t" << mesh.batches.size()
            << "\nbase_color_map_bindings\t" << mapCounts[0]
            << "\nmetallic_roughness_map_bindings\t" << mapCounts[1]
            << "\nnormal_map_bindings\t" << mapCounts[2]
            << "\nemissive_map_bindings\t" << mapCounts[3]
            << "\nocclusion_map_bindings\t" << mapCounts[4]
            << "\nunique_images\t" << images.size() << '\n';
        return std::cout ? 0 : 1;
    }

    bool within(const std::filesystem::path& path, const std::filesystem::path& root)
    {
        const auto relative = path.lexically_relative(root);
        return !relative.empty() && !relative.is_absolute() &&
            *relative.begin() != std::filesystem::path("..");
    }

    bool exportObj(const std::filesystem::path& output, std::string_view name,
        DataId id, const MeshRenderData& mesh)
    {
        std::error_code error;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(output, error)))
        { std::cerr << "refusing symlink OBJ output: " << output << '\n'; return false; }
        if (mesh.indices.size() % 3 != 0)
        { std::cerr << "OBJ mesh has incomplete triangle indices: " << id << '\n'; return false; }
        for (const auto index : mesh.indices)
            if (index >= mesh.vertices.size())
            { std::cerr << "OBJ mesh index outside vertices: " << id << '\n'; return false; }
        std::ofstream file(output, std::ios::trunc);
        file.imbue(std::locale::classic());
        file << std::setprecision(std::numeric_limits<float>::max_digits10)
             << "# Diagnostic retail base mesh; production decoder coordinates and normals.\n"
             << "# DataId " << id << "; no textures, materials or sequence transforms.\n"
             << "o " << name << '\n';
        for (const auto& vertex : mesh.vertices)
            file << "v " << vertex.position[0] << ' ' << vertex.position[1] << ' '
                 << vertex.position[2] << '\n';
        for (const auto& vertex : mesh.vertices)
            file << "vn " << vertex.normal[0] << ' ' << vertex.normal[1] << ' '
                 << vertex.normal[2] << '\n';
        for (std::size_t index = 0; index < mesh.indices.size(); index += 3)
        {
            file << 'f';
            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                const auto objIndex = static_cast<std::uint64_t>(mesh.indices[index + corner]) + 1;
                file << ' ' << objIndex << "//" << objIndex;
            }
            file << '\n';
        }
        file.close();
        if (!file) { std::cerr << "cannot write diagnostic OBJ: " << output << '\n'; return false; }
        return true;
    }
    std::string jsonString(std::string_view text)
    {
        std::ostringstream result;
        result << '"';
        for (const unsigned char value : text)
        {
            if (value == '"' || value == '\\') result << '\\' << static_cast<char>(value);
            else if (value < 0x20U)
                result << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<unsigned>(value) << std::dec;
            else result << static_cast<char>(value);
        }
        result << '"';
        return result.str();
    }

    bool exportSquareLabels(const std::filesystem::path& directory,
        const ResourceSnapshot& resources)
    {
        const auto language = resources.language();
        if (resources.context().board != BoardEdition::Usa || !language ||
            language->language != LanguageId::EnglishUs || !language->catalog)
        { std::cerr << "USA print labels require classic USA English resources\n"; return false; }
        std::ostringstream json;
        json.imbue(std::locale::classic());
        json << "{\"label_contract_version\":1,\"edition\":\"USA\",\"language_id\":1,"
                "\"city\":0,\"immutable_rules\":true,\"name_source\":\"LanguageCatalog::lookup\","
                "\"price_source\":\"BoardRules::originalDefinition + money::format\",\"squares\":[";
        for (std::uint32_t index = 0; index < 40; ++index)
        {
            const auto messageId = 1001U + index;
            const auto text = language->catalog->lookup(messageId);
            if (!text || !*text || (***text).empty() || (***text).front() == u'*')
            { std::cerr << "missing canonical USA square name: " << messageId << '\n'; return false; }
            const auto name = monopoly::fonts::transcodeUtf8(std::u16string_view(***text));
            if (!name) { std::cerr << "invalid USA square UTF16\n"; return false; }
            const auto square = static_cast<monopoly::rules::board::SquareType>(index);
            const auto& original = monopoly::rules::board::originalDefinition(square);
            const bool ownable = monopoly::rules::board::isOwnable(square);
            const auto price = monopoly::money::format(original.purchaseCost,13,true,BoardEdition::Usa);
            if (!price) { std::cerr << price.error() << '\n'; return false; }
            json << (index ? "," : "") << "{\"index\":" << index << ",\"type\":" << index
                << ",\"name_message_id\":" << messageId << ",\"name_utf8\":" << jsonString(*name)
                << ",\"purchase_cost\":" << original.purchaseCost
                << ",\"price_text_usd\":" << jsonString(ownable ? *price : std::string{})
                << ",\"group\":" << static_cast<unsigned>(original.group)
                << ",\"ownable\":" << (ownable ? "true" : "false") << '}';
        }
        json << "]}\n";
        const auto output = directory / "boardmed_labels.json";
        std::error_code error;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(output,error)))
        { std::cerr << "refusing symlink USA labels output\n"; return false; }
        std::ofstream file(output,std::ios::trunc);
        file << json.str(); file.close();
        return static_cast<bool>(file);
    }

    bool exportTexturedBoard(const std::filesystem::path& directory,
        DataId id, const MeshRenderData& mesh, const BoardTextureRecipe* recipe = nullptr)
    {
        // Refuse existing links before any write, including per-batch image outputs.
        for (const auto& entry : std::filesystem::directory_iterator(directory))
            if (entry.is_symlink())
            { std::cerr << "refusing symlink textured-board output\n"; return false; }
        std::ofstream obj(directory / "boardmed.obj", std::ios::trunc);
        std::ofstream mtl(directory / "boardmed.mtl", std::ios::trunc);
        obj.imbue(std::locale::classic());
        mtl.imbue(std::locale::classic());
        obj << std::setprecision(std::numeric_limits<float>::max_digits10)
            << "# Production MeshRuntime ClassicMedium, default pose; raw engine Y-up units.\n"
            << "# DataId " << id << "; UVs converted top-left to OBJ bottom-left.\n"
            << "mtllib boardmed.mtl\no boardmed\n";
        mtl << std::setprecision(std::numeric_limits<float>::max_digits10);
        for (const auto& vertex : mesh.vertices)
            obj << "v " << vertex.position[0] << ' ' << vertex.position[1] << ' '
                << vertex.position[2] << '\n';
        for (const auto& vertex : mesh.vertices)
            obj << "vt " << vertex.uv[0] << ' ' << 1.0F - vertex.uv[1] << '\n';
        for (const auto& vertex : mesh.vertices)
            obj << "vn " << vertex.normal[0] << ' ' << vertex.normal[1] << ' '
                << vertex.normal[2] << '\n';
        std::size_t covered{};
        for (std::size_t batchIndex = 0; batchIndex < mesh.batches.size(); ++batchIndex)
        {
            const auto& batch = mesh.batches[batchIndex];
            if (batch.firstIndex != covered || batch.indexCount % 3 != 0 ||
                batch.firstIndex > mesh.indices.size() ||
                batch.indexCount > mesh.indices.size() - batch.firstIndex)
            { std::cerr << "invalid textured board batch coverage\n"; return false; }
            covered += batch.indexCount;
            const auto name = "board_batch_" + std::to_string(batchIndex);
            mtl << "newmtl " << name << "\nKd " << batch.material.diffuse[0] << ' '
                << batch.material.diffuse[1] << ' ' << batch.material.diffuse[2]
                << "\nd 1\nillum 2\nPr 0.72\nPm 0\n";
            if (batch.texture)
            {
                const auto& image = batch.texture->sourceImage;
                if (!image || !image->width || !image->height ||
                    image->width > 16384 || image->height > 16384 ||
                    image->rgba.size() != static_cast<std::size_t>(image->width) * image->height * 4)
                { std::cerr << "textured board has invalid/missing decoded source image\n"; return false; }
                const auto fileName = name + ".bmp";
                auto* surface = SDL_CreateSurfaceFrom(static_cast<int>(image->width),
                    static_cast<int>(image->height), SDL_PIXELFORMAT_RGBA32,
                    const_cast<std::uint8_t*>(image->rgba.data()), static_cast<int>(image->width * 4));
                if (!surface) { std::cerr << SDL_GetError() << '\n'; return false; }
                const bool saved = SDL_SaveBMP(surface, (directory / fileName).string().c_str());
                SDL_DestroySurface(surface);
                if (!saved) { std::cerr << SDL_GetError() << '\n'; return false; }
                mtl << "map_Kd " << fileName << '\n';
            }
            mtl << '\n';
            obj << "usemtl " << name << '\n';
            for (std::size_t index = batch.firstIndex; index < covered; index += 3)
            {
                obj << 'f';
                for (std::size_t corner = 0; corner < 3; ++corner)
                {
                    const auto vertex = mesh.indices[index + corner];
                    if (vertex >= mesh.vertices.size()) return false;
                    const auto number = static_cast<std::uint64_t>(vertex) + 1;
                    obj << ' ' << number << '/' << number << '/' << number;
                }
                obj << '\n';
            }
        }
        if (covered != mesh.indices.size()) return false;
        obj.close(); mtl.close();
        if (!obj || !mtl) return false;
        std::ofstream proof(directory / "boardmed_contract.json", std::ios::trunc);
        proof.imbue(std::locale::classic());
        proof << std::setprecision(std::numeric_limits<float>::max_digits10)
            << "{\"production_geometry_decoder\":true,\"data_id\":" << id
            << ",\"units_per_meter\":1,\"yaw_degrees\":0,\"offset\":[0,0,0],"
               "\"ground_to_zero\":false,\"raw_engine_units\":true,\"vertices\":"
            << mesh.vertices.size() << ",\"triangles\":" << mesh.indices.size()/3
            << ",\"batches\":" << mesh.batches.size() << ",\"minimum\":[";
        for (std::size_t axis=0; axis<3; ++axis)
            proof << (axis ? "," : "") << mesh.bounds.minimum[axis];
        proof << "],\"maximum\":[";
        for (std::size_t axis=0; axis<3; ++axis)
            proof << (axis ? "," : "") << mesh.bounds.maximum[axis];
        proof << "]";
        if (recipe)
        {
            proof << ",\"texture_resolution\":256,\"geometry_uv_verified_unchanged\":true,\"source_textures\":[";
            bool first = true;
            for (std::size_t index = 0; index < mesh.batches.size(); ++index)
            {
                const auto& texture = mesh.batches[index].texture;
                if (!texture) continue;
                const auto& image = *texture->sourceImage;
                const auto reference = std::find_if(recipe->textures.begin(), recipe->textures.end(),
                    [&](const auto& item) { return item.coordinate.x == image.rawX && item.coordinate.y == image.rawY; });
                if (reference == recipe->textures.end()) return false;
                proof << (first ? "" : ",") << "{\"batch\":" << index << ",\"file\":" << jsonString(reference->fileName)
                    << ",\"raw_x\":" << image.rawX << ",\"raw_y\":" << image.rawY
                    << ",\"width\":" << image.width << ",\"height\":" << image.height << '}';
                first = false;
            }
            proof << ']';
        }
        proof << "}\n";
        proof.close();
        return static_cast<bool>(proof);
    }

}

int main(int argc, char** argv)
{
    using namespace monopoly::data;

    if (argc >= 2 && std::string_view(argv[1]) == "--environment-benchmark")
        return benchmarkEnvironment(argc,argv);
    if (argc >= 2 && std::string_view(argv[1]) == "--gltf")
        return probeGltf(argc, argv);

    const bool boardTextures256 = argc == 5 && std::string_view(argv[3]) == "--dump-textured-board-256";
    const bool texturedBoard = boardTextures256 || (argc == 5 && std::string_view(argv[3]) == "--dump-textured-board");
    const bool boardOnly = texturedBoard || (argc == 5 && std::string_view(argv[3]) == "--dump-board");
    if (argc != 3 && !(argc == 5 &&
            (std::string_view(argv[3]) == "--dump-retail" || boardOnly)))
    {
        std::cerr
            << "usage: MonopolyModernAssetProbe <retail-root> <modern-assets-root> "
               "[--dump-retail|--dump-board|--dump-textured-board|--dump-textured-board-256 <existing-cmake-build-dir>]\n";
        return 2;
    }

    const std::filesystem::path retailRoot =
        std::filesystem::absolute(argv[1]);
    const std::filesystem::path modernRoot =
        std::filesystem::absolute(argv[2]);
    std::optional<std::filesystem::path> dumpRoot;
    if (argc == 5)
    {
        std::error_code error;
        const auto build = std::filesystem::canonical(argv[4], error);
        bool underSource = false;
        for (const auto& component : build)
        {
            auto text = component.wstring();
            for (auto& character : text) character = static_cast<wchar_t>(std::towlower(character));
            if (text == L"source") underSource = true;
        }
        if (error || underSource || !std::filesystem::is_regular_file(build / "CMakeCache.txt", error))
        { std::cerr << "OBJ output requires an existing CMake build outside Source\n"; return 2; }
        const auto destination = build / (boardTextures256 ? "retail-textured-board-256" :
            texturedBoard ? "retail-textured-board" : "retail-reference-obj");
        std::filesystem::create_directories(destination, error);
        if (error) { std::cerr << "OBJ directory: " << error.message() << '\n'; return 1; }
        dumpRoot = std::filesystem::canonical(destination, error);
        if (error || !within(*dumpRoot, build))
        { std::cerr << "OBJ output directory escapes the selected build\n"; return 2; }
    }

    const std::array roots{retailRoot};
    auto paths = ResourcePaths::create(roots);
    if (!paths)
    {
        std::cerr << "resource root: " << paths.error().detail << '\n';
        return 1;
    }

    ResourceRuntime runtime;
    auto initialized = runtime.initialize(*paths);
    if (!initialized)
    {
        std::cerr << "resource runtime: "
                  << initialized.error().detail << '\n';
        return 1;
    }

    auto resources = runtime.snapshot();
    MeshRuntimeCache legacy(resources);
    std::optional<BoardTextureRecipe> boardRecipe;
    if (boardTextures256)
    {
        auto recipe = buildUsaTextureRecipe(BoardMeshKind::ClassicMedium, TextureResolution::Pixels256);
        if (!recipe) { std::cerr << recipe.error().detail << '\n'; return 1; }
        auto images = loadBoardTextureImages(*paths, *recipe, BoardTextureContext{});
        if (!images) { std::cerr << "USA256 texture recipe unavailable: " << images.error() << '\n'; return 1; }
        const auto original = legacy.resolve(recipe->meshDataId);
        if (!original || !(*original)->renderData) { std::cerr << "USA256 original board unavailable\n"; return 1; }
        const auto replaced = legacy.replaceTextureImages(recipe->meshDataId, *images);
        if (!replaced) { std::cerr << replaced.error().detail << '\n'; return 1; }
        const auto replacement = legacy.resolve(recipe->meshDataId);
        if (!replacement || !(*replacement)->renderData) return 1;
        const auto& before = *(*original)->renderData;
        const auto& after = *(*replacement)->renderData;
        bool identical = before.indices == after.indices && before.vertices.size() == after.vertices.size() &&
            before.batches.size() == after.batches.size() && before.bounds.minimum == after.bounds.minimum &&
            before.bounds.maximum == after.bounds.maximum;
        for (std::size_t index = 0; identical && index < before.vertices.size(); ++index)
        {
            const auto& a = before.vertices[index]; const auto& b = after.vertices[index];
            identical = a.position == b.position && a.normal == b.normal && a.uv == b.uv && a.tangent == b.tangent;
        }
        for (std::size_t index = 0; identical && index < before.batches.size(); ++index)
        {
            const auto& a = before.batches[index]; const auto& b = after.batches[index];
            identical = a.firstIndex == b.firstIndex && a.indexCount == b.indexCount &&
                a.material.rawDiffuse == b.material.rawDiffuse && a.material.diffuse == b.material.diffuse;
            if (b.texture)
            {
                const auto& image = b.texture->sourceImage;
                if (!image || image->width != 256 || image->height != 256) { identical = false; break; }
                const auto matched = std::find_if(images->begin(), images->end(), [&](const auto& item)
                { return item->texturePage == image->texturePage && item->rawX == image->rawX && item->rawY == image->rawY; });
                identical = matched != images->end() && (*matched)->rgba == image->rgba;
            }
        }
        if (!identical) { std::cerr << "USA256 substitution changed board geometry/UVs or image mapping\n"; return 1; }
        boardRecipe = std::move(*recipe);
    }
    bool failed = false;
    std::cout.imbue(std::locale::classic());

    std::cout << "retail representative token bounds\n"
              << "token\tslug\tlegacy_data_id\tminimum_xyz\tmaximum_xyz\tsize_xyz\tcenter_xyz\n";
    if (!boardOnly) for (const auto& definition : modernTokenDefinitions())
    {
        const auto id = representativeLegacyMesh(definition.token);
        const auto asset = legacy.resolve(id);
        if (!asset || !(*asset)->renderData)
        {
            std::cerr << definition.slug << ": representative legacy: "
                << (asset ? "missing render data" : asset.error().detail) << '\n';
            failed = true;
            continue;
        }
        const auto& bounds = (*asset)->renderData->bounds;
        const auto measured = dimensions(bounds);
        std::cout << static_cast<unsigned>(definition.token) << '\t' << definition.slug
                  << '\t' << id << '\t';
        printVector(bounds.minimum);
        std::cout << '\t';
        printVector(bounds.maximum);
        std::cout << '\t';
        printVector(measured.size);
        std::cout << '\t';
        printVector(measured.center);
        std::cout << '\n';
        if (dumpRoot && !exportObj(*dumpRoot / (std::string(definition.slug) + ".obj"),
            definition.slug, id, *(*asset)->renderData)) failed = true;
    }

    std::cout << "\nretail board and building bounds\n"
              << "slug\tlegacy_data_id\tminimum_xyz\tmaximum_xyz\tsize_xyz\tcenter_xyz\n";
    constexpr std::array<std::pair<DataTag, std::string_view>, 4> BoardAssets{{
        {DataTag{0x0001}, "board_citymed"}, {DataTag{0x0003}, "boardmed"},
        {DataTag{0x0004}, "hotel"}, {DataTag{0x0005}, "house"}}};
    for (const auto& [tag, name] : BoardAssets)
    {
        if (texturedBoard && tag != DataTag{0x0003}) continue;
        const auto id = packDataId(LegacyGroupId::ThreeD, tag);
        const auto asset = legacy.resolve(id);
        if (!asset || !(*asset)->renderData)
        {
            std::cerr << name << ": retail DataId " << id << ": "
                << (asset ? "missing render data" : asset.error().detail) << '\n';
            failed = true;
            continue;
        }
        const auto& bounds = (*asset)->renderData->bounds;
        const auto measured = dimensions(bounds);
        std::cout << name << '\t' << id << '\t';
        printVector(bounds.minimum); std::cout << '\t';
        printVector(bounds.maximum); std::cout << '\t';
        printVector(measured.size); std::cout << '\t';
        printVector(measured.center); std::cout << '\n';
        if (texturedBoard)
        {
            if (!exportTexturedBoard(*dumpRoot, id, *(*asset)->renderData, boardRecipe ? &*boardRecipe : nullptr)) failed = true;
            if (!exportSquareLabels(*dumpRoot, *resources)) failed = true;
        }
        else if (dumpRoot && !exportObj(*dumpRoot / (std::string(name) + ".obj"),
            name, id, *(*asset)->renderData)) failed = true;
    }

    if (boardOnly) return failed ? 1 : 0;

    std::cout
        << "\nmodern token calibration\n"
           "token\tslug\tunits_per_meter\tyaw_degrees\tlegacy_xyz\t"
           "modern_xyz\tscale_xyz(modern/legacy)\tlegacy_center\tmodern_center\n";

    for (const auto& definition : modernTokenDefinitions())
    {
        const auto glb =
            modernRoot / std::filesystem::path(definition.relativeGlbPath)
                .lexically_relative("assets/modern");

        std::error_code filesystemError;
        if (!std::filesystem::is_regular_file(glb, filesystemError))
        {
            // Missing optional authoring assets are expected. Real filesystem
            // failures (permissions, invalid path, etc.) still fail the probe.
            if (filesystemError && filesystemError != std::errc::no_such_file_or_directory)
            {
                std::cerr << definition.slug << ": modern file: " << filesystemError.message() << '\n';
                failed = true;
            }
            continue;
        }

        const auto legacyId =
            representativeLegacyMesh(definition.token);
        auto legacyAsset = legacy.resolve(legacyId);
        if (!legacyAsset || !(*legacyAsset)->renderData)
        {
            std::cerr << definition.slug << ": legacy: "
                      << (legacyAsset ? "missing render data" : legacyAsset.error().detail) << '\n';
            failed = true;
            continue;
        }

        ModernGltfLoadOptions loadOptions;
        loadOptions.unitsPerMeter = definition.unitsPerMeter;
        loadOptions.yawDegrees = definition.yawDegrees;
        loadOptions.localOffset = definition.localOffset;
        loadOptions.groundToZero = true;
        auto modernAsset = loadModernGltfMesh(glb, loadOptions);
        if (!modernAsset)
        {
            std::cerr << definition.slug << ": modern: "
                      << modernAsset.error().detail << '\n';
            failed = true;
            continue;
        }

        const auto oldDimensions =
            dimensions((*legacyAsset)->renderData->bounds);
        const auto newDimensions =
            dimensions((*modernAsset)->bounds);

        std::array<float, 3> ratio{};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            ratio[axis] = oldDimensions.size[axis] != 0.0F
                ? newDimensions.size[axis] / oldDimensions.size[axis]
                : 0.0F;
        }

        std::cout << static_cast<unsigned>(definition.token)
                  << '\t' << definition.slug
                  << '\t' << definition.unitsPerMeter
                  << '\t' << definition.yawDegrees << '\t';
        printVector(oldDimensions.size);
        std::cout << '\t';
        printVector(newDimensions.size);
        std::cout << '\t';
        printVector(ratio);
        std::cout << '\t';
        printVector(oldDimensions.center);
        std::cout << '\t';
        printVector(newDimensions.center);
        std::cout << '\n';
    }

    std::array<std::set<std::size_t>, ModernTokenCount> poseCounts;
    std::array<std::size_t, ModernTokenCount> hmdCounts{};
    std::array<std::size_t, ModernTokenCount> decodeFailures{};

    for (std::uint32_t rawTag = 0; rawTag <= 0x00E2U; ++rawTag)
    {
        const auto id = packDataId(
            LegacyGroupId::ThreeD,
            static_cast<DataTag>(rawTag));
        const auto token = tokenForLegacyMesh(id);
        if (!token)
            continue;

        ++hmdCounts[*token];
        auto asset = legacy.resolve(id);
        if (!asset || !(*asset)->mesh)
        {
            ++decodeFailures[*token];
            failed = true;
            std::cerr << "legacy HMD " << id << ": "
                << (asset ? "missing decoded mesh" : asset.error().detail) << '\n';
            std::cout << "decode_error\t" << unsigned(*token) << "\t0x" << std::hex << id
                << std::dec << '\t' << (asset ? "missing decoded mesh" : asset.error().detail);
            if (!asset)
            {
                std::cout << "\terror_code=" << static_cast<unsigned>(asset.error().code);
                if (asset.error().sourceError)
                    std::cout << "\tsource_error=" << asset.error().sourceError->detail;
            }
            std::cout << '\n';
            continue;
        }
        poseCounts[*token].insert((*asset)->mesh->poseCount());
    }

    std::cout << "\nretail MIMe pose inventory\n"
              << "token\tslug\thmd_count\tpose_counts\tdecode_failures\n";
    for (const auto& definition : modernTokenDefinitions())
    {
        const auto token = static_cast<std::size_t>(definition.token);
        std::cout << static_cast<unsigned>(definition.token)
                  << '\t' << definition.slug
                  << '\t' << hmdCounts[token] << '\t';
        bool first = true;
        for (const auto count : poseCounts[token])
        {
            if (!first) std::cout << ',';
            std::cout << count;
            first = false;
        }
        std::cout << '\t' << decodeFailures[token] << '\n';
    }

    constexpr DataTag CornerBase = 0x00F6;
    constexpr DataTag MoveBase = 0x010D;
    constexpr std::uint32_t AnimsPerToken = 0x0159 - 0x00F6;

    std::cout << "\nretail token sequence mesh inventory\n"
              << "token\tslug\tidle_sequence\tidle_hmds\t"
                 "movement_hmd_count\tmovement_hmds\n";

    for (const auto& definition : modernTokenDefinitions())
    {
        const auto token = static_cast<std::uint32_t>(definition.token);
        const auto idleTag = static_cast<DataTag>(
            static_cast<std::uint32_t>(MoveBase) +
            token * AnimsPerToken);
        const auto idleId = packDataId(LegacyGroupId::ThreeD, idleTag);

        std::set<DataTag> idleMeshes;
        std::set<DataTag> movementMeshes;

        const auto collectMeshes =
            [&](DataId sequenceId, std::set<DataTag>& output)
        {
            auto program = monopoly::sequence::SequenceProgram::load(
                resources, sequenceId);
            if (!program)
            {
                failed = true;
                std::cerr << "legacy sequence " << sequenceId << ": " << program.error().detail << '\n';
                return;
            }
            for (const auto& description : (*program)->descriptions())
            {
                if (!description.contentsDataId)
                    continue;
                const auto mapped =
                    tokenForLegacyMesh(*description.contentsDataId);
                if (mapped && *mapped == definition.token)
                    output.insert(dataTag(*description.contentsDataId));
            }
        };

        collectMeshes(idleId, idleMeshes);

        // Movement code uses move base + 0..10 and corner base + 0..4.
        // Both bases are offset by the same 99-sequence token stride.
        for (std::uint32_t offset = 0; offset <= 10; ++offset)
        {
            collectMeshes(
                packDataId(
                    LegacyGroupId::ThreeD,
                    static_cast<DataTag>(
                        static_cast<std::uint32_t>(MoveBase) +
                        token * AnimsPerToken + offset)),
                movementMeshes);
        }
        for (std::uint32_t offset = 0; offset <= 4; ++offset)
        {
            collectMeshes(
                packDataId(
                    LegacyGroupId::ThreeD,
                    static_cast<DataTag>(
                        static_cast<std::uint32_t>(CornerBase) +
                        token * AnimsPerToken + offset)),
                movementMeshes);
        }

        const auto printTags = [](const std::set<DataTag>& tags)
        {
            bool first = true;
            for (const auto tag : tags)
            {
                if (!first) std::cout << ',';
                std::cout << "0x" << std::hex
                          << static_cast<unsigned>(tag)
                          << std::dec;
                first = false;
            }
        };

        std::cout << static_cast<unsigned>(definition.token)
                  << '\t' << definition.slug
                  << "\t0x" << std::hex
                  << static_cast<unsigned>(idleTag)
                  << std::dec << '\t';
        printTags(idleMeshes);
        std::cout << '\t' << movementMeshes.size() << '\t';
        printTags(movementMeshes);
        std::cout << '\n';
    }

    return failed || !std::cout ? 1 : 0;
}
