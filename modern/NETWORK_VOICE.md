# GIS-8 : session vocale TCP moderne

Les builds et tests ont ete reautorises. Le transport et Messaging sont
compiles et leurs huit scenarios sur sockets loopback passent. Les commandes
ci-dessous restent a qualifier entre deux machines avec de vrais peripheriques
audio; ces tests automatises ne constituent pas cette preuve.

L'application demarre en local sans argument. Pour ouvrir explicitement une
session vocale, lancer l'hote avec une adresse IPv4 de sa machine et un port :

```text
MonopolyModern.exe --voice-host 192.168.1.20:47624
```

Sur chaque machine cliente, utiliser l'adresse de cet hote :

```text
MonopolyModern.exe --voice-connect 192.168.1.20:47624
```

Les adresses ci-dessus sont des exemples a adapter. `127.0.0.1` permet une
qualification entre deux processus du meme ordinateur. Les essais automatises
utilisent des sockets loopback; aucun microphone physique n a ete utilise.

Le client est un **spectateur vocal** : il recoit les notifications publiques
du serveur et peut emettre des paquets vocaux, mais ne peut ni attribuer ni
commander un joueur distant. Le protocole n'implemente pas le lobby DirectPlay,
la decouverte, la migration d'hote, la reconnexion automatique ou le transport
multijoueur complet. La navigation visuelle d'un spectateur rejoignant une
partie deja commencee reste hors de ce lot. Apres deconnexion, relancer le
client pour rejoindre une nouvelle session; il ne devient jamais serveur
local de la partie distante. Le transport est destine a un reseau de confiance :
il ne fournit ni authentification utilisateur ni chiffrement.

## Contrats raccordes

- `Application` cree le transport apres l'initialisation locale, avant le
  premier cycle. `Messaging` en possede la duree de vie. `serverMode` distingue
  immediatement l'hote du client; `networkMode` devient vrai uniquement apres
  l'echange de version avec au moins un pair, pas a la simple ouverture du port.
- Les sockets IPv4 TCP sont non bloquants, pompes sur le thread du jeu sans
  worker audio/reseau partageant les files. Les lectures/ecritures partielles
  sont conservees. Un pump accepte au plus un pair et traite au plus seize
  trames par pair; la file entrante garde la capacite `MessageQueueCapacity`.
- Le protocole Modern version 1 a un en-tete little-endian de douze octets :
  signature `MMS1`, version u16, type u16, taille u32. Les actions transportent
  type u16, joueurs u8, cinq i64, 80 unites UTF-32 et deux blobs prefixes u32.
  Aucun pointeur, padding C++ ou `wchar_t` natif n'est transmis. Les blobs
  de resynchronisation existants restent opaques; le format ArtLib de la voix
  n'est pas reencode par le transport.
- Maximum neuf clients plus l'hote; une trame est limitee a 1 Mio et chaque
  file sortante a 4 Mio et `MessageQueueCapacity` trames. Le compteur MESS
  inclut la plus grande file sortante, preservant le seuil vocal retail >50.
  Un pair trop lent est deconnecte plutot que de perdre silencieusement CHAT
  ou STOP. Delais : connexion/handshake 10 s, emission bloquee 15 s, absence
  de trame valide 30 s; heartbeat apres 5 s sans emission.
- L'hote attribue une identite machine non nulle et non reutilisee aux clients;
  zero reste l'hote local. `Actions::Message::sourceId` est une metadonnee
  d'ingress, jamais un champ fourni par le client. `RuleCoreActions` la copie
  vers `NotifyVoiceChat.numberD`, comme `ActionEchoChat` utilisait l'adresse
  de provenance. Le retour audio a l'emetteur suit le comportement legacy.
- L'admission provoque `ResyncClient(AllPlayers)` sur l'hote. Le chemin RULE
  existant envoie les options avant l'etat compact. Une resync valide arrete
  puis redemarre la capture pour reemettre CHAT aux nouveaux arrivants.
  `NobodyPlayer` est accepte comme joueur courant avant le debut de partie.
  Si STOP occupe la derniere place de MESS, le demarrage est retente apres
  vidage de la file; les erreurs de peripherique/codec ne bouclent pas.
- L'ingress distant n'autorise que `VoiceChat`, Spectator -> Bank -> All,
  avec champs auxiliaires vides et paquet ArtLib valide. Les notifications
  Bank -> All suivent le chemin UI existant cote client. Les commandes de jeu
  distantes sont rejetees avant RULE; aucune attribution locale n'est simulee.
- `receiveVoiceChatOnly` continue de pomper le reseau pendant les locks
  d'animation, meme si une admission attend la resynchronisation. Cette admission
  ne rejoint RULE qu'une fois MESS vide; DATN/STOP des pairs etablis continuent
  de circuler avec une place reservee pour leur echo. Les admissions AllPlayers
  supplementaires se regroupent dans cette meme resynchronisation.
  Une deconnexion de client genere STOP pour sa source via RULE.
  La perte de la derniere connexion arrete capture et playback, purge les
  sessions vocales et les paquets devenus obsoletes. La fermeture de
  l'application detruit les sockets; STOP final est une tentative sans attente.

## Qualification restante

La compilation Windows de l application et les tests TCP loopback passent.
Le test du verrou d animation a echoue avant correction, puis passe avec
DATN/STOP d un pair etabli pendant une nouvelle admission et la capacite MESS
complete disponible au deverrouillage. Les tests couvrent aussi trames
partielles/invalides, saturation, identites et deconnexion. Trois groupes audio
exercent le codec GSM WAV49, la capture SDL dummy et le playback PCM/GSM dummy.

Restent la qualification de l application avec ses DAT/HMD retail, deux
processus puis deux machines, microphone et sortie audio physiques, arrivee
tardive et reconnexion par relance en partie reelle. Le pilote SDL dummy ne
prouve ni l audibilite ni le fonctionnement d un peripherique reel. Les branches
POSIX sont ecrites mais n ont pas ete compilees. La coalescence de plusieurs
admissions a ete relue mais n a pas encore de scenario automatise specifique.
