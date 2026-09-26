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
