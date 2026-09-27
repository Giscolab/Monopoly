# Assemblage des ressources DAT

`MonopolyArchiveTool` relie les manifestes DMAKE99 au writer DAT moderne. Il
travaille hors de `Source/`, conserve les identifiants numeriques et ne publie
jamais de banques incompletes dans le dossier de donnees de l'application.

## Executer l'assemblage des ressources fournies

Depuis la racine du depot, apres configuration CMake :

```powershell
cmake --build modern/build --config Debug --target MonopolyAssembleArchives --parallel 6
```

Cette cible compile l'outil puis assemble les contenus disponibles. Ce n'est pas
une suite de tests. Les sorties sont dans un nouveau dossier
`modern/build/Debug/resource-assembly/run-.../` a chaque execution. Aucun ancien
jeu de donnees n'est ecrase et aucune archive n'est automatiquement installee.

- `resources.tsv` : chaque identifiant, son statut, sa source et les erreurs.
- `banks.tsv` : nombre attendu, nombre assemble et chemin produit pour chaque banque.
- `unassigned-files.tsv` : fichiers sans correspondance de manifeste prouvee.
  Les textures externes utilisees par TextureCatalog y figurent aussi : cela
  ne veut pas dire qu'elles sont manquantes ou inutilisees par le jeu.
- `Dat_Mon/` : banques ayant toutes les entrees de leur manifeste.
- `partial/` : archives de reconstruction partielles, jamais pretes a jouer.

L'outil n'ecrit aucun DAT entierement vide. Dans une archive partielle, les trous
restent des entrees absentes et les tags suivants ne sont pas renumerotes.
Il ne remplace ni un son par du silence, ni une sequence par une image, ni un
modele 3D par une geometrie inventee. La signature, le CRC, les types, le nombre
de tags et les octets decompresses sont relus avant publication de chaque DAT.
Cela ne qualifie pas la fidelite du jeu ni les dependances entre sequences.

## Fournir des ressources supplementaires sans toucher Source/

Les candidats automatiques ont exactement le nom attendu, sans distinction de
casse : par exemple `nom.bmp` ou `BMP_nom.bmp` pour `BMP_nom`. Des fichiers ayant
un nom approchant ne sont jamais affectes automatiquement. Si plusieurs fichiers
conviennent, l'entree reste ambigue jusqu'a fourniture d'une correspondance.
Un `TAB_` attend un payload UAP prepare (`.uap` ou `.tab`), pas un BMP renomme.
Les formats BMP/UAP, CNK, HMD et RIFF/WAV font l'objet d'une inspection structurelle.

Pour des noms differents ou des ressources preparees ailleurs, utiliser un TSV :

```text
bank	symbol	root	path
```

`bank` est le nom sans `.dat`, `symbol` le symbole exact du manifeste, `root` vaut
0 pour `--source`, puis 1, 2, etc. pour les racines `--extra-root`, et `path` est un
chemin relatif a cette racine. Les quatre colonnes sont separees par de vraies
tabulations. Les lignes commencant par `#` sont des commentaires. Une faute dans
un symbole, un chemin inexistant, une sortie hors racine ou un doublon est refuse.

```powershell
.\modern\build\Debug\MonopolyArchiveTool.exe `
  --source .\Source\monopoly `
  --extra-root .\modern\resources `
  --map .\modern\resources\archive-map.tsv `
  --output .\modern\build\Debug\resource-assembly `
  --strict
```

Ce second exemple suppose que la racine et le fichier de correspondances ont ete
crees avec de vrais contenus. L'outil ne cree pas de correspondances presumees.
Codes de sortie : 0 = assemblage/rapport produit, eventuellement partiel ;
1 = erreur d'entree ou d'E/S ; 2 = erreur d'arguments ; 3 = `--strict` demande,
mais au moins une banque reste incomplete. Sans `--strict`, lire `banks.tsv` :
un code 0 ne signifie pas que les donnees du jeu sont completes.

## Textes anglais recuperables

`English.atr` contient des messages ASCII entre accolades et leurs identifiants
symboliques. L'outil associe uniquement les symboles qui existent exactement dans
`Dat_Mon/dat_lang.h`, convertit le texte sans changer les espaces ou les codes `^`,
puis produit les chaines UTF-16LE terminees par NUL et l'index logique trie
(message u32 + tag u16) attendu par `LanguageCatalog`.

Les symboles propres a l'ancien ATR et absents du header actif sont consignes
`unmapped-symbol`. Les messages requis par le header mais absents de l'ATR restent
manquants. Aucun decalage numerique d'`English.A` ni aucun ancien header n'est
suppose compatible. Un ATR d'une autre structure ou d'un encodage non ASCII est
refuse plutot que converti incorrectement. La banque texte reste partielle tant
que les identifiants du header actif ne sont pas tous couverts.

Le profil de ce premier assemblage est celui demande au demarrage par defaut :
USA et anglais US, soit sept manifestes DMAKE et la banque texte `dat_ln01`.
Les variantes Europe et les autres langues ne sont pas fabriquees par substitution.

## Premiere execution sur le corpus fourni

Execution du 27 septembre 2026, cible `MonopolyAssembleArchives`, Windows Debug :
compilation et assemblage termines avec code 0, sans suite de tests.

La conversion a produit `partial/dat_ln01.partial.dat` : 126 messages a symbole
exact commun, sur 877 identifiants du header actif, pour une archive de 14 134
octets. Les 475 autres symboles de l'ATR ne sont pas remappes par supposition.
Les sept banques DMAKE n'ont aucun payload associe par nom exact dans cette
execution ; aucun DAT vide n'a ete produit pour elles. Les 1 017 BMP du corpus
sont recenses parmi les fichiers non affectes a ces manifestes et restent
copies a leurs chemins d'utilisation directe par la preparation du runtime.

Ces compteurs mesurent l'assemblage des ressources, PAS le pourcentage de code
termine. Des correspondances explicites ou de nouveaux payloads peuvent faire
progresser la reconstruction. Cette premiere sortie ne debloque pas une partie.
