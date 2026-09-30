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
- Assets Blender/glTF : export headless déterministe des six pions récupérés, staging CMake optionnel, loader GLB statique basé sur `fastgltf v0.9.0`, calibration hauteur/orientation/pivot et fallback HMD contextuel. Le source retail confirme l’ordre `ship=6`, `shoe=7`. Les GLB statiques ne remplacent que les idles mono-HMD sûrs ; les mouvements et les idles multi-HMD (chien/cheval) restent retail pour préserver leurs animations. Le contrat matériau conserve baseColor, metallic, roughness, emissive et double-sided ; shaders PBR, textures, animations modernes complètes et qualification visuelle restent à faire. Voir [contrat Blender/glTF](BLENDER_INTEGRATION_SPEC.md).

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
| A07 | PC3D | Caméras, scènes et matériaux au-delà des contrats HMD consommés déjà fermés. Le backend GLB, la calibration et le fallback par contexte de séquence sont raccordés. L’audit DAT montre que les animations de pions changent de HMD (poseCount MIMe = 1), donc un futur remplacement moderne doit couvrir la séquence entière. Le chemin PBR GPU à facteurs est raccordé et qualifié par lecture de pixels ; les textures PBR, animations modernes et intégration du plateau Blender restent ouverts. |

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

## Qualification sur données et plateformes réelles

| ID | Qualification | Limite actuelle |
|---|---|---|
| Q01 | Parties jouables avec DAT/LANG/CNK retail, règles, IA, UI et langues. | Les payloads retail nécessaires ne sont pas fournis. Des fixtures ne prouvent pas une partie complète. |
| Q02 | Voix entre deux processus puis deux machines, capture et écoute physiques. | Les tests de transport/codec ne prouvent pas le parcours utilisateur ni le matériel. Voir [réseau et voix](NETWORK_VOICE.md). |
| Q03 | Textures/UV/HMD, éditions, devises et scénarios Board Editor personnalisés. | Corpus BMP disponible ; HMD retail et vues externes personnalisées manquants. Le plateau standard ne dépend pas de ces vues externes. |
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

## Modern assets — qualification du 30 septembre 2026

Le correctif de priorité `6299180` est poussé : GLB statiques uniquement pour
les racines idle prévues, aux priorités joueurs 224..229. Chien et déplacements
restent HMD. MonopolyDataCore et MonopolyModern ont compilé ; les tests ciblés
du cache passent. Un démarrage réel de 25 secondes en 1280x720 charge les cinq
pions autorisés, sans preuve visuelle d'une partie ou de déplacements.

Le chargement GLB est borné et vérifie références, accesseurs, indices et
transformations. Les 22 contrôles de fixtures passent. Les nouveaux shaders
PBR à facteurs utilisent des pipelines distincts ; les lectures réelles SDL_GPU
valident matériaux, émission/sRGB, culling et mélange avec Gouraud. DXIL est
exécuté sous Windows ; SPIR-V/MSL sont compilés, sans qualification Linux/macOS.
Les cartes PBR et animations GLB ne sont pas encore actives.

L'outil de timeline réutilise SequenceRuntime et inventorie les 1 089 CNK de
pions sur 600 ticks avec les DAT locaux. Les sorties restent sous build/.
L'inventaire représente des séquences autonomes et ne remplace pas une
qualification des appels de jeu, de la pose sur le plateau ou des médias.