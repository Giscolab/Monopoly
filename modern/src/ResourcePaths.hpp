#pragma once

#include "LegacyDataArchive.hpp"

#include <expected>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace monopoly::data
{
    // Racines explicites, dans leur ordre de priorite. Aucun chemin courant,
    // lecteur CD ou repli sur le seul nom de fichier n'est ajoute implicitement.
    class ResourcePaths final
    {
    public:
        [[nodiscard]] static std::expected<ResourcePaths, DataError> create(
            std::span<const std::filesystem::path> roots);

        // Les noms historiques Windows acceptent les deux separateurs et une
        // casse ASCII indifferente. Deux entrees de meme casse repliee sont une
        // erreur, meme si l'une correspond exactement au nom demande.
        // Les liens symboliques suivent les regles du filesystem : ceci n'est
        // pas une frontiere de confinement contre les liens hors d'une racine.
        [[nodiscard]] std::expected<std::filesystem::path, DataError> resolve(
            std::string_view relativePath) const;

        [[nodiscard]] std::span<const std::filesystem::path> roots()
            const noexcept;

    private:
        std::vector<std::filesystem::path> roots_;
    };
}
