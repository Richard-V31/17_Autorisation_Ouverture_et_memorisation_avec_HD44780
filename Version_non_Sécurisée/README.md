# Contrôle d'accès par badges RFID — version normale

Programme Arduino de contrôle d'accès : un lecteur **MFRC522** identifie les badges par leur **UID**, la liste des badges autorisés est conservée dans l'**EEPROM**, et un écran **LCD 20×4 I2C** (librairie `hd44780`) guide l'utilisateur.

Fichier : `Autorisation_Ouverture_NORMAL_HD44780.ino`

---

## Sommaire

- [Fonctions](#fonctions)
- [Matériel](#matériel)
- [Câblage](#câblage)
- [Installation](#installation)
- [Utilisation](#utilisation)
- [Signalisation](#signalisation)
- [Moniteur série](#moniteur-série)
- [Organisation de l'EEPROM](#organisation-de-leeprom)
- [Réglages](#réglages)
- [Dépannage](#dépannage)
- [Version normale ou sécurisée](#version-normale-ou-sécurisée)

---

## Fonctions

- Ouverture de la porte pendant **10 s** par un badge autorisé.
- Refus des badges inconnus (message, LED rouge, bip grave).
- **Badge administrateur** défini à la première mise en service : il donne accès au mode programmation (il n'ouvre jamais la porte).
- **Mode programmation** : présenter un badge inconnu l'ajoute, un badge connu le retire. Sortie par le badge admin ou automatiquement après **20 s** sans action.
- **Bouton d'ouverture forcée** (D2) : 5 s minimum, tant que le bouton est appuyé, **9 s maximum** (sécurité bouton coincé), avec anti-rebond de 50 ms.
- **Bouton d'effacement** de la base (D3), actif au démarrage, avec 5 s pour annuler.
- Programme **non bloquant** (automate à états) : le bouton reste surveillé pendant les messages et animations.
- Consultation de la base sur le **moniteur série** (lecture seule).
- Capacité : **254 badges** + 1 badge administrateur.
- Tous badges ISO 14443A 13,56 MHz lisibles par le MFRC522 (MIFARE Classic, Ultralight, NTAG…).

## Matériel

| Élément | Remarque |
|---|---|
| Arduino UNO R3 (ou UNO R4 Minima / WiFi) | |
| Module RFID MFRC522 13,56 MHz + badges | Alimentation **3,3 V** |
| LCD 20×4 HD44780 avec module I2C (PCF8574) | Adresse détectée automatiquement |
| 3 LEDs (rouge, verte, bleue) + résistances 220–330 Ω | |
| Buzzer **passif** | Notes générées par `tone()` |
| 2 boutons-poussoirs | Ouverture forcée, effacement |
| Relais ou transistor/MOSFET + alimentation de la gâche | Jamais la gâche en direct sur une sortie |

## Câblage

| Élément | Broche module | Broche Arduino | Remarque |
|---|---|---|---|
| MFRC522 | VCC | 3.3V | ⚠️ jamais en 5 V |
| MFRC522 | RST | D9 | |
| MFRC522 | SDA (SS) | D10 | |
| MFRC522 | MOSI / MISO / SCK | D11 / D12 / D13 | ou connecteur ICSP |
| MFRC522 | IRQ / GND | — / GND | IRQ non connecté |
| LCD I2C | SDA / SCL | A4 / A5 | ou broches SDA/SCL |
| LCD I2C | VCC / GND | 5V / GND | |
| Bouton ouverture | — | D2 → GND | pull-up interne |
| Bouton RAZ base | — | D3 → GND | pull-up interne |
| Serrure | commande relais | D4 | HIGH = ouverte |
| LED bleue | anode + résistance | D5 | attente |
| LED verte | anode + résistance | D6 | accès autorisé |
| LED rouge | anode + résistance | D7 | refus / fermée |
| Buzzer passif | + | D8 | – au GND |

> Ajouter une diode de roue libre sur la bobine de la gâche si le module relais n'en possède pas.

## Installation

1. Dans l'IDE Arduino, **Outils > Gérer les bibliothèques**, installer :
   - **MFRC522** (GithubCommunity / miguelbalboa)
   - **hd44780** (Bill Perry)

   `EEPROM`, `SPI` et `Wire` sont fournies avec l'IDE.
2. Ouvrir `Autorisation_Ouverture_NORMAL_HD44780.ino`.
3. Choisir la carte et le port, puis **Téléverser**.
4. (Facultatif) Ouvrir le moniteur série à **115200 bauds**.

Le téléversement **ne modifie pas l'EEPROM** : la base et le badge admin sont conservés lors d'une mise à jour (y compris depuis l'ancienne version normale, même format).

### Séquence de démarrage

1. Serrure verrouillée, LEDs éteintes.
2. Initialisation du LCD (s'il est absent : arrêt, la LED D13 clignote un code d'erreur).
3. Contrôle du lecteur RFID (s'il ne répond pas : « Erreur Communication », LED rouge).
4. Bouton RAZ appuyé → procédure d'effacement.
5. Vérification de la cohérence de l'EEPROM.
6. Pas de badge admin → demande d'un badge admin.
7. Affichage de la base sur le moniteur série, puis écran d'attente.

## Utilisation

| Je veux… | Je fais… |
|---|---|
| Définir le badge admin | À la 1re mise en service, je présente le badge voulu (LED bleue clignotante) |
| Ouvrir la porte | Je présente un badge autorisé, ou j'appuie sur le bouton D2 |
| Entrer en mode programmation | Je présente le badge administrateur |
| Ajouter un badge | En mode programmation, je présente le nouveau badge |
| Retirer un badge | En mode programmation, je présente le badge à retirer |
| Sortir du mode programmation | Je représente le badge admin, ou j'attends 20 s |
| Tout effacer | BP RAZ appuyé + RESET, je garde le bouton appuyé pendant les 5 s |
| Voir la liste des badges | Moniteur série 115200 bauds, commande `r` |

> ⚠️ En mode programmation, le même geste **ajoute ou retire** selon que le badge est connu ou non : présenter deux fois le même badge l'ajoute puis le retire. Vérifier le message affiché.

> L'effacement supprime **aussi** le badge administrateur. En cas de perte du badge admin, c'est la seule solution : effacer, puis définir un nouvel admin et réenregistrer les badges.

Pendant une ouverture (badge ou bouton), les badges ne sont pas lus. En mode programmation, aucun badge n'ouvre la porte, mais le bouton d'ouverture reste actif.

## Signalisation

### LEDs

| LED | État | Signification |
|---|---|---|
| Bleue | Fixe | Attente d'un badge |
| Bleue | Clignotante | Attente du badge administrateur |
| Bleue | 3 clignotements | Badge retiré |
| Verte | Fixe | Porte ouverte |
| Verte | 3 clignotements | Badge ajouté |
| Rouge | Fixe | Refus, porte refermée, ou erreur lecteur |
| Rouge | 3 clignotements | Base pleine, ou base effacée |
| Verte → bleue → rouge | Chenillard | Mode programmation actif |

### Buzzer

| Son | Signification |
|---|---|
| Arpège montant de 5 notes | Accès autorisé |
| Bip court + bip long graves (370 Hz) | Refus ou base pleine |
| Bip court 1500 Hz | Ouverture par le bouton |
| Bip court 2200 Hz | Badge ajouté / admin enregistré |
| Bip court 1800 Hz | Badge retiré |

## Moniteur série

Vitesse : **115200 bauds**. Les commandes sont traitées quand le lecteur est en attente d'un badge et ne font que **lire** l'EEPROM.

| Commande | Action |
|---|---|
| `r` | Affiche la base : indicateur, badge admin, nombre et liste des badges |
| `d` | Vidage brut des 1024 octets de l'EEPROM en hexadécimal |
| `h` | Rappel des commandes |

Chaque badge présenté est affiché avec la taille et la valeur de son UID, par exemple :

```
Badge scanne (4 octets) UID: B7 C9 47 1B
```

Sur UNO R3, l'ouverture du moniteur redémarre la carte (serrure fermée pendant ce temps) et la base s'affiche en fin de démarrage. Sur UNO R4, envoyer `r`.

## Organisation de l'EEPROM

| Adresse | Taille | Contenu |
|---|---|---|
| `0x00` | 1 octet | Nombre de badges utilisateurs (0 à 254), hors admin |
| `0x01` | 1 octet | Indicateur : `0x80` = badge admin enregistré |
| `0x02`–`0x05` | 4 octets | UID du badge administrateur |
| `0x06`–`0x09` | 4 octets | UID du badge n° 1 |
| `0x0A`–`0x0D` | 4 octets | UID du badge n° 2 |
| … | … | … |
| `0x3FA`–`0x3FD` | 4 octets | UID du badge n° 254 |

Adresse d'une fiche : `adresse = 0x06 + (n - 1) × 4`

Exemple (admin `86 32 3C 1F`, deux badges) :

```
  Adr   00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F
0x0000  02 80 86 32 3C 1F B7 C9 47 1B 04 A1 5C 22 00 00
```

- **Ajout** : UID écrit dans la fiche suivante, puis compteur mis à jour **en dernier** (base cohérente en cas de coupure).
- **Retrait** : les fiches suivantes remontent d'un cran, la dernière est remise à zéro. Les numéros de badge ne sont donc pas fixes : seul l'UID identifie un badge.
- **Effacement** : `0x00` dans les 1024 octets.
- **Usure** : écriture uniquement lors d'un ajout, retrait ou effacement, via `EEPROM.update()` (n'écrit que si la valeur change).

### Contrôles au démarrage

| Situation | Action |
|---|---|
| Indicateur = `0x80` | Base valide |
| Indicateur ≠ `0x80` (EEPROM neuve, effacée) | Demande d'un badge admin |
| Indicateur = `0xA5` (base de la version sécurisée) | Message, base remise à zéro, demande d'un badge admin |
| Compteur > 254 | Remis à 0 |

### Badges à UID long (7 ou 10 octets)

Seuls 4 octets sont mémorisés. Par défaut (`UID_LONG_4_DERNIERS = false`), les 4 **premiers** ; avec `true`, les 4 **derniers** — préférable si vous n'utilisez que des UID longs (le 1er octet est souvent l'identifiant fabricant, ex. `04` pour NXP). Changer ce réglage oblige à réenregistrer les badges à UID long.

## Réglages

Constantes regroupées en tête du programme (durées en ms) :

| Constante | Valeur | Rôle |
|---|---|---|
| `DUREE_OUVERTURE_BADGE` | 10000 | Ouverture après un badge autorisé |
| `DUREE_OUVERTURE_BP_MIN` | 5000 | Ouverture minimale par le bouton |
| `DUREE_OUVERTURE_BP_MAX` | 9000 | Fermeture de sécurité bouton coincé |
| `DUREE_MESSAGE` | 2000 | Durée des messages LCD |
| `TIMEOUT_PROGRAMMATION` | 20000 | Sortie auto du mode programmation |
| `ANTI_REBOND` | 50 | Stabilité exigée du bouton d'ouverture |
| `PAS_LEDS` | 200 | Vitesse du chenillard / clignotements |
| `UID_LONG_4_DERNIERS` | false | Octets conservés pour les UID longs |

- **Portée de lecture** : décommenter `mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);` dans `setup()`.
- **Adresse LCD** : en cas de souci de détection, remplacer `hd44780_I2Cexp lcd;` par `hd44780_I2Cexp lcd(0x27);` (ou `0x3F`).
- ⚠️ Ne pas modifier les constantes de la partie EEPROM (`ADR_…`, `CODE_ADMIN`, `TAILLE_UID`) : la base existante ne serait plus lue.

## Dépannage

| Symptôme | Cause probable | Solution |
|---|---|---|
| Écran vide, LED D13 qui clignote | LCD non détecté | Vérifier SDA/SCL (A4/A5), 5 V, GND, contraste |
| Caractères invisibles | Contraste | Régler le potentiomètre du module I2C |
| « Erreur Communication » | Lecteur RFID absent | Vérifier SPI (D10–D13), RST (D9), 3,3 V |
| Aucune réaction au badge | Badge trop loin ou 125 kHz | Présenter à plat à 1–3 cm ; 13,56 MHz uniquement |
| Badge enregistré refusé | Retiré, base effacée, réglage UID modifié | Contrôler avec `r`, réenregistrer |
| « Base version securisee detectee » | EEPROM issue de la version sécurisée | Définir un admin, réenregistrer les badges |
| Porte qui s'ouvre seule | Bouton D2 en court-circuit ou parasité | Contrôler le câblage, câble torsadé/blindé |
| Fermeture à 9 s bouton appuyé | Sécurité bouton coincé | Relâcher puis réappuyer |
| Reset à l'ouverture de la gâche | Chute de tension / parasites | Alimentation séparée + diode de roue libre |

## Version normale ou sécurisée

Cette **version normale** n'identifie le badge que par son UID, qui peut être copié sur un badge « clone » : elle convient à un usage domestique ou à un local sans enjeu particulier. Une **version sécurisée** existe, qui écrit une clé et un compteur signé dans le badge pour détecter les copies. Les deux versions n'utilisent pas le même format d'EEPROM.

Pour le détail complet, voir le **manuel d'utilisation** (PDF) du projet.
