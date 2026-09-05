# Monopoly 1999 -> modern porting status

Cette matrice est la carte unique du port moderne. La source originale reste
l'autorite semantique; les statuts ci-dessous decrivent uniquement le code
effectivement present sous `modern/`.

## Legende

- `PORTED_COMPLETE` : contrat original identifie, integre et verifie sans
  omission connue dans le perimetre indique.
- `PORTED_PARTIAL` : une partie reelle du comportement existe, mais le module
  conserve des omissions connues.
- `NOT_STARTED` : aucun equivalent fonctionnel significatif n'existe encore.
- `REPLACED_PORTABLE` : le service legacy a un equivalent portable couvrant
  le contrat consomme par Monopoly.
- `BLOCKED_MISSING_DATA` : le code ou les identifiants existent, mais les
  payloads necessaires ne sont pas disponibles et ne doivent pas etre inventes.
- `MISSING_TOOLING` : un outil historique necessaire a une reproduction
  bit-a-bit n'est pas present dans le depot inspecte.
- `LEGACY_UNUSED` : le callgraph a prouve que Monopoly ne consomme pas ce
  composant. Ne pas utiliser ce statut sans preuve.

## Jeu Monopoly

| Original | Equivalent moderne | Statut | Symboles originaux importants | Dependances | Travail restant / blocage |
|---|---|---|---|---|---|
| `Source/monopoly/Main.cpp` | `Application`, `Engine`, `Game`, `RenderSlots`, `Timers`, `GPUFrame` | `PORTED_PARTIAL` | `GameInitialise`, `InitRenderSlots`, `ProcessUIMessage`, `GameShutdown`, `GameUpdateCycle` | SDL3, SDL_GPU, DISPLAY, UI messages | Lifecycle DATA/LANG et timers raccorde. Le slot historique World3D 1 alimente maintenant un renderer SDL_GPU reel jusqu au swapchain/offscreen, avec tests D3D12 de readback; le renderer UI/2D et le parcours interactif retail restent incomplets. |
| `Source/monopoly/GameInc.cpp/.h` | `RuleTypes`, `DataBanks`, `LegacyDataArchive`, includes modernes explicites | `PORTED_PARTIAL` | constantes `DAT_*`, `MAIN_GAME_TIMER`, configuration de build | Tous les sous-systemes | Groupes, chemins de banques et triplets de langue sont portes; continuer a remplacer le precompiled-header implicite par des interfaces explicites. |
| `Source/monopoly/Mdef.cpp` | `RuleTypes`, `RuntimeState`, `BoardRules` | `PORTED_PARTIAL` | constantes de jeu, langues, plateaux, joueurs | RULE, LANG, DISPLAY | Comparaison systematique des constantes encore a terminer. |
| `Source/monopoly/Mess.cpp` | `Messaging` | `PORTED_PARTIAL` | `MESS_InitializeSystem`, `MESS_SendAction`, `MESS_ReceiveActionMessage`, modes serveur/reseau | RULE, UI, futur transport | File locale fonctionnelle; DirectPlay/Winsock volontairement absents; saturation, voice-chat et transport futur restent a porter. |
| `Source/monopoly/Rule.cpp` | `RulesEngine`, `Rule*`, `BoardRules`, `PhaseStack`, `CardDeck*` | `PORTED_PARTIAL` | creation, phases, tours, economie, cartes, prison, enchere, trade, save/resync | Messaging, RNG, donnees plateau | Couverture importante mais inventaire symbole-par-symbole et scenarios complets encore requis; ne pas reecrire RULE. |
| `Source/monopoly/trade.cpp` | `RuleTrade` | `PORTED_PARTIAL` | editions, acceptations, immunites, loyers futurs | RULE, Messaging, UI trade | Logique RULE largement presente; ecran `UDTrade` et validation scenario bout-en-bout absents. |
| `Source/monopoly/Ai.cpp` | Aucun | `NOT_STARTED` | boucle de decision, achat, enchere, prison, construction | RULE, `Ai_load`, donnees AI | Porter l'architecture de decision; aucun fallback aleatoire ne doit la remplacer. |
| `Source/monopoly/Ai_load.cpp` | Aucun | `NOT_STARTED` | chargement `Boot.ai`, `Normal.ai`, parametres | fichiers `*.AI`, parser | Identifier le format et les fichiers reels avant implementation. |
| `Source/monopoly/Ai_trade.cpp` | Aucun | `NOT_STARTED` | evaluation et negociation de trades | AI, RuleTrade | Porter apres le noyau AI et ajouter des tests deterministes. |
| `Source/monopoly/Ai_util.cpp` | Aucun | `NOT_STARTED` | evaluations joueurs/proprietes/cash | AI, BoardRules | Extraire les calculs purs puis tester contre les constantes originales. |
| `Source/monopoly/Lang.cpp` | `LanguageCatalog`, `LanguageService`, `ResourceRuntime`, `DataBanks`, `LegacyTextIds` | `PORTED_PARTIAL` | `LANG_InitializeSystem`, `StartupExternalLanguage`, `GetLanguageString`, fallback, clean | lecteur DAT, index packed, UTF-16LE | Contrats existants conserves; startup publie les trois banques LANG partagees avec le registre DATA avant MESS. Les snapshots anciens conservent des chargements non caches apres remplacement/arret. Restent la migration des callers UI, fonts/mesures/impression et le formatage locale; textes retail bloques separement. |
| `Source/monopoly/TexInfo.cpp` | `TextureCatalog` | `PORTED_COMPLETE` | tableaux USA/Europe, recettes 8/16/14/22, overlays, 39 vues 2D, tags HMD | BMP bruts, futur resolver/mesh | Catalogue CPU, ordre, coordonnees, noms et anomalie `CityHigh128 -> *_256.BMP` sont portes et testes. Les meshes/resolutions hors contrat produisent une erreur typee avant toute indexation. L'application GPU/PC3D des substitutions appartient au futur chargeur de mesh. |
| `Source/monopoly/Tickler.cpp` | `TimeStep`, `Messaging`, `UserInterface` | `PORTED_PARTIAL` | `AdvanceTimeStep`, action unique par cycle, `ACTION_TICK` | Timers, RULE, AI | Routage local/broadcast et reset moderne presents; verrou de game queue, AI et voice-chat restent absents. |
| `Source/monopoly/L_voice.cpp` | Aucun | `NOT_STARTED` | voix, capture/lecture, timing | audio portable, MESS | Identifier les contrats consommes; ne pas porter les codecs/wrappers Win32 litteralement. |
| `Source/monopoly/display.cpp` | `Display`, `GPUFrame`, `RenderSlots`, `LogicalViewport` | `PORTED_PARTIAL` | `DISPLAY_initialize`, `DISPLAY_tickActions`, `DISPLAY_showAll2`, `DISPLAY_destroy` | modules UD, renderer, assets | Ordre relatif, cycle desired/current et repere logique letterbox conserves; renderer 2D et nombreux modules manquent. |
| `Source/monopoly/Userifce.cpp` | `UserInterface`, `LocalPlayers`, `TimeStep`, `ExtendedInitialization`, `ResourceRuntime` | `PORTED_PARTIAL` | `MainExtendedInitialization`, `ProcessLibraryMessage`, `ProcessMessageToPlayer`, `ProcessPlayersUI` | RULE, DISPLAY, LANG, CHAT | Cinq banques core puis LANG avant MESS raccordes; absence/corruption bloque le startup avec erreur typee. Frontiere locale, `NOTIFY_GAME_STARTING` et pause routes. Le miroir UI reste incomplet pour cash, plateau, trade, enchere, prison, etc. |
| `Source/monopoly/UDAuct.cpp` | `RuleAuction` couvre seulement la regle | `NOT_STARTED` | init/destroy/tick/show/process message de l'ecran enchere | DISPLAY, IBar, RULE auction | Creer le module UI sans dupliquer l'etat authoritative de `RuleAuction`. |
| `Source/monopoly/UDBoard.cpp` | etat de backdrop dans `Display`, geometie dans `BoardGeometry` | `PORTED_PARTIAL` | `UDBOARD_SetBackdrop`, `DISPLAY_UDBOARD_Show` | assets plateau, renderer, PC3D | Valeurs des huit ecrans, etat initial invalide, commit differe et mapping Main/Portfolio/Trade portes; composition et hotspots absents. |
| `Source/monopoly/UDIBar.cpp` | `IBar`, `IBarLayout` | `PORTED_PARTIAL` | initialize/destroy/tick/show/process, filtrage joueurs | LocalPlayers, DISPLAY, assets/fonts | Visibilite setup/Main/Portfolio/Trade et layout purs presents; aucun dessin reel, icones, textes, cash ou animation complete. |
| `Source/monopoly/UDOpts.cpp` | Aucun | `NOT_STARTED` | options UI | persistence options, DISPLAY | Porter apres le resolver de ressources et le renderer UI. |
| `Source/monopoly/UDPieces.cpp` | `PiecePlacement`, `PieceRuntime`, `PieceCamera`, `BoardGeometry` | `PORTED_PARTIAL` | orientation tokens/repos, maisons/hotels, resolution de pose runtime et choix cameras 3/5/15 | `SequenceRuntime`, TextureCatalog, PC3D moderne, BoardGeometry, RULE | Offsets 5x6, rotations token, constantes batiments derivees du source, GetInfo/GetChildMeshWorldMatrix -> LastKnownData et les 42 mappings camera de chaque famille sont portes/testes. `PickAGoodCamera` est aussi porte et les indices 0..38 sont verifies contre `2DVIEW01..39`. Restent planification/stack d animations, show/lifecycle et raccordement complet au DISPLAY. |
| `Source/monopoly/UDPsel.cpp` | `PlayerSelection`, `PlayerSetupFlow`, `LocalPlayers`, `IBar` | `PORTED_PARTIAL` | phases, noms, tokens, humains/IA, add/remove/start | UserInterface, Messaging, DISPLAY | Commit de phase limite a `DISPLAY_UDPSEL_Show`, refresh one-shot, reset zero-joueur et initialisation de la premiere notification non nulle portes; SelectCity/regles, projections mortes et rendu restent a traiter. |
| `Source/monopoly/UDSound.cpp` | Aucun | `NOT_STARTED` | options son, volume, musique/SFX | service audio portable, persistence | Choisir l'equivalent portable apres inventaire des assets et callers. |
| `Source/monopoly/UDStats.cpp` | Aucun | `NOT_STARTED` | statistiques joueur/partie | miroir UI, fonts, assets | Porter state/layout/show/process apres consolidation du miroir UI. |
| `Source/monopoly/UDTrade.cpp` | `RuleTrade` couvre seulement la regle | `NOT_STARTED` | editeur, acceptance, presentation trade | miroir UI, RuleTrade, DISPLAY | L'UI de trade reste entierement a porter. |
| `Source/monopoly/UDCGE.cpp` | Aucun | `NOT_STARTED` | editeur/outil runtime appele par le jeu | DISPLAY, data | Determiner le callgraph runtime exact avant de classer ou porter. |
| `Source/monopoly/UDChat.cpp` | Aucun; `Messaging` accepte les futurs messages | `NOT_STARTED` | `CHAT_InitializeSystem`, saisie, `ACTION_TEXT_CHAT` | fonts, DAT_MAIN, LANG, transport | Remplacer le controle d'edition Win32; ressources DAT actuellement manquantes. |
| `Source/monopoly/UDPlrCfg.cpp` | Aucun | `NOT_STARTED` | configuration joueur | PlayerSelection, options, assets | Comparer les ecrans et transitions avant implementation. |
| `Source/monopoly/UDPlrSum.cpp` | Aucun | `NOT_STARTED` | resume joueur | miroir UI, DISPLAY | Porter apres les notifications de jeu dans le miroir UI. |
| `Source/monopoly/UDRules.cpp` | `RuleOptions` couvre la regle, pas l'ecran | `NOT_STARTED` | choix/reglage des regles | RuleOptions, PlayerSetupFlow, DISPLAY | Implementer une projection controlee de `RuleOptions`. |
| `Source/monopoly/UDTitle.cpp` | Aucun | `NOT_STARTED` | title/opening flow | video, assets, DISPLAY | Identifier les ecrans et l'ordre; Bink/AVI seront remplaces par un service portable. |
| `Source/monopoly/UDPenny.cpp` | Aucun | `NOT_STARTED` | Penny Bags/assistant | animations, audio, assets | Porter seulement les comportements effectivement appeles. |
| `Source/monopoly/UDUtils.cpp` | `UDUtils`, `ResourcePaths`, `ResourceContext`, `TextureCatalog` | `PORTED_PARTIAL` | chemins, INI, choix HMD, substitutions de textures | filesystem, DATA, assets | Resolver portable raccorde au startup DATA selon edition/langue, racines explicites et casse ASCII des chemins legacy. Adaptation des recettes ville/langue/plateau/devise aux chemins BMP et au futur mesh encore requise. INI/CD et recherche par basename remplaces. |
| `Source/monopoly/Unility.cpp` | Aucun mapping complet confirme | `NOT_STARTED` | utilitaires de jeu | callers a inventorier | Etablir definitions et callers avant tout port. |
| `Source/monopoly/Debugart.cpp` | Aucun | `NOT_STARTED` | outils/debug runtime | callgraph | Ne classer `LEGACY_UNUSED` qu'apres preuve par callgraph. |

