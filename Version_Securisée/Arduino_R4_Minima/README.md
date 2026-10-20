# Contrôle d'accès par badges RFID — version sécurisée pour Arduino UNO R4

Programme Arduino de contrôle d'accès : un lecteur **MFRC522** identifie des badges **MIFARE Classic**, un écran **LCD 20×4 I2C** affiche les messages, et un MOSFET commande une gâche ou une serrure électrique 12 V.

Cette version protège les badges contre la copie : en plus de l'UID, chaque badge reçoit **une clé qui lui est propre** et **un compteur de passages signé**.

Fichier : `Autorisation_Ouverture_Securisee_R4.ino` (**Arduino UNO R4 Minima** ou UNO R4 WiFi)
Version pour Arduino Uno R3 : `Autorisation_Ouverture_Securisee_HD44780.ino` (112 badges)

La protection des badges est identique à la version Uno. Seule la gestion de la mémoire change, pour tirer parti des 8 Ko du R4 et ménager sa mémoire flash.

## Fonctionnalités

- Ouverture de la porte pendant 10 s avec un badge autorisé.
- **Clone de l'UID refusé** : un badge copié avec un copieur ordinaire ne peut pas être lu.
- **Copie complète détectée** : dès que l'original et la copie servent tous les deux, le badge est **bloqué**.
- **Réactivation** d'un badge bloqué : toutes ses anciennes copies deviennent inutilisables.
- Lecture tolérante : si le badge est détecté au bord du champ, le programme réessaie seul pendant 0,4 s.
- **Badge administrateur** (protégé de la même façon) pour ajouter, retirer ou réactiver des badges.
- Sortie automatique du mode programmation après 30 s sans activité.
- **Bouton d'ouverture** intérieur : 5 s minimum, tant qu'il est appuyé, 70 s maximum.
- **Bouton RAZ** pour effacer la base au démarrage, avec compte à rebours d'annulation.
- Jusqu'à **791 badges** (plus le badge administrateur).
- Mémoire flash ménagée : 1 seul effacement par passage, usure répartie sur 7 blocs.
- Fonctionnement **non bloquant** et journal détaillé sur le moniteur série (115200 bauds).

## Badges compatibles

Seuls les badges **MIFARE Classic 1K ou 4K** conviennent : porte-clés bleus et cartes blanches livrés avec les kits RC522. Les **NTAG** et **MIFARE Ultralight** sont refusés.

Pour vérifier un badge sans rien y écrire, utilisez **`Test_Type_Badges.ino`** (moniteur à 115200 bauds). Il doit afficher « COMPATIBLE ».

## Matériel

| Élément | Remarque |
|---|---|
| **Arduino UNO R4 Minima** (ou R4 WiFi) | même format et mêmes broches que l'Uno R3 |
| Lecteur RFID MFRC522 | alimenté en **3,3 V** |
| Écran LCD 20×4 avec module I2C (PCF8574) | adresse détectée automatiquement |
| LED RVB à cathode commune | résistances de **470 Ω à 1 kΩ** (broches limitées à 8 mA) |
| Buzzer | par un transistor s'il consomme plus de 8 mA |
| 2 boutons-poussoirs | ouverture et RAZ |
| MOSFET IRF3205 + résistance 10 kΩ (R2) + diode 1N4007 (D1) | commande de la serrure |
| Alimentation 12 V (prise jack) | le R4 accepte 6 à 24 V ; alimente aussi la serrure par VIN |
| Gâche ou serrure électrique 12 V | ou relais 12 V |

## Câblage

| Élément | Broche |
|---|---|
| Bouton d'ouverture (vers GND) | D2 |
| Bouton RAZ de la base (vers GND) | D3 |
| Grille du MOSFET (HIGH = porte ouverte) | D4 |
| LED bleue / verte / rouge | D5 / D6 / D7 |
| Buzzer | D8 |
| MFRC522 RST / SDA (SS) | D9 / D10 |
| MFRC522 MOSI / MISO / SCK | D11 / D12 / D13 |
| MFRC522 VCC | **3,3 V** |
| LCD SDA / SCL | A4 / A5 |

