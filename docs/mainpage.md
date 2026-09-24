# Monopoly — Portage moderne

Bienvenue dans la documentation technique du portage moderne de **Monopoly**.

Le dépôt conserve le code historique dans `Source/` et développe une
implémentation portable dans `modern/`, en **C/C++23**, avec **SDL3** et
**SDL_GPU**.

<img src="porting-progress.svg" alt="Progression automatisée du portage" />

## Documentation

- **API moderne** — classes, structures, fonctions et fichiers de `modern/src/`.
- **Suivi du portage** — `modern/PORTING_STATUS.md`.
- **Audit structurel** — `modern/PORTING_AUDIT.md`.
- **Validation continue** — build Windows/MSVC et suite CTest via GitHub Actions.

## Principes du portage

Le code historique reste une référence de comportement. Le portage moderne
remplace progressivement les dépendances Windows/DirectX d'origine par des
composants portables, tout en conservant les contrats fonctionnels utiles.

## Liens

- [Dépôt Monopoly](https://github.com/Giscolab/Monopoly)
- [Code source historique public](https://github.com/RetailGameSourceCode/Monopoly)
- [Tableau de progression](https://github.com/Giscolab/Monopoly/blob/master/modern/PORTING_STATUS.md)

> Cette documentation est générée automatiquement par Doxygen et publiée
> sur GitHub Pages à chaque modification pertinente de la branche `master`.