## Services ArtLib consommes

| Original | Equivalent moderne | Statut | Contrat consomme | Dependances | Travail restant / blocage |
|---|---|---|---|---|---|
| `Source/artlib/L_Main.*` | `Application`, `Engine`, `Game` | `REPLACED_PORTABLE` | boucle evenementielle, init/shutdown | SDL3 | Fenetre redimensionnable/HiDPI, presentation, erreurs de startup, creation/device/swapchain et lifecycle GPU sont raccordes et testes sur D3D12. Vulkan/Metal restent architecturaux mais non compiles ici; un smoke test interactif retail reste requis. |
| `Source/artlib/L_Timers.*` | `Timers` + `UIMessages` | `REPLACED_PORTABLE` | horloge 60 Hz, 4 timers, speed/restart, evenement index+tick | steady_clock, file UI | Contrat actuellement raccorde teste de facon deterministe; les appels historiques `LE_TIMER_Delay` identifies dans `Main`, `UDUtils` et les diagnostics DISPLAY appartiennent a des chemins remplaces, inactifs ou encore differes. |
| `Source/artlib/L_UIMsg.*` | `UIMessages` | `PORTED_PARTIAL` | FIFO 100, evenements timer/input, delestage | Application, Timers | Ajouter davantage de tests de pression/coalescence et les types requis par les futurs modules. |
| `Source/artlib/L_Data.*` | `DataBanks`, `LegacyDataArchive`, `DataBankRegistry`, `LegacyDataArchiveBuilder` | `PORTED_PARTIAL` | DataId 16:16, groupes, header/index DAT, zlib, chargement paresseux, refs | zlib, filesystem | Lecture LE explicite, validation, metadata, cache, leases partagees, mount/unmount, index packed et writer de fixtures sont testes. Restent les sources runtime/user-created/external-file, le budget/LRU automatique et une validation sur banque retail. |
| `Source/artlib/L_Chunk.*` | `LegacyChunkReader`, `openLegacyChunkReader` | `PORTED_COMPLETE` | lecteur consomme : header 24-bit + ID 8-bit, descend/ascend/seek/map/read, limite 8 niveaux | `LegacyDataArchive`, futur `L_Seqncr` | Contrat read-only et ownership `ReadFromDataID` portes sans bitfield ABI et testes sur fixtures, y compris le franchissement historique des siblings ID 0/128 lors d'une recherche precise. La validation d'un CNK retail reste bloquee separement; l'editeur/writer non consomme n'est pas dans ce perimetre. |
| `Source/artlib/L_Grafix.*`, `L_Rend2D.*`, `L_Sprite.*` | `Display`, futur renderer UI SDL_GPU | `PORTED_PARTIAL` | composition 2D, clipping, priorites, surfaces | SDL_GPU, assets, transformation 800x600 | Seul le cycle d'etat existe; rendu des objets/sprites/fonts a construire. |
| `Source/artlib/L_Rend3D.*` | `GPUFrame`, `SequenceWorld3DSlot`, `World3DGPUScene`, `World3DProjection`, `World3DRenderer` | `PORTED_PARTIAL` | slot World3D 1, viewport, camera/projection, bounds/culling, draw indexed et meshes animes | SDL_GPU, `SequenceRenderData`, PC3D moderne | Le chemin sequence -> slot 1 -> scene GPU -> renderer -> GPUFrame est actif. Bounds, projection ecran, culling, textures HMD et vertex buffers MIMe par node sont testes sur D3D12 reel. Camera 3D/FOV/SetCamera sont raccordes. La visibility 3D historique est maintenant auditee : `SequenceMoved()` retourne toujours TRUE pour un mesh (commentaire source inclus), donc le culling moderne reste strictement renderer-only et ne pilote pas `scrollingWorld`. Restent certains contrats de scene et primitives HMD non consommees. |
| `Source/artlib/L_Seqncr.*` | `LegacySequence`, `SequenceClock`, `SequenceChildSchedule`, `SequenceProgram`, `SequenceRuntime`, `SequenceCommandQueue`, `SequenceTransforms`, `SequenceRenderData` | `PORTED_PARTIAL` | records, arbre runtime, lifecycle, commandes actives, transformations/tweekers, mesh choice et intention 3D | `LegacyChunkReader`, DATA, `MeshRuntime`, `SequenceWorld3DSlot` | Grouping/indirect/tweeker, feuilles mesh 3D et records camera 3D sont executes; Start/Stop/SetEndingAction, MoveTheWorks/MoveXY/MoveRySTxz et SetCamera sont raccordes. `GetInfo` expose le sous-ensemble effectivement lu par Monopoly (clock/endTime/matrice monde 3D) avec la recherche `FindNextSequence`, et `GetChildMeshWorldMatrix` parcourt uniquement le sous-arbre du premier root selectionne. ForceRedraw est maintenant porte avec son cycle transitoire de redraw et la reevaluation cible/ancetres. Restent surtout labels generiques, bitmap/model/sound, callbacks et chains selon callers reels. |
| `Source/artlib/L_Fonts.*`, `L_Print.*` | Aucun | `NOT_STARTED` | Arial 10, mesure/rendu de texte | font rasterizer portable, LANG | Choisir un backend portable et conserver metriques/layout observables. |
| `Source/artlib/L_Keybrd.*`, `L_Mouse.*` | traduction SDL dans `Application`, `MousePointer` partiel | `REPLACED_PORTABLE` | input clavier/souris | SDL3, `LogicalViewport` | Souris reconvertie vers 800x600 et bandes noires rejetees; rendu du pointeur et certains types d'evenements restent partiels. |
| `Source/artlib/L_Sound.*`, `L_Midi.*` | Aucun | `NOT_STARTED` | WAV, musique, mixage, voice | audio portable, data | Inventorier les appels Monopoly et remplacer DirectSound/MIDI legacy. |
| `Source/artlib/L_Video.*` | Aucun | `NOT_STARTED` | opening movies | decoder/service video portable | Identifier fichiers et timing avant choix technique; ne pas porter VFW/Bink litteralement. |

