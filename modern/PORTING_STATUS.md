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
| `Source/monopoly/Main.cpp` | `Application`, `Engine`, `Game`, `RenderSlots`, `Timers`, `GPUFrame` | `PORTED_PARTIAL` | `GameInitialise`, `InitRenderSlots`, `ProcessUIMessage`, `GameShutdown`, `GameUpdateCycle` | SDL3, SDL_GPU, DISPLAY, UI messages | Lifecycle DATA/LANG raccorde et teste avec vrais archives/timers/file UI et consommateurs doubles : rollback, reprise, arret des timers et retrait de leurs expirations en attente. Les slots ne pilotent pas encore un vrai renderer 2D/3D; lifecycle GPU reel encore a valider. |
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
| `Source/monopoly/UDPieces.cpp` | geometrie de plateau partielle seulement | `NOT_STARTED` | pieces, positions, animations, cameras | PC3D moderne, BoardGeometry, RULE | Necessite mesh/camera/scene SDL_GPU. |
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
| `Source/artlib/L_Main.*` | `Application`, `Engine`, `Game` | `REPLACED_PORTABLE` | boucle evenementielle, init/shutdown | SDL3 | Fenetre redimensionnable/HiDPI, presentation, erreurs de startup et formats GPU Vulkan/D3D12/Metal sont raccordes; test de lifecycle GPU et smoke test interactif restent requis. |
| `Source/artlib/L_Timers.*` | `Timers` + `UIMessages` | `REPLACED_PORTABLE` | horloge 60 Hz, 4 timers, speed/restart, evenement index+tick | steady_clock, file UI | Contrat actuellement raccorde teste de facon deterministe; les appels historiques `LE_TIMER_Delay` identifies dans `Main`, `UDUtils` et les diagnostics DISPLAY appartiennent a des chemins remplaces, inactifs ou encore differes. |
| `Source/artlib/L_UIMsg.*` | `UIMessages` | `PORTED_PARTIAL` | FIFO 100, evenements timer/input, delestage | Application, Timers | Ajouter davantage de tests de pression/coalescence et les types requis par les futurs modules. |
| `Source/artlib/L_Data.*` | `DataBanks`, `LegacyDataArchive`, `DataBankRegistry`, `LegacyDataArchiveBuilder` | `PORTED_PARTIAL` | DataId 16:16, groupes, header/index DAT, zlib, chargement paresseux, refs | zlib, filesystem | Lecture LE explicite, validation, metadata, cache, leases partagees, mount/unmount, index packed et writer de fixtures sont testes. Restent les sources runtime/user-created/external-file, le budget/LRU automatique et une validation sur banque retail. |
| `Source/artlib/L_Chunk.*` | `LegacyChunkReader`, `openLegacyChunkReader` | `PORTED_COMPLETE` | lecteur consomme : header 24-bit + ID 8-bit, descend/ascend/seek/map/read, limite 8 niveaux | `LegacyDataArchive`, futur `L_Seqncr` | Contrat read-only et ownership `ReadFromDataID` portes sans bitfield ABI et testes sur fixtures, y compris le franchissement historique des siblings ID 0/128 lors d'une recherche precise. La validation d'un CNK retail reste bloquee separement; l'editeur/writer non consomme n'est pas dans ce perimetre. |
| `Source/artlib/L_Grafix.*`, `L_Rend2D.*`, `L_Sprite.*` | `Display`, futur renderer UI SDL_GPU | `PORTED_PARTIAL` | composition 2D, clipping, priorites, surfaces | SDL_GPU, assets, transformation 800x600 | Seul le cycle d'etat existe; rendu des objets/sprites/fonts a construire. |
| `Source/artlib/L_Rend3D.*` | `GPUFrame` et futur renderer 3D | `PORTED_PARTIAL` | viewport et fond 3D | SDL_GPU, PC3D moderne | Fond BMP seulement; scene, camera, mesh, materiaux et animations absents. |
| `Source/artlib/L_Seqncr.*` | `LegacySequence`, `SequenceClock`, `SequenceChildSchedule` | `PORTED_PARTIAL` | records, horloges et selection temporelle des enfants | `LegacyChunkReader`, DATA, futurs objets/slots | Six records decodes; horloge CPU, pause/reprise, actions de fin et selection des enfants directs/indirects testees. Aucun executeur recursif, commandes/chains/watches, transformation, tweeker, streaming audio/video ni raccordement aux slots de rendu. |
| `Source/artlib/L_Fonts.*`, `L_Print.*` | Aucun | `NOT_STARTED` | Arial 10, mesure/rendu de texte | font rasterizer portable, LANG | Choisir un backend portable et conserver metriques/layout observables. |
| `Source/artlib/L_Keybrd.*`, `L_Mouse.*` | traduction SDL dans `Application`, `MousePointer` partiel | `REPLACED_PORTABLE` | input clavier/souris | SDL3, `LogicalViewport` | Souris reconvertie vers 800x600 et bandes noires rejetees; rendu du pointeur et certains types d'evenements restent partiels. |
| `Source/artlib/L_Sound.*`, `L_Midi.*` | Aucun | `NOT_STARTED` | WAV, musique, mixage, voice | audio portable, data | Inventorier les appels Monopoly et remplacer DirectSound/MIDI legacy. |
| `Source/artlib/L_Video.*` | Aucun | `NOT_STARTED` | opening movies | decoder/service video portable | Identifier fichiers et timing avant choix technique; ne pas porter VFW/Bink litteralement. |

