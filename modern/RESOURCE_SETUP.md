# Ressources au démarrage

Au démarrage, Monopoly Modern vérifie les huit banques de l'édition USA et le
catalogue de langue anglais US utilisés par l'application. Si le dossier est
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

Le code de sortie vaut `0` si les banques et le catalogue LANG peuvent être
ouverts, `1` sinon. Les erreurs de toutes les banques requises sont affichées
ensemble. La vérification utilise les lecteurs DAT et LANG du jeu moderne ;
elle ne prouve pas que chaque séquence, texture ou média sera correctement lu
pendant une partie.

Les options réseau existantes restent combinables avec `--data-root`. L'option
`--check-resources` termine le programme avant toute connexion réseau.

Les fichiers attendus sous `Dat_Mon` sont `dat_main.dat`, `dat_pat.dat`,
`dat_bord.dat`, `dat_brd2.dat`, `dat_3d.dat`, `dat_ln01.dat`, `dat_lm01.dat` et
`dat_lk01.dat`. Les en-têtes `.h`, les manifestes et les archives de code source
ne remplacent pas ces données. Les autres éditions/langues prises en charge par
les lecteurs ne sont pas sélectionnées automatiquement par ce démarrage.


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
