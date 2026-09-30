# État et plan du portage {#porting_status}

## Objectif et périmètre

Reproduire les comportements actifs du Monopoly d’origine dans le runtime C++23/SDL3/SDL_GPU de `modern/`. `Source/` reste la référence en lecture seule. Le mot **legacy** signifie « code d’origine » : une fonctionnalité encore utilisée reste à porter. Seuls les chemins désactivés, retirés ou sans usage actif démontré peuvent être exclus.

Le portage est fonctionnel sur des contrats testés, mais **sa fidélité complète au jeu retail n’est pas établie**. Aucun pourcentage fonctionnel n’est actuellement justifié. Les 79 entrées sont conservées dans l’[inventaire détaillé](PORTING_MATRIX.md), avec les écarts connus séparés des comparaisons restant à mener.

## Ce qui est implémenté

- Initialisation, boucle de jeu, timers, slots, règles, IA, messages locaux, archives et restauration de partie.
- Surfaces UI : enchères, plateau, pièces, sélection locale, Trade/Future/Immunity, IBar et Banque maisons/hôtels, chat avec texte et défilement, statistiques et deed floater, options Load/Save, Credits et QuickHelp.
- **GIS-8** : capture SDL3, PCM 11025 Hz/8-bit/mono, GSM 6.10 WAV49, DAT1/DATN, playback et transport TCP de voix. Les sessions de jeu C01 ont leur admission, ownership, routage et retour local ; le scénario intégré TCP est validé localement au code `cf1ba45`, voir [sessions TCP](NETWORK_GAME.md). La qualification entre machines et du matériel reste ouverte.
- **GIS-9** : TextureCatalog → ResourcePaths/BMP → mesh raccordé. **GIS-10** : types HMD consommés portés. La suite porte sur les données et scénarios réels, pas sur la recréation de types désactivés.
- Lecture DAT/CNK/LANG, ressources, séquences, fonts, rendu GPU, vidéo avec frames/PCM et cycle de vie, films d’ouverture. Les consommateurs DATA passent désormais par une interface logique `DataSource` : les DAT sont le backend de repli, avec surcharges par `DataId` et manifestes de payloads hors archive. Les codecs vidéo utilisent FFmpeg/FFprobe externes ; voir [vidéo](VIDEO_RUNTIME.md).
- Présentation moderne : plein écran bureau par défaut, modes fenêtré/exclusif, 1080p/1440p/4K selon le mode d’affichage, VSync/Mailbox/Immediate, deux frames GPU en vol, F11, télémétrie résolution/FPS et canevas 3D 800x450 16:9 indépendant de l’UI 800x600. Voir [présentation](PRESENTATION.md).
- Assets Blender/glTF : six pions récupérés et cinq sculptures nouvelles exportés séparément, staging optionnel, loader statique borné et fallback HMD selon la racine CNK et sa priorité d'activation. Le chemin PBR comprend les cinq cartes, samplers, mipmaps, bases tangentes et alpha OPAQUE/MASK. Les racines rigides qualifiées conservent le timing CNK ; les autres changements de forme restent retail. Plateau, maison et trois éléments de décor ont des adaptateurs optionnels. Les animations complètes et la qualification visuelle en partie restent ouvertes. Voir [contrat Blender/glTF](BLENDER_INTEGRATION_SPEC.md).

« Implémenté » décrit la présence du contrat moderne ; les comparaisons sémantiques et qualifications encore ouvertes sont détaillées ci-dessous. Le panneau Future/Immunity est terminé et n’est plus une tâche restante.

## Code à terminer

Aucun manque de code confirmé n’est actuellement listé ici après implémentation de C03. Les comparaisons A01–A07 restent ouvertes et peuvent révéler d’autres écarts.

QuickHelp n’est plus bloqué par les accents hors Windows : son décodage CP1252 vers UTF-8 est explicite et testé. Les fichiers fournis sont compatibles avec ce choix ; leur contenu ne permet pas de distinguer CP1252 de Latin-1 pour les octets qu’ils emploient.

Un écart actif connu est un comportement utilisé dans la version d’origine dont une différence ou une absence moderne a été identifiée. C03 est désormais implémenté et reste à qualifier visuellement ; D01 dépend de données Europe absentes. Les lignes UDStats et UDPenny de la matrice partagent C03 : elles ne représentent pas deux travaux distincts. Les comparaisons A et qualifications Q ci-dessous ne sont pas, à elles seules, des défauts démontrés.

## Comparaisons encore nécessaires

