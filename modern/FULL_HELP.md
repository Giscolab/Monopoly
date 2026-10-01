# Aide complète portable

Le bouton **Full Help** convertit le fichier d’aide du jeu en une page HTML autonome,
puis ouvre cette page dans le navigateur par défaut. La conversion est asynchrone :
le jeu continue de traiter les entrées pendant son exécution. Aucun appel à WinHelp
n’est nécessaire ; aucun texte d’aide n’est inventé ni remplacé par le Quick Help.

## Installer l’exporteur

Le programme externe [winhlp](https://github.com/bitplane/winhlp) est requis.
La version qualifiée ici est **0.4.1**, avec Python **3.14**. Depuis la racine du
dépôt, installer uniquement dans le dossier de build (aucun PATH global modifié) :

```powershell
& C:/Python314/python.exe -m venv modern/build/help-export-tools
& ./modern/build/help-export-tools/Scripts/python.exe -m pip install "winhlp==0.4.1" "pillow-wmf==0.1.1" "pillow==12.3.0"
```

`modern/PlayModern.cmd` sélectionne cet exécutable s’il existe, sans remplacer un
`MONOPOLY_WINHLP` déjà défini. Relancer le jeu pour transmettre cette configuration.
Si l’exporteur local est absent, le lanceur affiche la dépendance à fournir ; un
exporteur déjà présent dans PATH reste utilisable. L’interface attendue est :

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
supprime uniquement le fichier et le dossier réservés à cette conversion, sauf si
la demande d’ouverture a déjà été transmise au système : le document est alors
conservé pour une ouverture tardive.

## Contrat et limites de validation

- `openFullHelp` démarre une conversion et signale immédiatement un fichier absent,
  une signature HLP incorrecte, un export déjà actif ou un exécutable introuvable.
- `pollFullHelp`, appelé par la boucle principale, contrôle le processus sans attendre.
  Sous Windows, une tâche native COM/ShellExecuteEx ouvre le document sans bloquer
  le jeu ; `SDL_OpenURL` reste utilisé sur les autres plateformes. Une erreur
  est retournée une seule fois, sans arrêter le jeu.
- `cancelFullHelp` annule le processus avant l’arrêt de SDL.
- Limites par défaut : **30 secondes par phase** (export puis ouverture), **32 Mio
  de HTML**, **16 Kio de diagnostics**. Une demande Windows bloquée ne bloque pas
  la boucle du jeu. Après annulation ou délai dépassé, elle peut encore aboutir ;
  aucune deuxième tâche d’ouverture ne démarre avant son retour.
  Les processus Windows sont masqués ; leur véritable code de sortie est contrôlé.

Qualification du 1er octobre 2026 : le véritable `runtime-data/Mono01.hlp` a été
exporté avec succès. Ses **55 sujets** ont chacun une section HTML, sans erreur du
parseur ni lien interne cassé. Ce fichier contient **zéro ressource bitmap** ; son
export sans image est donc attendu. Le rapport, les versions des dépendances et le
HTML restent dans `modern/build/full-help-qualification/`, hors des sources retail.

Cette vérification structurelle ne prouve pas la fidélité sémantique de chaque sujet
ni l’ouverture en jeu et la navigation dans le navigateur. Le parcours complet doit
être qualifié sur le bureau Windows. La conversion est asynchrone et ce chemin
n’ajoute aucune pause globale : le libellé retail « game will pause » ne constitue
pas une preuve que le jeu moderne se met en pause lors de l’ouverture du navigateur.

Le contrat réel `openFullHelp`/`pollFullHelp` a également réussi avec l’environnement
Windows complet et l’exporteur isolé. Les tests de blocage, délai, annulation et
erreurs vérifient la réactivité sans ouvrir de navigateur. La navigation visuelle
en jeu reste une qualification distincte.

Le rendu compilé du thème a été comparé au véritable export dans Edge à
1920×1080, ainsi qu’à390×844 :55 rubriques,55 liens de navigation et aucun
débordement horizontal. Les94577 octets HTML d’origine restent intacts ;2147
octets CSS/métadonnées sont ajoutés. Les captures et le rapport sont dans
`modern/build/help-theme-pass27/`. La traduction automatique visible dans Edge
n’est pas une traduction produite par le jeu.