## PC3D consomme

| Original | Equivalent moderne | Statut | Contrat consomme | Dependances | Travail restant / blocage |
|---|---|---|---|---|---|
| cameras, viewports, background (`camera.*`, `D3DDevice.*`, view code) | `GPUFrame`, `World3DProjection`, `World3DRenderer`, `Display::Viewport3D`, `LogicalViewport` | `PORTED_PARTIAL` | rectangles Main/Status/Trade, view/projection, depth, bounds ecran et clear | SDL_GPU | Camera/view/projection portable et viewport letterbox sont raccordes au renderer et testes sur D3D12. Le record camera 7, FOV 144, SetCamera direct et selection par label camera sont portes; l angle est confirme comme FOV complet par `PC3D/Matrix.inl`. Le source 3D ne remonte pas son culling au sequenceur (`SequenceMoved` retourne TRUE); restent surtout les transitions UDBoard/UDPieces au fil du port UI. |
| meshes/scenes/materials (`mesh*`, `NewMesh*`, `Scene.h`, `l_material.h`) | `MeshXRuntime`, `MeshRuntimeCache`, `MeshRenderData`, `MeshGPUCache`, `World3DGPUScene` | `PORTED_PARTIAL` | postload HMD, groupes materiau/texture, poses MIMe, bounds, donnees indexees et buffers GPU | `LegacyMeshData`, SDL_GPU | Pour Monopoly, `USE_OLD_FRAME` rend `hmdload.cpp`/`meshx` autoritatif; `NewMesh.cpp` est le chemin alternatif inactif. Triangles actuellement decodes, textures HMD embarquees, pose 0 + diff MIMe, interpolation/extrapolation mesh choice et buffers vertex GPU par node sont portes. Restent autres primitives/categories et substitutions BMP externes `TextureCatalog`. |
| decodeur HMD / postload MESHX (`HMDData.h`, `NewMesh.cpp`, `hmdload.*`) | `LegacyMeshData`, `openLegacyMeshData`, `MeshXRuntime` | `PORTED_PARTIAL` | HMD disque LE, offsets DWORD, triangles, `GsUIMG1`, `GsVtxMIMe`/`GsNrmMIMe`, puis construction mesh CPU | `DAT_3D`, textures, GPU | Structure bornee, cycles/budgets, triangles categorie 0, image 8-bit CLUT -> RGBA8 et diff blocks vertex/normal MIMe sont testes sur fixtures synthetiques. Reset/joint MIMe et autres categories/primitives restent explicites/non portees. MESHX demeure un resultat memoire, jamais un second format disque. |
| vieux DirectDraw/Direct3D drivers (`DDraw*`, `D3DDevice*`) | SDL3 / SDL_GPU | `REPLACED_PORTABLE` | creation device, swapchain, soumission | SDL3 | Les implementations legacy ne seront pas portees; completer seulement les comportements de rendu consommes. |

