# Contrôles automatiques du portage {#porting_audit}

> Généré depuis [la matrice](PORTING_MATRIX.md), [l’état courant](PORTING_STATUS.md), les sources modernes et CMake. Ce rapport vérifie la cohérence de l’inventaire ; il ne certifie ni la fidélité au jeu d’origine ni la portabilité sur une plateforme non testée.

## Inventaire

Les lignes peuvent partager des dépendances. Leurs nombres ne sont pas un pourcentage fonctionnel.

| Catégorie | Nombre de lignes |
|---|---:|
| Complètes ou remplacées | 37 |
| Écarts actifs connus (`PORTED_PARTIAL`) | 2 |
| Non commencées (`NOT_STARTED`) | 0 |
| Comparaison à mener (`REVIEW_REQUIRED`) | 28 |
| Données manquantes | 4 |
| Outils manquants | 1 |
| Hors périmètre, preuve d’absence d’usage | 7 |

Progression fonctionnelle : **Non établi**.

Validation de référence : **132/132 suites CTest** — code `d77be96`, Windows/MSVC Debug, 26 septembre 2026.

## Contrôles de cohérence

- PASS — Référence de validation et conventions documentaires.
- PASS — Tous les fichiers source .cpp figurent dans CMake.
- PASS — Tous les fichiers de tests .cpp figurent dans CMake.

## Écarts et comparaisons ouverts

Le [plan des travaux](PORTING_STATUS.md) fixe les priorités. Cette liste suit la matrice, sans pondération automatique ni verdict sur les fonctionnalités non examinées.

