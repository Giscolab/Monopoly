# Sessions de jeu TCP

Le protocole moderne remplace DirectPlay ; il n’est pas compatible avec les exécutables de 1999. Les identités sont attribuées par l’hôte et ne proviennent jamais des messages du client.

## Démarrer

- Hôte : `MonopolyModern --network-host 0.0.0.0:28799`.
- Client : `MonopolyModern --network-connect 192.168.1.10:28799`, en remplaçant l’adresse par celle de l’hôte.
- Sans argument, le bouton **Network** ouvre une session hôte sur `0.0.0.0:28799`.
- Les adresses sont des IPv4 numériques et le port doit être compris entre 1 et 65535. L’hôte doit être joignable au port choisi.

Dans la sélection, choisir **Network**. Le client attend l’application de l’état reçu avant de proposer ses joueurs ; l’hôte peut préparer ses joueurs dès l’ouverture de l’écoute. Plusieurs joueurs peuvent appartenir à la même machine, dans la limite des six places du jeu. Le bouton **Local** ferme le réseau et attend le reset de la partie locale avant de poursuivre.

Les options `--voice-host` et `--voice-connect` conservent leur mode de spectateurs vocaux ; elles ne deviennent pas implicitement des admissions de joueurs.

## Contrats implémentés

- Connexion mise en attente, puis admission et resynchronisation quand la file RULE est disponible.
- Association des slots aux connexions seulement après acceptation par RULE ; conservation lors des permutations, suppressions et reprises d’IA.
- Vérification de l’identité au dispatch, avant le compteur d’activité ; actions rejetées si leur joueur appartient à une autre connexion ou si leur source a disparu.
- Notifications privées acheminées au propriétaire ; notifications publiques diffusées aux connexions admises. RULE reste sur l’hôte.
- Déconnexion d’un client : ses joueurs deviennent des IA locales selon le comportement d’origine. Perte de l’hôte : fin du réseau, arrêt des anciennes présentations et retour à une partie locale ; aucune migration d’hôte.
- Chargement d’une sauvegarde : joueurs réattribués à l’hôte, comme dans la source.

Ce lot est relu statiquement seulement : aucun build ni test n’a été lancé à la demande de l’utilisateur. Le parcours entre processus/machines et la qualification des ressources restent ouverts dans [PORTING_STATUS.md](PORTING_STATUS.md).