## Donnees et verification

| Sous-composant DATA | Equivalent moderne / preuve | Statut | Limite exacte |
|---|---|---|---|
| `DataId` / `DataTag` / groupes | `DataBanks`, y compris `IdWithFileFromParent` | `PORTED_COMPLETE` | Contrat 16 bits groupe + 16 bits tag; groupe zero reserve, tag zero valide. |
| Header et index physique DAT | `LegacyDataArchive` | `PORTED_COMPLETE` | Header Win32 28 octets et records 16 octets lus champ par champ en LE; signature/version/types/ranges/troncatures testes. |
| Codec DAT | zlib via `uncompress2` avec consommation exacte | `PORTED_COMPLETE` | `ZImplode.c` prouve un stream zlib enveloppe (`windowBits=15`), pas un codec PKWARE opaque. |
| Lifecycle, lookup, metadata et ownership | `LegacyDataArchive`, `DataBankRegistry`, `ResourceRuntime` | `PORTED_PARTIAL` | Montage des huit banques raccorde au jeu; publication transactionnelle et conservation des snapshots lecteurs testees sur succes/echec/reprise. Sources runtime, external-file et LRU automatique restent a porter. |
| Resolution des chemins DATA | `ResourcePaths`, `UDUtils`, `ResourceContext` | `REPLACED_PORTABLE` | Racines absolues ordonnees, separateurs legacy, casse ASCII, erreurs de collision/acces et choix USA/Europe + langues 1..10. Raccordement reel SDL/variable de processus teste; adaptation contextuelle des textures encore distincte. |
| CRC global DAT | option `ChecksumPolicy::Verify` | `PORTED_PARTIAL` | Le runtime original ne le verifiait pas et aucune banque retail ne confirme la convention du writer; politique runtime par defaut `Ignore`, audit explicite disponible. |
| Writer DAT portable | `LegacyDataArchiveBuilder` | `PORTED_COMPLETE` | Reconstruit deterministement un conteneur valide depuis des payloads fournis; ne pretend pas reproduire les payloads ou octets retail. |
| Index logique packed 6 octets | `DataIndexTable`, `lookupIndexedDataId` | `PORTED_COMPLETE` | Tri strict, cles dupliquees rejetees, plusieurs cles vers le meme tag permises, groupe parent reapplique. |
| Lecteur CNK | `LegacyChunkReader`, `openLegacyChunkReader` | `PORTED_COMPLETE` | Lecture hierarchique source-compatible sur fixtures, y compris la semantique des sentinelles nulles selectionnees ou sautees; aucune validation retail faute de CNK. |
| Parseurs semantiques CNK / sequence | `LegacySequence` | `PORTED_PARTIAL` | En-tete 12 octets, temps signes 24 bits, records grouping/indirect/bitmap/model/sound/camera/mesh/tweeker et attributs prives dimensionality/offset/matrix/OSRT sont bornes. Le record camera packe fait 21 octets (near/far/label); les chunks 139 `3D_MESH_CHOICE` et 144 `CAMERA_FIELD_OF_VIEW` sont decodes explicitement. Video/preloader et attributs encore inconnus sont refuses tant que leurs effets ne sont pas portes. |
| `SequenceClock` | `SequenceClock` | `PORTED_COMPLETE` | Cadence, premiere evaluation, drop/catch-up enfant, pause/reprise, stop, seek, hold cadence 255 et boucle naturelle a zero sont testes pour les types actuellement executables. |
| `SequenceChildSchedule` | `SequenceChildSchedule` | `PORTED_COMPLETE` | Selection ouverte a gauche/fermee a droite, ordre disque, refs DATA relatives et rewind; description partagee immutable et curseur runtime independant. |
| Arbre runtime et execution | `SequenceProgram`, `SequenceRuntime` | `PORTED_PARTIAL` | DAG descriptif borne puis foret mutable a ownership unique; lifecycle, loops, stop/hold/seek, cycles/profondeur/budgets et snapshots testes. Grouping/indirect/tweeker, feuilles mesh 3D et cameras 3D sont executes. Les cameras exposent matrice monde/FOV/near/far et un ownership de label source-compatible; les meshes exposent `contentsDataId`, world transform et `meshChoice`. Les queries immuables `GetInfo` (clock/endTime/matrice 3D optionnelle) et `GetChildMeshWorldMatrix` reproduisent le premier match et les bornes de sous-arbre du source. |
| Commandes `L_Seqncr` | `SequenceCommandQueue` | `PORTED_PARTIAL` | FIFO proprietaire 500 commandes, nesting Collect/Execute, Start, Stop, SetEndingAction, MoveTheWorks, MoveXY, MoveRySTxz, SetCamera et ForceRedraw, priorite 16 bits, doublons et recherche top-level/whole-tree portes. ForceRedraw marque tous les matches, force la reevaluation cible/ancetres et expose `needsRedraw` pour le frame courant avant remise a zero au cycle suivant. Aucun redraw visuel 2D fictif n est ajoute tant que ce renderer n existe pas. Les callers Monopoly actifs ne donnent aucun chain ID; chains restent differees. |
| Transformations / tweekers | `SequenceTransforms`, etat `SequenceRuntime` | `PORTED_PARTIAL` | Matrices row-vector 2D/3D, offset/matrix/OSRT, composition parentale, commandes Move*, identity/constant/linear et ordre tweeker-avant-position portes. `3D_MESH_CHOICE` interpole la proportion sans clamp; `CAMERA_FIELD_OF_VIEW` constant/linear modifie le FOV brut du parent camera avec defaut 3D pi/4. Restent son et callbacks; `scrollingWorld` demeure un contrat d horloge distinct et non simule. |
| MESHX runtime | `MeshXRuntime`, `MeshRuntimeCache` | `PORTED_PARTIAL` | Le chemin Monopoly `USE_OLD_FRAME` est reproduit pour le sous-ensemble HMD porte : inversion Y, normales de base /4096, deduplication, groupes materiau/texture, pose 0 de base puis poses MIMe `base + delta`, interpolation position/normale non clampee, UV de A et bounds interpoles. Le comportement historique ajoute les diff normals SVECTOR bruts apres conversion de la normale de base; ce contrat est teste. Autres primitives et donnees retail restent bloquees. |
| Render data de sequence | `MeshRenderData`, `SequenceRenderData` | `PORTED_PARTIAL` | Les feuilles mesh actives publient DataID/node/priority/clock/world matrix, `meshChoice`, asset partage et render data de pose evaluee. La topologie/index/texture reste celle de l asset; une pose invalide produit une erreur explicite. Les ressources SDL_GPU sont creees dans la couche GPU, pas ici. |
| Raccordement sequence -> render slots | `SequenceWorld3DSlot`, `World3DGPUScene` | `PORTED_PARTIAL` | Le slot historique 1 gere startup/moved/shutdown, bounds de la pose courante, culling et publication transactionnelle. Il resout SetCamera direct ou camera sequence labellisee; origine/+Z/+Y sont transformes puis forward/up normalises comme `L_Rend3D`. La scene GPU utilise buffers index/textures statiques et un vertex buffer dynamique par `SequenceNodeId` pour MIMe. Le culling moderne n est volontairement pas renvoye au sequenceur, conforme au `SequenceMoved()` 3D historique qui retourne toujours TRUE. |
| LANG core | `LanguageCatalog`, `LanguageService`, `ResourceRuntime` | `PORTED_PARTIAL` | Contrats existants conserves et raccordes au startup via les memes archives que DATA; index valide avant publication. Adaptateurs de rendu et migration des callers UI non raccordes. |
| Chaines/audio/dialogues LANG retail | aucun payload | `BLOCKED_MISSING_DATA` | Les neuf fichiers texte bruts sont vides et les `dat_ln/lm/lkNN.dat` sont absents; aucune chaine ne sera inventee. |
| Catalogue `TexInfo` | `TextureCatalog` | `PORTED_COMPLETE` | Huit atlas, recettes USA/Europe, overlays, provenances et tags HMD portes et testes; meshes/resolutions invalides rejetes par erreurs typees. |
| Corpus BMP `TexInfo` | `LegacyBitmap`, manifeste de 1 001 assets | `PORTED_COMPLETE` | 1 001/1 001 fichiers trouves : 497 en 128x128, 504 en 256x256, 881 en 8-bit, 120 en 24-bit, tous `BI_RGB`; offsets, tailles declarees, stride DWORD, raster complet et overflows sont verifies. |
| Loader BMP runtime | `LegacyAssets` / `SDL_LoadBMP` | `PORTED_PARTIAL` | Decodage et upload du fond existent; raccorder les substitutions `TextureCatalog` au resolver et aux slots mesh. |
| Manifestes DMake | `LegacyManifest`, `MonopolyManifestTool` | `PORTED_COMPLETE` | Les dix headers reels donnent 46 423 tags/noms/types contigus et peuvent etre exportes en TSV. |
| DMAKE99 et reconstruction bit-a-bit | aucun outil historique | `MISSING_TOOLING` | Sources/executable DMAKE99, fichiers `.df`, chemins source, payloads, tailles et offsets manquent. |
| Banques DAT retail exactes | absentes | `BLOCKED_MISSING_DATA` | `dat_main`, `dat_pat`, `dat_bord[e]`, `dat_brd2`, `dat_3d`, `dat_ln/lm/lkNN` introuvables dans l'arbre, `Source.zip` et l'ISO inspectes. |
| `2DVIEW01..39` externes | noms portes, fichiers absents | `BLOCKED_MISSING_DATA` | Ne pas confondre ces vues custom avec les vues 2D standard stockees en DAT/TAB. |
| HMD retail / objets MESHX | headers/tags seulement, fixtures HMD synthetiques | `BLOCKED_MISSING_DATA` | Le decodeur HMD partiel est teste depuis le contrat source, mais aucun mesh retail ne permet sa validation; aucun faux payload MESHX disque n'est cree. |