Ces points remplacent les anciennes mentions vagues « partiel » ou « futur ». Ils demandent une comparaison ciblée ; ils ne constituent pas tous des fonctionnalités manquantes. Pour chaque divergence : citer l’appelant ou l’asset, reproduire le comportement, corriger et tester. Une exclusion demande une preuve source.

| ID | Zone | Vérification attendue |
|---|---|---|
| A01 | RULE / UserInterface | Branches internes, transitions et scénarios complets de règles, au-delà des fixtures existantes. |
| A02 | IA / trade | Décisions, évaluations et transitions des échanges. `Trade_SendItems` est déjà raccordé. |
| A03 | UI / audio / Penny | Notifications, animations, transitions et surfaces résiduelles effectivement consommées ; ne pas rouvrir les propriétaires de texte déjà portés. |
| A04 | LANG / FONTS / GRAFIX | 96 DPI d’origine raccordés ; formatage, UTF-16, métriques et clipping encore à comparer. |
| A05 | DATA | Contrats mémoire/LRU et sentinelles exigés par les appelants. Le LRU des blocs DAT et la libération des bitmaps/meshes CPU et GPU sont raccordés ; les consommateurs runtime ne dépendent plus directement du registre DAT et peuvent recevoir des payloads logiques hors archive avec fallback retail. Les scènes actives et substitutions de textures restent possédées. Le décodage des formats modernes natifs et le retrait complet des banques retail restent à poursuivre. |
| A06 | Séquenceur | Préchargement, attributs après enfants, sélection et persistance des tweekers, événements de fin et callback souris consommé sont raccordés. Labels et autres usages C++ ou DAT restent à comparer. Model est le type 4, Preloader le type 8 ; le renderer source actif n’accepte pas Model. L’absence d’appel C++ seule n’exclut pas un usage par données. |
| A07 | PC3D | Backend GLB, calibration, fallback par contexte CNK, cinq cartes PBR et alpha OPAQUE/MASK raccordés et couverts par contrôles CPU/GPU ciblés. Les adaptateurs optionnels plateau/maison/décor passent un rendu SDL_GPU séparé. L'audit des pions confirme des changements de HMD, et non des poses MIMe : les variantes doivent couvrir une racine entière. Silhouettes animées, matériaux procéduraux restants et qualification visuelle d'une partie restent ouverts. |

MIDI est désactivé par `CE_ARTLIB_EnableSystemMidi=0`. Les cas HMD reset/joint/UIMG0/ground/envmap sont commentés dans `hmdload.cpp`. Le chemin `NewMesh` alternatif n’est pas celui sélectionné par les appelants actifs. Ces éléments ne sont pas des tâches actives sans nouvelle preuve contraire. Les sept modules exclus et leurs justifications figurent dans la matrice.

## Code implémenté : validations restantes

| ID | Code présent | Validation attendue |
|---|---|---|
| C01 | Code des sessions de jeu TCP raccordé : menu/CLI, admission, ownership, actions, notifications privées, déconnexion et retour local. | Scénario intégré sur deux connexions TCP, dans un même processus, validé au code `cf1ba45`. Qualification Q02 encore ouverte. Voir [usage et portée](NETWORK_GAME.md). |
| C02 | FullHelp portable raccordé : conversion HLP → HTML asynchrone puis ouverture navigateur. | Exporteur externe `winhlp` requis ; conversion réelle, sujets/images et plateformes à qualifier. Voir [aide complète](FULL_HELP.md). |
| C03 | Génération des 28 rectos/28 versos Europe et catalogue runtime raccordés aux enchères, IBar, Trade et Stats ; variantes de langue, plateau, devise et règle maisons/hôtel. | Qualification visuelle avec les modèles DAT retail en Q03 ; génération et remplacement du catalogue couverts par tests ciblés, résultat consigné ci-dessous. |

## Données manquantes

| ID | Données | Périmètre et condition de résolution |
|---|---|---|
| D01 | Six références Europe d’historique absentes du corpus livré ; aucune valeur inventée. | Les appels sont dans les branches Europe de `UDIBar.cpp`, désactivées par `USA_VERSION=1` dans le build source livré. Les définitions/données Europe restent nécessaires pour cette édition ; l’achat utilise déjà LANG 3178. |
| D02 | Banques Europe/français absentes de l'installation locale : `dat_borde.dat`, `dat_ln03.dat`, `dat_lm03.dat`, `dat_lk03.dat`. | La sélection explicite Europe/français est raccordée et signale ces absences ; le démarrage français réel et le plateau Paris dans une partie restent bloqués jusqu'à fourniture des payloads. Les en-têtes ne remplacent pas les banques. |

## Qualification sur données et plateformes réelles

