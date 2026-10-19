# Contrôle d'accès par badges RFID

Programme Arduino de contrôle d'accès : un lecteur **MFRC522** identifie des badges RFID, un écran **LCD 20×4 I2C** affiche les messages, et une sortie commande une gâche ou une serrure électrique. Les badges autorisés sont mémorisés dans l'**EEPROM** de l'Arduino et sont conservés après une coupure de courant ou un nouveau téléversement.

Fichier : `Autorisation_Ouverture_et_memorisation_avec_HD44780.ino`

## Fonctionnalités

- Ouverture de la porte pendant 10 s avec un badge autorisé.
- Refus d'accès pour un badge inconnu.
- **Badge administrateur** pour ajouter ou retirer des badges (mode programmation).
- Sortie automatique du mode programmation après 30 s sans activité.
- **Bouton d'ouverture** intérieur : 3 s minimum, tant qu'il est appuyé, 30 s maximum (sécurité si le bouton est coincé).
- **Bouton RAZ** pour effacer toute la base au démarrage, avec compte à rebours d'annulation.
- Jusqu'à **254 badges** (plus le badge administrateur).
- Fonctionnement **non bloquant** (`millis()`) : lecteur, bouton, LED et écran restent actifs en permanence.
- Signalisation par LED tricolore, buzzer et écran LCD.
- Journal de fonctionnement sur le moniteur série (9600 bauds).

## Matériel

| Élément | Remarque |
|---|---|
| Arduino Uno (ATmega328P) | ou compatible |
| Lecteur RFID MFRC522 | badges 13,56 MHz, alimenté en **3,3 V** |
| Écran LCD 20×4 avec module I2C (PCF8574) | adresse détectée automatiquement |
| 3 LED (rouge, verte, bleue) | avec résistance 220 à 330 Ω |
| Buzzer | |
| 2 boutons-poussoirs | ouverture et RAZ |
| Gâche ou serrure électrique | via relais ou transistor |

## Câblage

| Élément | Broche |
|---|---|
| Bouton d'ouverture (vers GND) | D2 |
| Bouton RAZ de la base (vers GND) | D3 |
| Commande serrure (HIGH = ouverte) | D4 |
| LED bleue | D5 |
| LED verte | D6 |
| LED rouge | D7 |
| Buzzer | D8 |
| MFRC522 RST | D9 |
| MFRC522 SDA (SS) | D10 |
| MFRC522 MOSI | D11 |
| MFRC522 MISO | D12 |
| MFRC522 SCK | D13 |
| MFRC522 VCC | **3,3 V** |
| LCD SDA / SCL | A4 / A5 |

> ⚠️ **Le MFRC522 s'alimente en 3,3 V**, jamais en 5 V.
>
> ⚠️ **La broche D4 ne doit jamais alimenter directement la gâche.** Utilisez un module relais, ou un transistor/MOSFET avec une diode de roue libre, et une alimentation séparée pour la gâche.

Les boutons utilisent les résistances de tirage internes (`INPUT_PULLUP`) : aucune résistance externe n'est nécessaire.

## Installation

1. Installez l'**IDE Arduino** (2.x conseillé).
2. Dans le gestionnaire de bibliothèques, installez :
   - **MFRC522** (GithubCommunity)
   - **hd44780** (Bill Perry)

   `EEPROM`, `SPI` et `Wire` sont fournies avec l'IDE.
3. Placez le fichier `.ino` dans un dossier portant **le même nom** : `Autorisation_Ouverture_et_memorisation_avec_HD44780/`.
4. Sélectionnez la carte **Arduino Uno** et le port, puis téléversez.

## Utilisation

### Première mise en service

Au premier démarrage, l'écran affiche *« Pas de Badge Administrateur »* et la LED bleue clignote. **Le premier badge présenté devient le badge administrateur.** Gardez-le en lieu sûr : il n'ouvre pas la porte, mais il permet d'enregistrer n'importe quel badge.

### Au quotidien

| Action | Résultat |
|---|---|
| Badge autorisé | Porte ouverte 10 s, LED verte, mélodie |
| Badge inconnu | Accès refusé, LED rouge, deux bips graves |
| Bouton d'ouverture | Porte ouverte (3 s minimum, puis tant qu'il est appuyé, 30 s maximum) |

### Ajouter ou retirer des badges

1. Présentez le **badge administrateur** : l'écran affiche *« MODE PROGRAMMATION »* et le nombre de badges en base, et les LED défilent.
2. Présentez un badge :
   - **inconnu** → il est **ajouté** (LED verte clignote 3 fois) ;
   - **déjà connu** → il est **retiré** (LED bleue clignote 3 fois).
3. Représentez le badge administrateur pour sortir, ou attendez **30 s** sans activité.

> Présenter deux fois de suite le même badge l'ajoute puis le retire. Vérifiez le compteur affiché après chaque badge.

### Effacer toute la base (RAZ)

1. Appuyez sur le **bouton RAZ** (D3) et maintenez-le.
2. Faites un **RESET** de la carte sans relâcher le bouton.
3. Gardez le bouton appuyé pendant le compte à rebours de **5 s**.

Tous les badges, **y compris le badge administrateur**, sont effacés, puis le système demande un nouveau badge administrateur. Pour annuler, relâchez le bouton avant la fin du compte à rebours.

C'est aussi la procédure à suivre en cas de **perte du badge administrateur**.

## Signalisation