### Dettes de fidelite connues, non bloquantes pour le sequenceur

- Le decodeur LANG moderne valide les paires de substituts et le contenu apres
  NUL plus strictement que les manipulations historiques de code units UTF-16
  Windows. Cette difference reste a arbitrer avant de declarer LANG complet.
- `lookupIndexedDataId` represente l'absence par une erreur typee; le helper
  generique ne reproduit donc pas encore exactement le retour sentinelle
  historique `LED_EI == 0` de tous les callers. Ne pas masquer cette difference
  en `PORTED_COMPLETE` lorsqu'un caller dependra de la sentinelle.

`MonopolyManifestTool <output.tsv> <header-DMake>...` exporte uniquement les
informations effectivement reconstructibles : banque, tag decimal, type et
symbole. Il ne genere jamais silencieusement un DAT ou un payload fictif.

Une absence de DAT ne justifie jamais l'invention d'un chunk, d'une chaine,
d'un offset ou d'une texture. Les fixtures DAT/CNK/LANG presentes sous
`modern/tests` valident le format source et sont explicitement synthetiques.

### Configuration et duree de vie des ressources

Le jeu cherche desormais les banques sous `Dat_Mon/` dans le repertoire de
l'executable. `MONOPOLY_DATA_ROOT` peut designer explicitement une autre racine
absolue d'installation (celle qui contient `Dat_Mon/`). Une valeur relative ou
vide est une erreur de configuration; elle ne reutilise pas une ancienne
configuration et ne retombe pas sur une autre installation. Ce choix portable
remplace la recherche legacy INI/CD/cwd sans chemin propre a une machine.
L'API accepte un contexte edition/langue; le jeu conserve par defaut le contexte
historique USA/English US. Les choix Europe/French sont testes sur fixtures et
ne constituent pas encore un menu de selection utilisateur.

