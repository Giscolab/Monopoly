# Ressources au démarrage

Au démarrage, Monopoly Modern vérifie les huit banques et le catalogue LANG du
contexte choisi. Le défaut est USA/anglais US ; `--edition=usa|europe` et
`--language=en-us|en-uk|fr` sélectionnent explicitement une autre combinaison.
Si le dossier est
incomplet ou contient une banque illisible, une fenêtre présente les fichiers
concernés et permet de choisir un autre dossier sans relancer le programme.
Sélectionner le dossier qui **contient** `Dat_Mon`, puis laisser la vérification
se terminer. Annuler ferme proprement l'application avant l'initialisation GPU.

Le dossier peut aussi être fourni explicitement :

```powershell
.\MonopolyModern.exe --data-root 'C:\Jeux\Monopoly'
```

Ce choix remplace `MONOPOLY_DATA_ROOT` pour ce lancement uniquement. Sans choix
explicite, cette variable est utilisée si elle existe ; sinon l'application
cherche les ressources à côté de son exécutable. Aucun fichier du dossier choisi
n'est modifié ou copié. Le choix interactif n'est pas enregistré entre lancements.

Pour contrôler l'installation sans ouvrir de fenêtre ni initialiser le rendu :

```powershell
.\MonopolyModern.exe --data-root 'C:\Jeux\Monopoly' --check-resources
```

Le contrôle peut aussi cibler les ressources Europe/français :

```powershell
.\MonopolyModern.exe --data-root 'C:\Jeux\Monopoly' `
  --edition=europe --language=fr --check-resources
```

Le code de sortie vaut `0` si les banques et le catalogue LANG peuvent être
ouverts, `1` sinon. Les erreurs de toutes les banques requises sont affichées
ensemble. La vérification utilise les lecteurs DAT et LANG du jeu moderne ;
elle ne prouve pas que chaque séquence, texture ou média sera correctement lu
pendant une partie.

## Surcharges DATA hors DAT

Le runtime peut maintenant remplacer des entrées DATA précises par des payloads
hors archive tout en conservant les banques retail comme repli. Fournir un
manifeste TSV absolu avec `--data-overrides` :

```powershell
.\MonopolyModern.exe --data-root 'C:\Jeux\Monopoly' `
  --data-overrides 'C:\Dev\MonopolyAssets\overrides.tsv'
```

Le manifeste contient quatre colonnes séparées par des tabulations :

```text
group	tag	type	path
9	42	String	language/message-42.bin
8	120	Hmd	models/token-120.hmd
```

`group` et `tag` acceptent des entiers décimaux ou `0x...`. `type` reprend
le nom exact de `LegacyDataType`. `path` est relatif au dossier du manifeste,
ne peut pas en sortir avec `..`, et pointe vers le payload logique non compressé.
Une entrée déclarée masque le même `DataId` dans les DAT ; toute entrée non
déclarée continue d'être lue dans les banques retail. Les groupes de langue sont
également routés par cette couche avant validation du catalogue.

Ce mécanisme découple le runtime du **conteneur DAT** ; il ne convertit pas
arbitrairement un fichier moderne pour chaque consommateur. Le backend GLB et
ses PNG/JPEG embarqués sont désormais raccordés pour les meshes qualifiés,
séparément de ces surcharges de payloads. Les huit banques du contexte choisi
restent requises comme fallback tant que leurs contrats n'ont pas tous un backend
moderne.

Les options réseau existantes restent combinables avec `--data-root` et
`--data-overrides`. L'option
`--check-resources` termine le programme avant toute connexion réseau.

Les fichiers attendus sous `Dat_Mon` sont `dat_main.dat`, `dat_pat.dat`,
`dat_bord.dat`, `dat_brd2.dat`, `dat_3d.dat`, `dat_ln01.dat`, `dat_lm01.dat` et
`dat_lk01.dat`. Les en-têtes `.h`, les manifestes et les archives de code source
ne remplacent pas ces données. Europe remplace `dat_bord.dat` par
`dat_borde.dat` ; anglais UK utilise les triplets `ln02/lm02/lk02`, français
`ln03/lm03/lk03`. Le contexte est explicite, jamais déduit d'un GLB installé.