| Section | Origine | Statut | Référence |
|---|---|---|---|
| Jeu Monopoly | `Source/monopoly/Mess.cpp` | `REVIEW_REQUIRED` | [Matrice, ligne 25](PORTING_MATRIX.md#L25) |
| Jeu Monopoly | `Source/monopoly/Rule.cpp` | `REVIEW_REQUIRED` | [Matrice, ligne 26](PORTING_MATRIX.md#L26) |
| Jeu Monopoly | `Source/monopoly/trade.cpp` | `REVIEW_REQUIRED` | [Matrice, ligne 27](PORTING_MATRIX.md#L27) |
| Jeu Monopoly | `Source/monopoly/Ai.cpp` | `REVIEW_REQUIRED` | [Matrice, ligne 28](PORTING_MATRIX.md#L28) |
| Jeu Monopoly | `Source/monopoly/Ai_trade.cpp` | `REVIEW_REQUIRED` | [Matrice, ligne 30](PORTING_MATRIX.md#L30) |
| Jeu Monopoly | `Source/monopoly/display.cpp` | `REVIEW_REQUIRED` | [Matrice, ligne 36](PORTING_MATRIX.md#L36) |
| Jeu Monopoly | `Source/monopoly/Userifce.cpp` | `REVIEW_REQUIRED` | [Matrice, ligne 37](PORTING_MATRIX.md#L37) |
| Jeu Monopoly | `Source/monopoly/UDBoard.cpp` | `REVIEW_REQUIRED` | [Matrice, ligne 39](PORTING_MATRIX.md#L39) |
| Jeu Monopoly | `Source/monopoly/UDIBar.cpp` | `REVIEW_REQUIRED` | [Matrice, ligne 40](PORTING_MATRIX.md#L40) |
| Jeu Monopoly | `Source/monopoly/UDOpts.cpp` | `REVIEW_REQUIRED` | [Matrice, ligne 41](PORTING_MATRIX.md#L41) |
| Jeu Monopoly | `Source/monopoly/UDPieces.cpp` | `REVIEW_REQUIRED` | [Matrice, ligne 42](PORTING_MATRIX.md#L42) |
| Jeu Monopoly | `Source/monopoly/UDPsel.cpp` | `REVIEW_REQUIRED` | [Matrice, ligne 43](PORTING_MATRIX.md#L43) |
| Jeu Monopoly | `Source/monopoly/UDSound.cpp` | `REVIEW_REQUIRED` | [Matrice, ligne 44](PORTING_MATRIX.md#L44) |
| Jeu Monopoly | `Source/monopoly/UDStats.cpp` | `PORTED_PARTIAL` | [Matrice, ligne 45](PORTING_MATRIX.md#L45) |
| Jeu Monopoly | `Source/monopoly/UDTrade.cpp` | `REVIEW_REQUIRED` | [Matrice, ligne 46](PORTING_MATRIX.md#L46) |
| Jeu Monopoly | `Source/monopoly/UDChat.cpp` | `REVIEW_REQUIRED` | [Matrice, ligne 48](PORTING_MATRIX.md#L48) |
| Jeu Monopoly | `Source/monopoly/UDPenny.cpp` | `PORTED_PARTIAL` | [Matrice, ligne 53](PORTING_MATRIX.md#L53) |
| Jeu Monopoly | `Source/monopoly/UDUtils.cpp` | `REVIEW_REQUIRED` | [Matrice, ligne 54](PORTING_MATRIX.md#L54) |
| Services ArtLib consommes | `Source/artlib/L_Data.*` | `REVIEW_REQUIRED` | [Matrice, ligne 65](PORTING_MATRIX.md#L65) |
| Services ArtLib consommes | `Source/artlib/L_Seqncr.*` | `REVIEW_REQUIRED` | [Matrice, ligne 69](PORTING_MATRIX.md#L69) |
| Services ArtLib consommes | `Source/artlib/L_Sound.*, L_Midi.*` | `REVIEW_REQUIRED` | [Matrice, ligne 72](PORTING_MATRIX.md#L72) |
| Services ArtLib consommes | `Source/artlib/L_Video.*` | `REVIEW_REQUIRED` | [Matrice, ligne 73](PORTING_MATRIX.md#L73) |
| PC3D consomme | `cameras, viewports, background (camera.*, D3DDevice.*, view code)` | `REVIEW_REQUIRED` | [Matrice, ligne 79](PORTING_MATRIX.md#L79) |
| PC3D consomme | `meshes/scenes/materials (mesh*, NewMesh*, Scene.h, l_material.h)` | `REVIEW_REQUIRED` | [Matrice, ligne 80](PORTING_MATRIX.md#L80) |
| Donnees et verification | `Lifecycle, lookup, metadata et ownership` | `REVIEW_REQUIRED` | [Matrice, ligne 91](PORTING_MATRIX.md#L91) |
| Donnees et verification | `CRC global DAT` | `REVIEW_REQUIRED` | [Matrice, ligne 93](PORTING_MATRIX.md#L93) |
| Donnees et verification | `Parseurs semantiques CNK / sequence` | `REVIEW_REQUIRED` | [Matrice, ligne 97](PORTING_MATRIX.md#L97) |
| Donnees et verification | `Arbre runtime et execution` | `REVIEW_REQUIRED` | [Matrice, ligne 100](PORTING_MATRIX.md#L100) |
| Donnees et verification | `Transformations / tweekers` | `REVIEW_REQUIRED` | [Matrice, ligne 102](PORTING_MATRIX.md#L102) |
| Donnees et verification | `LANG core` | `REVIEW_REQUIRED` | [Matrice, ligne 106](PORTING_MATRIX.md#L106) |
| Donnees et verification | `Chaines/audio/dialogues LANG retail` | `BLOCKED_MISSING_DATA` | [Matrice, ligne 107](PORTING_MATRIX.md#L107) |
| Donnees et verification | `DMAKE99 et reconstruction bit-a-bit` | `MISSING_TOOLING` | [Matrice, ligne 112](PORTING_MATRIX.md#L112) |
| Donnees et verification | `Banques DAT retail exactes` | `BLOCKED_MISSING_DATA` | [Matrice, ligne 113](PORTING_MATRIX.md#L113) |
| Donnees et verification | `2DVIEW01..39 externes` | `BLOCKED_MISSING_DATA` | [Matrice, ligne 114](PORTING_MATRIX.md#L114) |
| Donnees et verification | `HMD retail / objets MESHX` | `BLOCKED_MISSING_DATA` | [Matrice, ligne 115](PORTING_MATRIX.md#L115) |

## Marqueurs informatifs

0 occurrences de TODO/FIXME/XXX/TBD dans les sources et tests. Un marqueur peut décrire un fixture ou une limite volontaire ; il ne prouve pas un manque actif.