| Situation | LED | Buzzer |
|---|---|---|
| Attente d'un badge | Bleue fixe | — |
| Mode programmation | Défilement vert / bleu / rouge | — |
| Attente du badge administrateur | Bleue clignotante | — |
| Accès autorisé | Verte fixe | Mélodie montante |
| Porte refermée | Rouge 2 s | — |
| Accès refusé | Rouge 2 s | Deux bips graves |
| Ouverture par bouton | Verte fixe | Un bip |
| Badge ajouté | Verte, 3 clignotements | Bip aigu |
| Badge retiré | Bleue, 3 clignotements | Bip |
| Base pleine | Rouge, 3 clignotements | — |
| Lecteur RFID non détecté | Rouge fixe, système arrêté | — |

## Réglages

Les réglages se trouvent en tête du programme, dans la section « Réglages ». Les durées sont en millisecondes.

| Constante | Défaut | Rôle |
|---|---|---|
| `DUREE_OUVERTURE_BADGE` | 10000 | Durée d'ouverture après un badge autorisé |
| `DUREE_OUVERTURE_BP_MIN` | 3000 | Durée minimale d'ouverture par le bouton |
| `DUREE_OUVERTURE_BP_MAX` | 30000 | Durée maximale si le bouton reste appuyé |
| `DUREE_MESSAGE` | 2000 | Durée d'affichage des messages |
| `TIMEOUT_PROGRAMMATION` | 30000 | Sortie automatique du mode programmation |
| `ANTI_REBOND` | 50 | Filtrage du bouton d'ouverture |
| `PAS_LEDS` | 200 | Vitesse de défilement et de clignotement des LED |
| `UID_LONG_4_DERNIERS` | `true` | Traitement des badges à UID de 7 octets |

### `UID_LONG_4_DERNIERS`

Le programme mémorise **4 octets** par badge. Les badges MIFARE Classic ont un UID de 4 octets ; les NTAG et Ultralight en ont 7.

- `true` (conseillé) : pour un UID de 7 octets, les **4 derniers** octets sont gardés. Ils distinguent mieux les badges, car le premier octet est le code fabricant, commun à beaucoup de badges.
- `false` : les **4 premiers** octets sont gardés, ce qui risque de confondre deux badges d'un même lot.

L'option n'a **aucun effet** sur les badges de 4 octets, ni sur la capacité de la base. Si vous la changez alors que des badges de 7 octets sont enregistrés, faites un RAZ et réenregistrez-les.

## Organisation de l'EEPROM

| Adresse | Contenu |
|---|---|
| `0x00` | Nombre de badges autorisés (hors administrateur) |
| `0x01` | `0x80` (128) si un badge administrateur est enregistré |
| `0x02`–`0x05` | UID du badge administrateur |
| `0x06`… | UID des badges autorisés, 4 octets chacun |

```
0x00  0x01  0x02 0x03 0x04 0x05  0x06 0x07 0x08 0x09
 01    80    86   32   3C   1F    B7   C9   47   1B
```

La capacité est de **254 badges** : (1024 − 6) / 4, avec un compteur sur un octet.

Pour consulter la base sans la modifier, utilisez le programme compagnon **`Lecture_EEPROM_Badges.ino`** (moniteur série à 115200 bauds). Un téléversement n'efface pas l'EEPROM : les badges sont conservés en passant d'un programme à l'autre.

## Moniteur série

À **115200 bauds**, le programme affiche notamment la version du lecteur, l'UID du badge administrateur, le nombre de badges, et pour chaque badge présenté sa taille et son UID complet :

```
Badge scanne (4 octets) UID: B7 C9 47 1B
Bonjour, Acces Autorise - Ouverture de la Porte
```

## Dépannage

| Symptôme | Solution |
|---|---|
| « Erreur Communication / Lecteur RFID » | Vérifier le câblage SPI (D9 à D13), le 3,3 V et la masse du MFRC522 |
| Écran vide ou rempli de carrés | Régler le contraste du module I2C ; vérifier A4/A5 |
| Badge autorisé refusé | Vérifier la base ; si `UID_LONG_4_DERNIERS` a changé, réenregistrer les badges de 7 octets |
| Badge administrateur perdu ou non reconnu | Faire un RAZ |
| Badge présenté sans effet | Attendre la fin du message (2 s), éloigner puis représenter le badge |
| Le RAZ ne se déclenche pas | Appuyer sur RAZ **avant** le RESET et maintenir 5 s |
| L'Arduino redémarre à l'ouverture | Étage de commande insuffisant : relais ou transistor, diode de roue libre, alimentation séparée |

## Sécurité et limites

- Un badge est identifié par son seul UID, qui peut être copié sur une carte « clone ». Ce montage convient à un garage, un atelier ou un local, pas à la protection de biens de valeur.
- Prévoyez une ouverture mécanique de secours, et choisissez le type de gâche selon le comportement souhaité en cas de coupure de courant.
- Le programme n'écrit dans l'EEPROM que lorsqu'une valeur change, ce qui préserve sa durée de vie (environ 100 000 écritures par case).

## Documentation

- `Manuel_Controle_Acces_RFID.pdf` : manuel complet d'utilisation.
- `Manuel_Lecture_EEPROM_Badges.pdf` : manuel du programme de lecture de la base.

## Dépendances

- [MFRC522](https://github.com/miguelbalboa/rfid)
- [hd44780](https://github.com/duinoWitchery/hd44780)
- EEPROM, SPI, Wire (fournies avec l'IDE Arduino)
