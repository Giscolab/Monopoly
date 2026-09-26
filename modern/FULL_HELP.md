# Aide complète portable

Le bouton **Full Help** convertit le fichier d’aide du jeu en une page HTML autonome,
puis ouvre cette page dans le navigateur par défaut. La conversion est asynchrone :
le jeu continue de traiter les entrées pendant son exécution. Aucun appel à WinHelp
n’est nécessaire ; aucun texte d’aide n’est inventé ni remplacé par le Quick Help.

## Installer l’exporteur

Le programme externe [winhlp](https://github.com/bitplane/winhlp) est requis.
Installer sa version compatible avec le Python disponible, ainsi que ses dépendances
d’export HTML et d’images indiquées dans sa documentation. L’interface attendue est :

```text
winhlp fichier.hlp --html sortie.html --images embed
```

`winhlp` doit être accessible dans `PATH`. Autrement, définir `MONOPOLY_WINHLP` avec
le chemin de son exécutable. Cette valeur désigne un fichier exécutable, pas une
commande shell : ne pas y ajouter d’arguments ou de guillemets. Le jeu transmet chaque
argument séparément, y compris les chemins contenant des espaces ou des accents.

L’exporteur est une dépendance externe sous GPL-2.0 ; son code n’est pas incorporé
au moteur. Selon sa version et ses dépendances d’images, certains formats intégrés
peuvent demander un composant supplémentaire : vérifier le rendu des fichiers
réels avant de considérer l’aide qualifiée.

## Fichiers et stockage

Le nom demandé reste celui du jeu d’origine : `monoNN.hlp`, où `NN` est l’identifiant
de la langue. Les racines de ressources et leur résolution de casse habituelle sont
utilisées. Il n’y a aucun repli silencieux vers une autre langue.

Le dépôt fournit `Source/monopoly/Mono01.hlp`, `Mono_US.hlp` et
`Documents/WestwoodMonopoly Rules.hlp`. Seul `Mono01.hlp` correspond directement au
nom numéroté attendu ; les autres ne sont pas substitués automatiquement. Les HLP des
autres langues restent à fournir dans une racine de ressources pour ouvrir leur aide.

Les exports sont écrits dans le sous-dossier `full-help` du répertoire de préférences
SDL de **Giscolab/Monopoly**, jamais dans `Source/` ou à côté des ressources. Chaque
conversion réserve un dossier distinct, sans écraser un document déjà ouvert. Les
exports réussis y restent disponibles pour le navigateur ; l’utilisateur peut vider
ce cache lorsque les pages correspondantes sont fermées. Un échec ou une annulation
supprime uniquement le fichier et le dossier réservés à cette conversion.

## Contrat et limites de validation

- `openFullHelp` démarre une conversion et signale immédiatement un fichier absent,
  une signature HLP incorrecte, un export déjà actif ou un exécutable introuvable.
- `pollFullHelp`, appelé par la boucle principale, contrôle le processus sans attendre.
  Il ouvre le document terminé avec `SDL_OpenURL`, puis libère le processus. Une erreur
  est retournée une seule fois, sans arrêter le jeu.
- `cancelFullHelp` annule le processus avant l’arrêt de SDL.
- Limites par défaut : **30 secondes**, **32 Mio de HTML**, **16 Kio de diagnostics**.
  Les processus Windows sont masqués ; leur véritable code de sortie est contrôlé.

Un code de sortie réussi et un document HTML complet prouvent le fonctionnement du
contrat d’export, pas la fidélité de tous les sujets, liens et illustrations. L’exporteur
peut tolérer certaines erreurs de lecture HLP. La comparaison des documents produits
avec les fichiers réels, leur navigation dans le navigateur et le parcours Full Help
en jeu restent à qualifier. Aucun export réel n’a été exécuté pour cette modification.