`ResourceRuntime` prepare cinq banques core puis les trois banques LANG dans
un nouveau registre. `LanguageService` utilise les memes instances. Le nouvel
ensemble n'est publie qu'apres validation des huit archives et de l'index LANG.
Une erreur preserve l'ancien snapshot du service. `Game` libere son snapshot
actif apres DISPLAY, souris et slots; un consommateur qui conserve un snapshot
peut encore charger des donnees non cachees. Cela evite le probleme signale par
le commentaire de `Source/monopoly/Main.cpp:450` sur un nettoyage LANG premature.
L'arret retire les expirations des timers de jeu deja en attente, en conservant
les autres evenements et leur ordre FIFO. Les tests de reprise utilisent les
vrais lecteurs DATA/LANG, timers et file UI; les consommateurs graphiques sont
des doubles de test et ne constituent pas une preuve de rendu GPU.

Le startup refuse maintenant aussi une erreur de banque core, dont le retour
etait ignore dans `Userifce.cpp`. C'est un renforcement explicite du diagnostic.
Les banques retail restant absentes, l'application ne peut pas demarrer une
partie complete; aucune fixture n'est installee ou chargee a leur place.
Le fond BMP de `LegacyAssets` conserve pour l'instant son chemin separe
`assets/legacy/BackGround.bmp`; son integration contextuelle reste a faire.