| ID | Qualification | Limite actuelle |
|---|---|---|
| Q01 | Parties jouables avec DAT/LANG/CNK retail, règles, IA, UI et langues. | Les banques USA locales permettent des démarrages bornés ; neuf chargements d'idles modernes ne prouvent pas une partie. Le démarrage Europe/français est bloqué par D02. |
| Q02 | Voix entre deux processus puis deux machines, capture et écoute physiques. | Les tests de transport/codec ne prouvent pas le parcours utilisateur ni le matériel. Voir [réseau et voix](NETWORK_VOICE.md). |
| Q03 | Textures/UV/HMD, éditions, devises et scénarios Board Editor personnalisés. | Géométrie HMD locale décodée et contrôles PBR disponibles ; la comparaison visuelle des éditions/devises et des vues personnalisées reste à qualifier. Le plateau standard ne dépend pas de ces vues externes. |
| Q04 | Films Indeo/Bink réels et synchronisation audiovisuelle, installation FFmpeg. | Runtime et tests disponibles ; médias retail absents. |
| Q05 | Linux/macOS, POSIX, Vulkan/Metal et disponibilité de FullHelp. | La validation de référence est Windows/D3D12, pas une qualification de toutes les plateformes. |

La reproduction binaire exacte de DMAKE99 est un outil de reconstruction optionnel. Le writer DAT moderne accepte des payloads fournis ; aucun outil ne peut recréer les assets absents à partir des seuls manifestes.

## Lot de code suivant la référence

Sessions réseau C01, aide C02, attente des actions IA avant trade (A02), tailles de fonts normalisées à 96 DPI (A04), cache DAT LRU global (A05) et préchargement des ressources de séquence (A06) sont implémentés dans les commits `ce78e39` à `ccf5f28`. Les autres comparaisons A01–A07 restent ouvertes : ces corrections ciblées ne prouvent pas leur clôture exhaustive.

Les onze branches `assistant/*` sont intégrées (dix têtes distinctes). Elles ajoutent la durée WAV, le backend de surfaces GRAFIX partagé, le blending des ombres et des contrats de tests. Leurs conclusions utiles sont reprises dans la matrice ; les anciennes réserves déjà résolues ne sont pas réintroduites. La génération des actes Europe C03 est implémentée dans le lot suivant, avec le survol Trade et le remplacement transactionnel des 56 surfaces.

**Compilations et tests autorisés via GitHub Actions.** Les premiers commits de ce lot avaient été publiés sans validation, avec `[skip ci]`. Cette restriction est levée ; la référence locale ci-dessous comprend désormais leurs modifications. Pour chaque résultat CI, vérifier le SHA testé : un succès sur un commit antérieur ne qualifie pas les changements suivants.

Les commits `ee9621d`, `57321a8` et `cf1ba45` corrigent ensuite les parcours de tours humains et IA, l’annulation des phases, les séquences sonores, les contre-offres, la sortie du plateau depuis la prison, les commandes SDL, les noms Unicode et la conservation des propriétaires réseau lors du choix de l’ordre. Le démarrage expose aussi les ressources manquantes et accepte un répertoire explicite. Les tests intégrés passent par RULE, FIFO, projection UI et commandes IBar ; deux parties IA successives atteignent leur fin. Les présentations graphiques de ces scénarios sont simulées : cette preuve ne remplace pas une partie avec les ressources retail.

Les commits `a697c17`, `cc251ce` et `0646b5e` corrigent le rendu Gouraud et ses uniformes partagés avec la 2D, la lecture des cartes après leur apparition, la répétition de la voix de victoire, les événements de fin, les attributs et effets persistants des séquences. Les caches libèrent les bitmaps et modèles arrêtés, y compris les animations partagées quand la vue 3D est masquée ; les textures personnalisées survivent à la reconstruction du modèle. Les refus de phase/joueur renvoient désormais l’erreur ciblée puis republient la décision attendue, avec l’exception des commandes internes de la banque. Douze commandes tardives sont exercées à travers RULE, FIFO et IBar ; les dettes collectives et faillites en chaîne de cartes passent aussi par ces interfaces. La compilation et les tests restent distincts de la qualification retail Q01–Q05.

Le commit `7aab3c0` rétablit la décision IBar après les échanges pendant le tour d’un joueur autre que le premier : les scénarios acceptés et refusés terminent réellement le tour par clic. Le rendu élimine les faces arrière, y compris pour les ombres. Un contrôle de l’API `IDirect3DDevice3` en 32 bits confirme le réglage initial `D3DCULL_CCW` des périphériques HAL et RGB ; les tests GPU vérifient les deux orientations sans modifier les indices HMD de production.