## PC3D consomme

| Original | Equivalent moderne | Statut | Contrat consomme | Dependances | Travail restant / blocage |
|---|---|---|---|---|---|
| cameras, viewports, background (`camera.*`, `D3DDevice.*`, view code) | `GPUFrame`, `Display::Viewport3D`, `LogicalViewport` | `PORTED_PARTIAL` | rectangles Main/Status/Trade, fond et clear | SDL_GPU | Rectangles mis a l'echelle dans le viewport letterbox; cameras, scene et projection 3D reelles restent a porter. |
| meshes/scenes/materials (`mesh*`, `NewMesh*`, `Scene.h`, `l_material.h`) | Aucun | `NOT_STARTED` | plateau et pieces effectivement references | HMD/MESHX, textures, SDL_GPU | Faire l'inventaire des services appeles; ne pas porter PC3D entier. |
| decodeur HMD / postload MESHX (`HMDData.h`, `NewMesh.cpp`, `hmdload.*`) | `LegacyMeshData`, `openLegacyMeshData` | `PORTED_PARTIAL` | HMD disque LE, offsets DWORD, primitives, triangles | `DAT_3D`, futur mesh CPU/GPU | Structure bornee, cycles/budgets et triangles categorie 0 avec normales testes. Sections image/TIM, MIMe, animations, autres polygones et conversion en mesh runtime restent absentes. MESHX est le resultat en memoire du postload HMD, pas un second format disque. |
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
| Parseurs semantiques CNK / sequence | `LegacySequence` | `PORTED_PARTIAL` | En-tete 12 octets, temps signes 24 bits, flags et records grouping/indirect/bitmap/model/sound/mesh decodes sans bitfield ABI. Lecteur inchange en erreur, references relatives au DAT contenant. Video/camera/preloader/tweeker refuses; attributs et sous-chunks non interpretes par ce decodeur. |
| Etat et timing des sequences | `SequenceClock`, `SequenceChildSchedule` | `PORTED_PARTIAL` | Etats CPU proprietaires, choix des enfants dans l'ordre disque, leases des CNK directs/indirects, cadence, pause/reprise et actions de fin testes. Pas de creation/destruction effective d'arbre, de commandes chainees, d'animation ni de rendu. |
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
cycles et impose des budgets de records avant allocation. Les offsets sont
des DWORDs de quatre octets, pas des pointeurs natifs. La version est conservee
sans inventer une constante que les loaders historiques ne verifient pas.
`triangle()` valide ensuite les references de la categorie 0 prise en charge
(couleurs, UV, indices et SVECTOR signes); une simple reussite de `parse()` ne
valide pas tous les payloads propres aux categories. Les flags process/scan
restent disponibles mais ne sont pas executes automatiquement. Le futur
constructeur de mesh devra aussi respecter le parcours des primitives de fin
de chaine vers le debut de `NewMesh::ProcessHMDPrimitive`, et non confondre
l'ordre d'inspection avec l'ordre de construction.

`LegacySequence` decode six parties fixes, pas des animations jouables.
`SequenceClock` consomme ces records et possede l'etat temporel des types
grouping/indirect/bitmap/model/mesh non defilants. Le contrat est extrait de
`L_Seqncr.cpp:3756-3835,6443-6703,7326-7337` : premiere mise a jour immediate,
rattrapage initial des enfants seulement si dropFrames, cadence, pause/reprise,
stop/suicide, maintien a la fin avec cadence 255 activee dans `C_ArtLib.h`,
boucle naturelle a zero (pas modulo du depassement). EndTime zero conserve le
sentinel historique 1234567890. Les commandes de remplacement non nulles de
cadence/action sont appliquees avant validation. Une cadence finale nulle,
une action inconnue, une duree negative ou un depassement arithmetique produit
une erreur explicite. Le decoder, lui, conserve les champs disque bruts.
Les horloges son/video et la visibilite des mondes defilants ne sont pas
simulees. Un recul du temps parent exige le futur rembobinage de l'arbre.