Validation requise pour chaque lot : configure CMake reussi, build reussi,
puis CTest. Un ancien executable de test ne constitue pas une validation.

### HMD et sequences : limites des contrats raccordes

`LegacyMeshData` conserve les octets HMD immuables. Il borne la table des
blocs, les headers, les chaines de primitives et les sections, detecte les
cycles et impose des budgets avant allocation. Les offsets sont des DWORDs de
quatre octets, jamais des pointeurs natifs. Pour Monopoly, les defines
`USE_OLD_FRAME` dans les callers jeu/ArtLib rendent `hmdload.cpp`/`meshx`
autoritatif; `NewMesh.cpp` est un chemin alternatif non compile par ces callers.
Le port suit donc le parcours tail-first de `HMD_MapUnit`, l'ordre append des
diff blocks et les contrats de `HMD_interpolate`, plutot que les typos visibles
dans `NewMesh.cpp`.

`triangle()` valide le sous-ensemble categorie 0 actuellement consomme. Les
images `GsUIMG1` 8-bit + CLUT sont converties en RGBA8 sans GDI. `mimePoses()`
decode `GsVtxMIMe` et `GsNrmMIMe` avec offsets relatifs bornes, budgets de
blocs/SVECTOR et appariement ordinal. La pose 0 reste le mesh de base; les poses
1..N appliquent les deltas. Reset/joint MIMe et autres categories restent
explicites/non portees. Une simple reussite de `parse()` ne pretend toujours pas
valider tous les payloads HMD retail.

`LegacySequence` decode maintenant les huit parties fixes effectivement utiles
a cette etape, dont le record tweeker de 13 octets. Il decode aussi les attributs
prives dimensionality, offset, matrix et OSRT 2D/3D sans copier dans une
structure native. Les octets/records et attributs restent immutables; ils ne
sont jamais utilises comme etat runtime mutable.

`SequenceClock` possede l'etat temporel. Le contrat est extrait de
`L_Seqncr.cpp:3756-3835,6443-6703,7326-7337` : premiere mise a jour immediate,
rattrapage initial des enfants seulement si dropFrames, cadence, pause/reprise,
stop/suicide, maintien a la fin avec cadence 255 activee dans `C_ArtLib.h`,
boucle naturelle a zero (pas modulo du depassement), et seek explicite. EndTime
zero conserve la sentinelle historique 1234567890. Les commandes de remplacement
non nulles de cadence/action sont appliquees avant validation. Une cadence finale
nulle, une action inconnue, une duree negative ou un depassement arithmetique
produit une erreur explicite. Le decoder conserve les champs disque bruts.
Les horloges son/video et la visibilite des mondes defilants ne sont pas simulees.

`SequenceChildSchedule` porte la selection de `AddNewlyBornChildren`
(`L_Seqncr.cpp:5230-5330`) : intervalle ouvert a gauche, ferme a droite,
premier enfant futur bloquant le scan, enfants Stop deja termines ignores.
L'ordre disque n'est jamais trie dans le calendrier. Le loader DATA respecte les
bornes du parent direct et le remplacement par une liste CNK indirecte; le tag
relatif zero est remappe, tandis que l'ID absolu zero signifie aucun enfant. La
description partagee conserve sa lease et chaque runtime possede son propre
curseur. Un type non decode est refuse, pas saute silencieusement.
Une reference indirecte vers le meme item est refusee : le source conserve
dans ce cas les bornes du parent puis echoue en tentant de revenir a zero
(`L_Seqncr.cpp:5269-5279`, `L_Chunk.cpp:2497-2511`). Le port preserve ce refus;
il n'invente pas une lecture recursive pour cette donnee invalide.

`SequenceProgram` construit ensuite un DAG descriptif immutable, avec budgets
de references/descriptions et profondeur configurable plafonnee a 128. Les
cycles indirects sont refuses par couple `(DataId, offset)`; les sous-descriptions
partagees ne sont pas amplifiees exponentiellement. Une construction depuis
`ResourceSnapshot` garde toutes les archives en vie apres remplacement du
snapshot publie. Ce chargement eager et borne remplace volontairement le parcours
paresseux par pointeurs du code Win32 afin que toute dependance invalide echoue
avant publication.

`SequenceRuntime` instancie une foret mutable a ownership unique sous la racine
historique implicite. Les enfants naissent a leur tick, sont mis a jour
recursivement et detruits enfant-avant-parent. Les loops detruisent les anciennes
instances, rembobinent le calendrier et creent de nouveaux IDs; stop, fin
naturelle, hold, pause/reprise et seek reconstruisent ou conservent les enfants
selon le contrat de l'horloge. Les siblings sont ordonnes par priorite croissante,
avec insertion avant l'ancien en cas d'egalite comme `InsertRuntimeChild`.
Aucun handle n'est reutilise et une erreur de construction/mise a jour vide
explicitement la foret plutot que publier un etat partiel trompeur.