Les commits `10ca017`, `2b46b1d`, `18bbf2a` et `c1c5cdc` ouvrent ensuite la migration des ressources : interface `DataSource` indépendante du conteneur, fallback DAT par `LayeredDataSource`, catalogue LANG routé par la même source logique, puis chargement optionnel de payloads hors archive via `--data-overrides`. Les DAT restent le fallback requis tant que les formats natifs modernes ne couvrent pas tous les contrats.

Les commits `e956f30`, `bfefa14` et `bda7326` modernisent la présentation sans toucher aux règles : configuration plein écran/résolution/swapchain, mapping 3D natif 16:9 avec entrée souris correspondante, F11 et télémétrie FPS/résolution. Le canevas UI 800x600 reste isolé pour préserver les écrans retail pendant leur future modernisation.

## Validation de référence

Validation de référence : **140/140 suites CTest passées** — code `7aab3c0`, Windows/MSVC Debug, 27 septembre 2026.

Les commits DATA `10ca017..c1c5cdc`, la présentation `e956f30..bda7326`, l’audio GSM610 et le pont Blender/GLB ont été compilés localement jusqu’à `MonopolyModern.exe` sous Windows/MSVC Debug. Le probe décode les six GLB exportés ; le runtime active les cinq idles statiques sûrs disponibles (voiture, chapeau, bateau, bottine, dé à coudre) et conserve le chien retail pour son idle multi-HMD. Aucune nouvelle campagne CTest n’a été lancée pour ces lots ; la référence 140/140 reste donc `7aab3c0`.

Application compilée ; CTest global réussi en 8,25 s avec six exécutions parallèles. Deux cas internes ResourcePaths restent non exécutés (collision dépendant de la casse et permissions de liens symboliques). Aucun test CTest ni test GPU n’est converti en skip. Cette validation comprend les scénarios humains/IA et TCP, les refus d’actions, les cartes et faillites, les régressions de séquences et de caches, ainsi que les lectures de pixels GPU ; elle ne remplace pas Q01–Q05.

Les journaux locaux de cette référence sont `modern/build/trade-return-culling-final-build.log` et `modern/build/trade-return-culling-final-tests.log`. Les exécutions CI et leurs logs restent la preuve de validation distante ; la référence ci-dessus n’est pas une affirmation sur le dernier run GitHub.

## Suivi automatique et maintenance

- Ce fichier contient le plan courant et la référence de qualification ; [PORTING_MATRIX.md](PORTING_MATRIX.md) contient l’unique inventaire des statuts.
- GitHub Actions contrôle la structure CMake et les scripts, génère [PORTING_AUDIT.md](PORTING_AUDIT.md) et `porting-progress.svg`, puis compile et exécute CTest sous Windows. FFmpeg/FFprobe sont provisionnés et vérifiés pour les tests vidéo.
- La documentation Doxygen inclut le plan, la matrice et le rapport généré. Les changements de ces documents déclenchent leur publication.
- L’audit structurel n’est pas un audit de fidélité. Les compteurs d’inventaire ne sont pas des pourcentages d’achèvement : familles et sous-contrats se recouvrent.
- Ne modifier manuellement ni le SVG ni le rapport généré. Supprimer une tâche seulement après correction vérifiée ou exclusion justifiée ; conserver les décisions utiles dans la matrice, les anciens checkpoints dans l’historique Git.

## Modern assets — qualification locale du 1er octobre 2026

Le chargement statique GLB vérifie budgets, fichiers, références, accesseurs,
indices et transformations. Les PNG/JPEG embarqués et cinq cartes PBR sont
raccordés, avec rôles sRGB/linéaires, samplers, mipmaps, normales tangentes et
alpha OPAQUE/MASK. Les **54 contrôles CPU GLB et 86 contrôles GPU** passent.
BLEND, scènes glTF animées et accesseurs sparse restent explicitement refusés.
Un environnement studio HDR optionnel ajoute des réflexions spéculaires IBL :
cube RGBA16F 64x64, mipmaps GGX déterministes et approximation DFG analytique.
Les 30 nouveaux contrôles CPU/GPU passent, ainsi que les 86 contrôles de rendu
existants ; environnement absent/invalide, le cube noir désactivé conserve les
pixels précédents. Le haut-de-forme de production (CNK `0x80236`, tick 0,
priorité racine 224) a été capturé en 1920x1080 avec PBR et environnement actifs,
et ses réflexions ont été examinées. Les 23 captures CNK demandées passent avec
IBL/PBR actifs ; la galerie des 11 pions a été examinée. Un démarrage de
l'application reste vivant plus de 25 secondes avec neuf chargements de pions,
puis son processus dédié est arrêté. Ce smoke test et ces captures ne qualifient
ni partie interactive ni animation continue. Le diffus conserve
l'approximation ambiante, sans cube d'irradiance ni LUT BRDF.
Les caches distinguent les propriétaires immuables modernes/retail partageant
un DATA id. Le tree Git de `Source/` conserve le baseline verrouillé.
DXIL est exécuté sous Windows ; SPIR-V/MSL sont compilés et réfléchis, sans
qualification de rendu Linux/macOS.