`SequenceChildSchedule` porte la selection de `AddNewlyBornChildren`
(`L_Seqncr.cpp:5230-5330`) : intervalle ouvert a gauche, ferme a droite,
premier enfant futur bloquant le scan, enfants Stop deja termines ignores.
L'ordre disque n'est jamais trie. Le loader DATA respecte les bornes du parent
direct et le remplacement par une liste CNK indirecte; le tag relatif zero
est remappe, tandis que l'ID absolu zero signifie aucun enfant. La lease permet
de relire les records/attributs apres fermeture de la banque. Les attributs
non-sequences ne participent pas au calendrier mais restent dans les octets
proprietaires; cela ne porte pas leur comportement. Un type de sequence non
decode est refuse, pas saute silencieusement. Le chargement ne developpe
qu'un niveau, sans expansion recursive des references indirectes.
Une reference indirecte vers le meme item est refusee : le source conserve
dans ce cas les bornes du parent puis echoue en tentant de revenir a zero
(`L_Seqncr.cpp:5269-5279`, `L_Chunk.cpp:2497-2511`). Le port preserve ce refus;
il n'invente pas une lecture recursive pour cette donnee invalide.

Les tests raccordent record -> horloge -> selection -> rewind/reselection.
Le caller doit encore instancier/detruire les enfants, appliquer les
transformations/tweekers et transmettre leurs objets aux render slots.
Cette separation ne constitue donc pas encore un executeur complet integre
au cycle du jeu, ni une preuve d'animation visible.

### Derniere validation locale

Le 2026-09-05, avec MSVC **19.51.36256.0** (toolset 14.51.36231),
CMake **4.3.1-msvc1** / Ninja, SDL **3.4.14** et zlib **1.3.2** :

- ancien compilateur 14.50 retire par la mise a jour Visual Studio : cache
  CMake reconfigure avec le toolset reel, puis reconstruction complete de
  `all` dans **le meme** `modern/build-phase-c`, dependances preservees ;
- configurations et builds `all` incrementaux reussis pour les ajouts suivants,
  y compris `MonopolyModern`, `MonopolyManifestTool`, `MonopolyDataCore` ;
- tests cibles apres chaque correction; suite complete finale : **24/24 suites
  passees**, zero echec,
  dont les **16 initiales conservees** et huit ajouts : ResourcePaths,
  ResourceRuntime, ResourceLifecycle, ResourceConfiguration, LegacySequence,
  LegacyMeshData, SequenceClock, SequenceChildSchedule ;
- deux branches ResourcePaths dependent de l'hote : collision de casse
  marquee `[SKIP]` sur ce filesystem Windows insensible a la casse, et boucle
  de symlink marquee `[SKIP]` faute de droits de creation. Le reste de la suite
  execute les fixtures ; ne pas presenter ces deux branches comme validees.

Les regressions reproduites puis corrigees incluent les chemins `fichier/.`
et les expirations de timers 0/1 restant dans la file UI apres arret. Leurs
tests conservent aussi les evenements clavier et les autres timers. Les tests
verrouillent egalement le refus historique d'une reference indirecte vers son
propre item : le test a echoue sur le premier raccordement, puis passe apres
correction. Les tests
LANG/DATA couvrent les huit banques manquantes ou tronquees, l'index invalide,
le rollback et les lectures non cachees apres arret/remplacement. La
configuration des chemins est egalement testee contre SDL reel, sans GPU.

Preuve historique distincte (2026-08-24, MSVC 19.50) : build autonome sans
arbre `Source` et 16/16 suites; audits de dix manifestes (46 423 entrees),
1 001 BMP, convertisseur TSV 46 424 lignes. La copie autonome de ce lot ancien
n'a pas ete reconstruite pour le lot actuel. Ni Linux ni macOS n'ont ete
compiles ici. Les tests actuels ne prouvent ni compatibilite avec les DAT/HMD
retail absents, ni parcours interactif, ni rendu sur GPU reel.

## Prochaines priorites

1. Construire l'arbre runtime de sequences autour des records, leases,
   horloges et calendriers existants : naissance/destruction/rebouclage des
   enfants, limites d'expansion indirecte, commandes, transformations et
   tweekers. Ne pas creer un second moteur de temps ni pretendre executer les
   types encore refuses. Raccorder ensuite ce noyau au cycle du jeu.
2. Completer les donnees HMD consommees par `NewMesh` (image/TIM et MIMe), puis
   les objets mesh CPU et les transformations/animations determinees par le
   source. Ne pas creer de format MESHX disque ni de geometrie retail fictive.
3. Raccorder `TextureCatalog` au resolver BMP, aux textures/meshes puis aux
   slots de rendu 3D; conserver la composition Europe sur CPU avant upload.
4. Ajouter les adaptateurs LANG de mesure/impression et migrer les callers vers
   des snapshots proprietaires, sans inventer les textes retail absents.
5. Seulement apres cette stabilisation DATA, reprendre la Phase D dans l'ordre
   `UDBoard -> UDIBar -> UDPsel -> UDPieces -> UDAuct -> UDTrade -> UDOpts`.