`SequenceCommandQueue` reproduit la FIFO owner-thread de 500 entrees et le niveau
Collect/Execute, y compris un Execute surnumeraire negatif. Start cree toujours
une nouvelle instance; Stop et SetEndingAction ciblent le couple DataId/priorite
16 bits, top-level ou arbre entier, avec offset de donnees nul. L'audit des
callers Monopoly ne montre aucun chain ID actif; les listes waiting/dechained ne
sont donc pas inventees. Les commandes MoveTheWorks/MoveXY/MoveRySTxz et SetCamera sont portees.
L ownership des labels camera suit `LE_SEQNCR_LabelArray`: le dernier demarre prend
le label et sa suppression ne restaure pas un overlap plus ancien. Les labels
generiques, callbacks et chains restent partiels. `GetInfo` et
`GetChildMeshWorldMatrix` couvrent maintenant les champs/parcours effectivement lus par Monopoly.
`ForceRedraw` est porte dans la FIFO et le runtime avec un redraw transitoire
source-compatible et propagation vers les ancetres pendant l update.

`SequenceTransforms` porte les conventions row-vector 2D/3D de `L_Matrix.cpp`,
les six attributs offset/matrix/OSRT et la composition local puis parent. Les
tweekers identity, constant et linear appliquent leur transformation avant le
calcul de position du parent (`L_Seqncr.cpp:5484-5672,5789-6275`). Le chunk
prive 139 `3D_MESH_CHOICE` est maintenant un etat non matriciel distinct :
constant/linear le mettent a jour, A/B restent ceux de la premiere cle et la
proportion seule interpole sans clamp; Identity n'efface pas cet etat.

Les feuilles 3D publient la pose MIMe evaluee jusqu'au slot World3D 1. La scene
SDL_GPU conserve indices/textures statiques et alloue un vertex buffer anime par
node, mis a jour par cycling puis prune avec le lifecycle. Un readback D3D12
prouve que le changement de pose deplace reellement les pixels rasterizes. Le record camera 7, le FOV 144 et SetCamera atteignent maintenant la camera
de projection utilisee par la frame; un label absent conserve la camera precedente.
Les effets son/callbacks et les objets sequence non encore consommes restent
explicitement partiels. La visibility 3D a ete auditee : le renderer original
renvoie toujours TRUE a `SequenceMoved`, donc aucun feedback de culling n est ajoute.

### Derniere validation locale

Le 2026-09-06, avec MSVC 19.51 / toolset 14.51, CMake/Ninja, SDL 3.4.14 et
zlib 1.3.2 :

- reconstruction **clean-first complete** de `modern/build-phase-c`, code de
  sortie zero, incluant `MonopolyModern.exe`, `MonopolyDataCore` et
  `MonopolyGPU3DCore` ;
- suite complete depuis ce build neuf : **37/37 suites passees**, zero echec ;
- trace conservee dans le build ignore sous
  `modern/build-phase-c/ctest-full-20260906-pieces-camera.log` ;
- les suites `MeshGPUResources`, `World3DGPUScene` et `World3DRenderer` utilisent
  un vrai device SDL_GPU **Direct3D 12**. Les readbacks prouvent le triangle
  indexe, l'echantillonnage d'une texture HMD RGBA8 et le deplacement visible
  des pixels apres mise a jour d'un vertex buffer MIMe par node ;
- un ancien faux negatif incremental a confirme le risque d'objets MSVC/Ninja
  perimes apres changement d'une structure publique; la validation de ce jalon
  repose donc sur la reconstruction clean-first ci-dessus, pas sur ces objets ;
- deux branches ResourcePaths dependent de l'hote restent `[SKIP]` sur ce
  Windows (collision de casse et creation de symlink sans droit). Elles ne sont
  pas presentees comme validees.

Les regressions reproduites puis corrigees restent verrouillees par tests :
chemins `fichier/.`, expirations de timers 0/1 restant dans la file UI, reference
sequence indirecte vers son propre item et fixture tweeker dont le flag
drop-frames etait initialement incoherent avec son attente. Les tests DATA/LANG
couvrent aussi banques manquantes/tronquees, index invalide, rollback et duree
de vie des snapshots.

Preuve historique distincte (2026-08-24, MSVC 19.50) : build autonome sans
arbre `Source`, 16/16 suites, dix manifestes (46 423 entrees) et 1 001 BMP.
Ni Linux ni macOS n'ont ete compiles pour le jalon actuel. Vulkan/Metal ne sont
pas valides ici; le SPIR-V perime n'est plus distribue comme s'il correspondait
au shader texture actuel. Les DAT/HMD retail exacts restent absents, donc les
fixtures synthetiques prouvent les contrats source mais pas un contenu retail
indisponible.

## Prochaines priorites

1. Utiliser `GetInfo`, `GetChildMeshWorldMatrix` et `ForceRedraw` lors du port
   des chemins `UDPieces`/`UDIBar`/`UDAuct`, sans inventer de renderer 2D avant
   le port de ses objets/sprites.
2. Raccorder progressivement les transitions camera des ecrans puis poursuivre
   les consommateurs UI qui reposent deja sur le sequenceur moderne.
3. Auditer les autres types HMD effectivement atteignables sous `USE_OLD_FRAME`;
   ne porter qu un type prouve par caller/source et laisser reset/joint MIMe ou
   primitives non consommees explicitement non supportes.
4. Raccorder la voie **externe** `TextureCatalog -> ResourcePaths/BMP -> mesh`
   pour les substitutions ville/langue/plateau/devise; ne pas dupliquer la voie
   texture HMD embarquee `GsUIMG1 -> SDL_GPUTexture` deja fonctionnelle.
5. Reprendre la Phase D en priorisant `UDBoard -> UDIBar -> UDPsel -> UDPieces ->
   UDAuct -> UDTrade -> UDOpts`, et reporter AI, audio/voice, video et fonts tant
   qu ils ne debloquent pas un chemin plus prioritaire.
