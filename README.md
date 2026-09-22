# Respiration — plugin F4SE (Fallout 4, ancienne génération 1.10.163)

Fait respirer le personnage : l'os du ventre (`Belly_skin` par défaut) gonfle et se dégonfle
en rythme. Plus l'endurance (points d'action, consommés par le sprint) baisse, plus le rythme
s'accélère et plus l'amplitude augmente.

## Prérequis dans le jeu
- F4SE 0.6.23 (ancienne génération)
- Address Library for F4SE Plugins (version ancienne génération)
- Un corps qui a l'os `Belly_skin` pondéré (CBBE, Fusion Girl... comme pour CBP/OCBP)

## Obtenir la DLL (compilation sur GitHub, gratuit)
1. Crée un dépôt GitHub (privé ou public) et envoie-y tout le contenu de ce dossier
   (`.github/workflows/build.yml` compris).
2. Onglet **Actions** → le workflow « Build Respiration.dll » démarre tout seul au push
   (ou **Run workflow**). Compte 5 à 15 minutes la première fois.
3. Quand il est vert : ouvre l'exécution → section **Artifacts** → télécharge `Respiration`.
4. Copie le dossier `Data` de l'archive dans le dossier `Data` de Fallout 4.
5. Lance le jeu **par F4SE**. Regarde le log :
   `Documents/My Games/Fallout4/F4SE/Respiration.log`

## Ce que dit le log
- `plugin chargé` puis `boucle de respiration démarrée` : le plugin tourne.
- `os trouvé(s), la respiration est active` : `Belly_skin` existe sur ton corps.
- Avec `Debug = 1` : toutes les 2 s, l'AP lue, le ratio, la période et le nombre de nœuds modifiés.
  `nœuds modifiés 0` = l'os n'a pas été trouvé (change `Bones=` dans le .ini).

## Réglages
Tout est dans `Data/F4SE/Plugins/Respiration.ini`, rechargé à chaud toutes les secondes.

## Test hors jeu de la logique
    cd tests && g++ -std=c++20 -I../src test_breath.cpp -o test_breath && ./test_breath
