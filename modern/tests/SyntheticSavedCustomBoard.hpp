#pragma once

#include "SyntheticSequenceResources.hpp"
#include "TextureCatalog.hpp"

#include <fstream>

// Test-only external BMP set, paired with synthetic retail archives. It never
// installs a .brd file or edits the ownership registry: saved-game restoration
// uses the persisted asset directory independently of initial board selection.
struct SyntheticSavedCustomBoard
{
    SyntheticSequenceResources stock;
    monopoly::data::ResourceRuntime localized;
    std::shared_ptr<const monopoly::data::ResourceSnapshot> resources;
    std::filesystem::path root = stock.directory / "Saved board";

    static void write(const std::filesystem::path& path,
        const monopoly::data::DataBytes& bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!output) throw std::runtime_error("synthetic custom BMP write failed");
    }

    static monopoly::data::DataBytes texture()
    {
        monopoly::data::DataBytes bytes(1078 + 128 * 128, std::byte{0});
        const auto put = [&](std::size_t at, std::uint32_t value, int count)
        {
            for (int i = 0; i < count; ++i)
                bytes[at + i] = static_cast<std::byte>((value >> (8 * i)) & 255);
        };
        bytes[0] = std::byte{'B'}; bytes[1] = std::byte{'M'};
        put(2, static_cast<std::uint32_t>(bytes.size()), 4);
        put(10, 1078, 4); put(14, 40, 4); put(18, 128, 4); put(22, 128, 4);
        put(26, 1, 2); put(28, 8, 2); put(34, 128 * 128, 4); put(46, 256, 4);
        bytes[60] = std::byte{201};
        std::fill(bytes.begin() + 1078, bytes.end(), std::byte{1});
        return bytes;
    }

    explicit SyntheticSavedCustomBoard(bool europe = false)
    {
        using namespace monopoly::data;
        resources = stock.service.snapshot();
        if (europe)
        {
            const auto copy = [&](const BankDefinition& from, const BankDefinition& to)
            {
                const auto source = resources->paths().resolve(from.legacyPath);
                if (!source) throw std::runtime_error("synthetic localized source unavailable");
                const auto destination = stock.directory / std::filesystem::path(to.legacyPath);
                if (*source != destination)
                {
                    std::filesystem::create_directories(destination.parent_path());
                    std::filesystem::copy_file(*source, destination,
                        std::filesystem::copy_options::overwrite_existing);
                }
            };
            const auto& usa = coreBanks(BoardEdition::Usa);
            const auto& euro = coreBanks(BoardEdition::Europe);
            for (std::size_t i = 0; i < usa.size(); ++i) copy(usa[i], euro[i]);
            const auto& us = *findLanguageBankTriplet(LanguageId::EnglishUs);
            const auto& fr = *findLanguageBankTriplet(LanguageId::French);
            copy(us.text, fr.text); copy(us.graphics, fr.graphics); copy(us.dialog, fr.dialog);
            const auto paths = ResourcePaths::create(std::array{stock.directory});
            if (!paths || !localized.initialize(*paths, {BoardEdition::Europe, LanguageId::French}))
                throw std::runtime_error("synthetic localized snapshot failed");
            resources = localized.snapshot();
        }
        for (const auto name : twoDimensionalBoardTextureNames())
            write(root / "2DBoards" / name, SyntheticSequenceResources::bitmap24());
        const BoardTextureContext context{resources->context().board,
            resources->context().language, -1, europe ? 5 : 13, root};
        const auto recipe = europe
            ? buildEuropeanTextureRecipe(BoardMeshKind::CityMedium, TextureResolution::Pixels128)
            : buildUsaTextureRecipe(BoardMeshKind::CityMedium, TextureResolution::Pixels128);
        if (!recipe) throw std::runtime_error("synthetic custom recipe invalid");
        const auto writeReference = [&](TextureLocation location, std::string_view name)
        {
            const auto relative = boardTextureRelativePath(recipe->mesh, location, name, context);
            if (!relative) throw std::runtime_error("synthetic custom path invalid");
            const bool custom = location == TextureLocation::SelectedCityNames ||
                location == TextureLocation::SelectedCityPhotos;
            write((custom ? root : stock.directory) / *relative, texture());
        };
        for (const auto& reference : recipe->textures)
        {
            writeReference(reference.location, reference.fileName);
            if (reference.overlay) writeReference(reference.overlay->location, reference.overlay->fileName);
        }
    }

    std::string savedName() const
    {
        const auto value = root.u8string();
        return {reinterpret_cast<const char*>(value.data()), value.size()};
    }
};