Commande de la serrure : R2 (10 kΩ) entre la grille et la masse ; drain relié au « − » de la serrure, dont le « + » va au 12 V (VIN) ; source à la masse ; **D1 (1N4007) en parallèle sur la serrure, cathode (bague) côté +12 V**.

Le câblage est **identique à la version Uno** : les schémas et le circuit imprimé du projet s'appliquent tels quels.

> ⚠️ **Broches du R4 limitées à 8 mA** (environ 20 mA sur l'Uno) : résistances de LED de 470 Ω à 1 kΩ, buzzer par transistor si nécessaire.
> ⚠️ **Le MFRC522 s'alimente en 3,3 V**, jamais en 5 V.
> ⚠️ **Respectez le sens de D1** : à l'envers, elle court-circuite le 12 V dès que le MOSFET conduit.

## Installation

1. Installez l'**IDE Arduino** (2.x conseillé).
2. Dans le gestionnaire de cartes, installez **Arduino UNO R4 Boards**.
3. Installez les bibliothèques **MFRC522** (GithubCommunity) et **hd44780** (Bill Perry).
4. Placez le fichier dans un dossier `Autorisation_Ouverture_Securisee_R4/`.
5. **Personnalisez la clé maître** (voir ci-dessous).
6. Sélectionnez la carte **Arduino UNO R4 Minima** (ou R4 WiFi) et le port, puis téléversez.

Compilé pour un autre type de carte, le programme s'arrête avec un message qui renvoie vers la version Uno.

### La clé maître `CLE_MAITRE`

```cpp
const byte CLE_MAITRE[16] PROGMEM = {
  0xF8, 0x9B, 0xE3, 0x3B, 0x35, 0xA2, 0x18, 0xC0,   // <- remplacer ces 16 valeurs
  0x3D, 0xA2, 0x1D, 0x0B, 0xEB, 0xAE, 0xCB, 0xE2    //    par les vôtres, au hasard
};
```

- Remplacez les 16 valeurs **avant d'enregistrer le premier badge**.
- **Notez-les en lieu sûr** et **ne les modifiez plus jamais** : si la clé change, les badges déjà préparés ne sont plus reconnus.
- Ne diffusez pas votre fichier `.ino` personnalisé.
- **Vous venez de la version Uno ?** Reprenez **exactement la même clé maître** : vos badges déjà préparés seront réenregistrés sur le R4 sans être remis à zéro.

## Utilisation

### Première mise en service

L'écran affiche *« Pas de Badge Administrateur »* et la LED bleue clignote. Présentez un badge MIFARE Classic : il devient le **badge administrateur** et reçoit sa clé et son compteur.

La base est rangée dans la mémoire de la carte et **ne passe pas d'une carte à l'autre** : sur un R4 neuf, enregistrez l'administrateur puis réenregistrez les badges. Si la mémoire contient une base d'une autre version, le message *« Ancienne base détectée : badges à réenregistrer »* s'affiche.

### Au quotidien

| Écran | Signification |
|---|---|
| **Accès Autorisé** | Clé, signature et compteur corrects : porte ouverte 10 s |
| **Accès REFUSÉ** | Badge inconnu |
| **Badge NON AUTHENTIQUE** | Signature invalide ou ancienne copie du badge |
| **Badge non compatible** | Pas un MIFARE Classic |
| **Lecture du badge incomplète** | Badge mal présenté : le représenter bien à plat, sans bouger |
| **Badge SUSPECT / Copie détectée** | Compteur en retard : une copie a servi, le badge est **bloqué** |
| **Badge BLOQUÉ** | Badge bloqué précédemment : voir l'administrateur |

### Mode programmation

1. Présentez le **badge administrateur** : *« MODE PROGRAMMATION »* s'affiche avec le nombre de badges.
2. Présentez un badge, **immobile jusqu'au message** :
   - **inconnu** → il est préparé (clé, compteur, n° d'inscription) et **ajouté** ;
   - **déjà connu** → il est **retiré** et remis en configuration d'usine.
3. Représentez le badge administrateur pour sortir, ou attendez 30 s.

**Réactiver un badge bloqué :** en mode programmation, présentez-le une première fois (il est retiré), puis une seconde fois (il est réenregistré). Ses anciennes copies sont alors refusées.

### Effacer toute la base (RAZ)

1. Appuyez sur le **bouton RAZ** (D3) et maintenez-le.
2. Faites un **RESET** sans relâcher le bouton.
3. Gardez-le appuyé pendant le compte à rebours de **5 s**.

Tous les badges sont effacés de la mémoire, y compris l'administrateur. **Les badges gardent leur clé** : avec la même clé maître, ils sont simplement réenregistrés.

## Signalisation

| Situation | LED | Buzzer |
|---|---|---|
| Attente | Bleue fixe | — |
| Mode programmation | Défilement vert / bleu / rouge | — |
| Accès autorisé | Verte fixe | Mélodie montante |
| Accès refusé / lecture incomplète | Rouge 2 s | Deux bips graves |
| **Badge suspect** | Rouge, 6 clignotements | **Trois bips aigus** |
| Badge bloqué présenté | Rouge 4 s | Deux bips graves |
| Badge ajouté / retiré | Verte / bleue, 3 clignotements | Bip |

## Réglages

| Constante | Défaut | Rôle |
|---|---|---|
| `DUREE_OUVERTURE_BADGE` | 10000 | Durée d'ouverture après un badge autorisé (ms) |
| `DUREE_OUVERTURE_BP_MIN` / `_MAX` | 5000 / 70000 | Durées d'ouverture par le bouton (ms) |
| `DUREE_MESSAGE` / `DUREE_MESSAGE_ALERTE` | 2000 / 4000 | Durée des messages (ms) |
| `TIMEOUT_PROGRAMMATION` | 30000 | Sortie automatique du mode programmation (ms) |
| `ESSAIS_LECTURE` / `PAUSE_ESSAI` | 6 / 60 | Tentatives de lecture et pause entre elles (ms) |
| `AVANCE_MAX` | 3 | Avance tolérée du compteur du badge |
| `SECTEUR_BADGE` | 1 | Secteur du badge utilisé (ne plus changer ensuite) |
| `CLE_MAITRE` | 16 octets | Clé secrète, à personnaliser une seule fois |

## Organisation des données

**EEPROM du R4 : 8 Ko simulés en mémoire flash, 8 blocs de 1 Ko.** Chaque modification efface et réécrit le bloc entier (environ 100 000 fois par bloc). Le programme groupe les écritures (1 effacement par passage), répartit les badges sur 7 blocs, et garde une copie de la base en mémoire vive.

| Emplacement | Contenu |
|---|---|
| Bloc 0, `0x0000` | En-tête : drapeau `0xB4` (1) + version (1) + nombre de badges (2) + UID admin (4) + compteur admin (2) + n° d'inscription admin (2) |
| Blocs 1 à 7 | Badges, 9 octets : UID (4) + compteur (2) + n° d'inscription (2) + état (1 : 0 actif, 1 bloqué) |
| Rangement | Badge n° i (0, 1, 2…) dans le bloc 1 + (i modulo 7), au rang i / 7 : 113 badges par bloc, 791 au total |

Durée de vie : environ **700 000 passages** au total, soit plus de 150 ans à 10 passages par jour.

**Badge, secteur 1 :**

| Bloc | Contenu |
|---|---|
| 4 | `'C' 'A'` + compteur (2) + n° d'inscription (2) + signature (8) + 2 octets à 0 |
| 7 | Clé A propre au badge + droits d'accès `FF 07 80 69` + clé B propre au badge |

Les clés et la signature sont calculées par **SipHash-2-4** à partir de la clé maître et de l'UID.

Pour consulter la base sans la modifier : **`Lecture_EEPROM_Badges_R4.ino`** (115200 bauds). Il affiche pour chaque badge son bloc, son adresse, le nombre de passages, le n° d'inscription et l'état.

## Dépannage

| Symptôme | Solution |
|---|---|
| « Erreur Communication / Lecteur RFID » | Vérifier le câblage SPI (D9 à D13), le 3,3 V et la masse du MFRC522 |
| « Lecture du badge incomplète » fréquent | Présenter le badge à plat et immobile ; activer `PCD_SetAntennaGain(RxGain_max)` dans `setup()` |
| « Badge non compatible » à l'enregistrement | NTAG / Ultralight, badge d'un autre système, ou autre clé maître : vérifier avec `Test_Type_Badges` |
| Tous les badges refusés après un téléversement | Clé maître différente : reprendre la clé d'origine |
| « Badge NON AUTHENTIQUE » pour l'original | Ancienne copie, ou badge réenregistré ailleurs : réactiver le badge |
| Badge administrateur perdu | Faire un RAZ, puis tout réenregistrer |
| L'Arduino redémarre à l'ouverture | Alimentation 12 V par la prise jack, câblage du MOSFET et de la diode D1 |
| « #error Version prevue pour Arduino UNO R4 » | Sélectionner la carte Arduino UNO R4 Minima, ou utiliser la version Uno |
| Aucun message de démarrage au moniteur série | Normal sur le R4 (USB intégré) : ouvrir le moniteur puis appuyer sur RESET |
| LED très faibles ou buzzer muet | Résistances trop fortes, ou buzzer de plus de 8 mA à commander par un transistor |

## Sécurité et limites

- Un clone de l'UID est refusé et une copie complète est détectée. Le chiffrement des MIFARE Classic (Crypto1) est toutefois **cassé** : une personne équipée peut copier entièrement un badge et entrer **une fois** avant d'être repérée. Ce niveau convient à un garage, un atelier ou un local.
- **Seul le lecteur RC522 doit être accessible de l'extérieur** : l'Arduino, le MOSFET, les boutons et les fils de la gâche doivent être du côté protégé.
- Gardez la **clé maître** secrète et en lieu sûr.
- Mémoire flash : environ 700 000 passages au total grâce à la répartition sur 7 blocs.

## Fichiers du projet

| Fichier | Rôle |
|---|---|
| `Autorisation_Ouverture_Securisee_R4.ino` | Programme principal (UNO R4, 791 badges) |
| `Autorisation_Ouverture_Securisee_HD44780.ino` | Programme principal (Uno R3, 112 badges) |
| `Lecture_EEPROM_Badges_R4.ino` | Lecture de la base (UNO R4) |
| `Lecture_EEPROM_Badges_Securise.ino` | Lecture de la base (Uno R3) |
| `Test_Type_Badges.ino` | Vérifie la compatibilité d'un badge, sans rien écrire |
| `Manuel_Controle_Acces_RFID_R4.pdf` | Manuel complet d'utilisation (UNO R4) |
| `Manuel_Controle_Acces_RFID.pdf` | Manuel complet d'utilisation (Uno R3) |
| `Securite_Badges_Option4.pdf` | Principe de la protection des badges |
| `Controle_Acces_RFID_RC522_HD44780.fzz` | Schéma Fritzing |

## Dépendances

- [MFRC522](https://github.com/miguelbalboa/rfid)
- [hd44780](https://github.com/duinoWitchery/hd44780)
- Paquet de cartes **Arduino UNO R4 Boards**
- EEPROM, SPI, Wire (fournies avec le paquet de cartes)