La provenance de priorité est conservée : activation de racine idle à 224..229,
racine mouvement à 100, priorité de feuille de dessin issue du CNK, y compris
la valeur 0. Le resolver utilise le contexte de racine, sans réécrire l'ordre de
présentation de la feuille. Les 46 racines rigides ont été exercées par le
SequenceRuntime de production : géométrie moderne à chaque frame, sans échec,
horloges, choix HMD, matrices, tweekers et cycle de vie identiques au retail.
Le rapport local est `build/qualified-rigid-production-20261001.json`.
L'inventaire couvre les 1 089 CNK sur 600 ticks ; ses sorties CPU autonomes ne
remplacent pas les appels et médias d'une partie réelle.

Six pions sont récupérés ; les cinq absents sont des sculptures nouvelles.
Leur hauteur/pivot/orientation sont calibrés sur des HMD décodés. Neuf idles
statiques se chargent dans un démarrage borné ; cette mesure ne qualifie pas le
gameplay. Le pack de deux états du bateau pour la racine `0x80360` passe les
71 ticks de sa timeline de production, appariés au retail avec mêmes horloges,
choix HMD, matrices, priorités et cycle de vie. C'est une preuve CPU, pas un
rendu animé GPU. L'adaptateur des quatre états du chien passe 99 frames de
production sans fallback ni erreur, avec mêmes horloges, matrices, choix HMD et
cycle de vie que le retail. Le démarrage vivant de 25 secondes charge neuf idles
statiques et ne prouve pas l'animation du chien à l'écran. L'adaptateur des six
états du cheval est compilé : 99/99 frames modernes sans erreur, appariées au
retail avec mêmes horloges, matrices, choix HMD et cycle de vie. Les refus de
contexte, l'échec indépendant d'un pack entier et le rejet GPU sont couverts.

Le probe de production CNK puis SDL_GPU/PBR capture 23 frames demandées : les
11 idles au tick zéro, quatre poses du chien, six du cheval et deux états de
mouvement du bateau. Chaque capture 1920x1080 utilise des assets ModernGltf,
un pipeline PBR chargé et contient des triangles/pixels visibles. La mosaïque
`build/token-gpu-qualification/eleven_tokens_gpu.png` a été examinée. Ces frames
isolées ne prouvent pas une animation continue en partie ; le démarrage borné
reste la preuve de neuf chargements statiques seulement.

Le plateau Paris aligné dispose d'une correspondance explicite de ses 40 cases
avec les cellules retail ; son export omet les tangentes authored invalides et
utilise une base dérivée de UV non dégénérées. Les adaptateurs plateau/maison et
les trois décors optionnels sont raccordés. Le probe SDL_GPU soumet **4 objets,
246 batches, 915 731 triangles** ; la mesure de cadence et sa portée sont
consignées dans le [contrat Blender](BLENDER_INTEGRATION_SPEC.md). Un benchmark
avec fence exclut chargement et readback et ne représente pas la boucle complète
du jeu. Le démarrage français requis pour Paris reste bloqué par D02.

MonopolyModern compile et les suites ciblées séquences, variantes, catalogue de
scène, décor et fallback GPU passent. Ce lot ciblé ne remplace pas la campagne
CTest globale historique ni les qualifications Q01–Q05. Le plateau aligné garde
ses positions X/Z, avec sol de jeu Y=0 et relief réduit ; la qualification de
géométrie par rayons indépendants passe dans les bornes consignées au contrat.
Le contact pendant une partie reste à qualifier.

Les cibles Blender sont explicites ; un build ordinaire stage les fichiers déjà
générés sans lancer Blender. Un bake reproductible du vrai graphe procédural
d'asphalte a validé trois cartes embarquées, mais les autres matériaux
procéduraux du plateau/décor restent à convertir ou à cuire.
Le self-test de bake du chapeau utilise ses vrais facteurs, sans cartes normales,
émissives ou AO inventées ; exports répétés, loader et comparaison Blender sont
consignés dans le contrat, sans affirmation de fidélité procédurale ou de partie.