L'installation locale vérifiée ne fournit pas `dat_borde.dat`, `dat_ln03.dat`,
`dat_lm03.dat` et `dat_lk03.dat`. Le contrôle Europe/français signale ces quatre
absences : un export Paris, des fixtures ou un rendu GPU isolé ne les remplacent
pas et ne prouvent pas un démarrage français réel.


## Copie automatique des ressources fournies

La cible `MonopolyRuntimeResources`, requise par `MonopolyModern`, copie les
ressources disponibles dans `Source/monopoly` vers le dossier de l'executable
(`build/Debug` ou `build/Release`). Elle s'execute aussi sans changement C++ :
une image ajoutee ou une copie supprimee sera prise en compte au prochain build.
`Source/` reste en lecture seule. Aucune sauvegarde de partie ni configuration
historique de machine n'est copiee.

Les images conservent leurs chemins, notamment `Boards/`, `Cities/`, `Currency/`
et `Languages/`, utilises directement par `TextureCatalog`. Les deux fonds ont
aussi leur copie sous `assets/legacy/`. Les douze profils IA sont copies sous
`assets/ai/`, avec les noms attendus par leur chargeur. Les fichiers QuickHelp,
HLP, la licence et les descriptions des textures conservent leurs chemins.

Les en-tetes de manifestes ainsi que `English.A` et `English.atr` sont conserves
sous `assets/reconstruction/` : ce sont des entrees pour la reconstruction, pas
des archives DAT pretes a jouer. Une banque `Dat_Mon/*.dat` deja fournie est
copiee telle quelle ; une banque differente deja presente n'est jamais ecrasee.
Le script ne fabrique aucune archive vide pour masquer une ressource absente.

Pour ne preparer que les fichiers, sans compiler le C++ ni lancer de tests :

```powershell
cmake --build modern/build --config Debug --target MonopolyRuntimeResources
```

`assets/resource-copies.tsv` consigne chaque origine, destination, taille et
empreinte SHA-256. `assets/resource-status.txt` donne les compteurs et la liste
des huit banques requises encore absentes. La presence d'une banque n'est pas
une validation de son contenu ni de la jouabilite. La copie des images et la
compilation ne reconstituent pas les sequences CNK, TAB, HMD ou les sons absents.


## Assemblage des archives

Le programme `MonopolyArchiveTool` et la cible CMake `MonopolyAssembleArchives`
assemblent les payloads fournis et les textes anglais compatibles. Voir
[ARCHIVE_ASSEMBLY.md](ARCHIVE_ASSEMBLY.md) pour les commandes, les correspondances
explicites et les rapports. Les sorties partielles restent isolees du repertoire
de lancement : la copie des BMP seule ne reconstitue pas tous les DAT.

## Assets Blender optionnels

`MonopolyRuntimeModernAssets` stage les GLB déjà générés sous
`assets/modern/` à côté de l'exécutable. Un build ordinaire n'exécute pas Blender.
Les cibles d'export sont explicites :

```powershell
cmake --build modern/build --config Debug --target MonopolyExportModernAssets
cmake --build modern/build --config Debug --target MonopolyReconstructModernTokens
cmake --build modern/build --config Debug --target MonopolyExportModernTokenVariants
cmake --build modern/build --config Debug --target MonopolyExportModernSceneAssets
cmake --build modern/build --config Debug --target MonopolyExportAlignedBoardAssets
cmake --build modern/build --config Debug --target MonopolyExportModernEnvironment
```

Le plateau runtime est `board/paris_board_runtime.glb`, distinct du plateau
diagnostique non aligné. Son export utilise les 40 cellules décodées du retail.
Maison, fontaine, gare et colonne Morris sont des fichiers séparés ; l'adaptateur
de décor ne crée pas de dépendance DATA retail fictive.

`--modern-board=paris`, `--modern-buildings=house` et
`--modern-environment=paris` activent ces chemins optionnels. Le plateau Paris
requiert le contexte Europe/français/Paris/euro, sans plateau personnalisé ; les
décors ne s'ajoutent que lorsque le vrai plateau moderne est actif. Une géométrie
absente ou rejetée conserve le repli retail ou omet le décor facultatif. Le bake
d'un sous-ensemble procédural et le probe SDL_GPU sont des validations d'assets,
pas une preuve de partie complète. Voir le [contrat Blender](BLENDER_INTEGRATION_SPEC.md).
