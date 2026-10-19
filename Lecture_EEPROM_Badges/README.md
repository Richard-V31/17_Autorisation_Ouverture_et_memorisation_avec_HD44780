# Lecture de la base de badges (EEPROM)

Programme Arduino de **consultation** de la base de badges enregistrée dans l'EEPROM par le programme de contrôle d'accès `Autorisation_Ouverture_et_memorisation_avec_HD44780.ino`.

Fichier : `Lecture_EEPROM_Badges.ino`

> ✅ **Lecture seule** : ce programme n'écrit jamais dans l'EEPROM. Il peut être utilisé sans aucun risque pour la base de badges.

## Fonctionnalités

- Indique si un badge administrateur est enregistré et affiche son UID.
- Affiche le nombre de badges autorisés et la liste de leurs UID, avec leur adresse en EEPROM.
- Affiche la taille de l'EEPROM et la capacité maximale de la base (254 badges sur Uno).
- Vidage brut complet de l'EEPROM en hexadécimal.
- Détection d'anomalies : compteur incohérent, badge identique au badge administrateur.

## Matériel

Seule la **carte Arduino** est nécessaire, reliée à l'ordinateur en USB. Le lecteur RFID, l'écran et le reste du montage peuvent rester branchés : ils ne sont pas utilisés.

> L'EEPROM appartient à la carte : utilisez **la même carte Arduino** que celle du contrôle d'accès.

Aucune bibliothèque à installer : seule `EEPROM.h`, fournie avec l'IDE Arduino, est utilisée.

## Utilisation

1. Placez le fichier dans un dossier nommé **`Lecture_EEPROM_Badges/`** (exigence de l'IDE Arduino).
2. Ouvrez-le dans l'IDE, choisissez la carte et le port, puis **téléversez**.
3. Ouvrez le moniteur série (*Outils › Moniteur série*) à **9600 bauds**.
4. Le contenu de la base s'affiche au démarrage. Sinon, envoyez `r` ou appuyez sur RESET.
5. Une fois la consultation terminée, **retéléversez le programme de contrôle d'accès**.

> Un téléversement n'efface pas l'EEPROM : **tous les badges sont conservés** en passant d'un programme à l'autre.

## Commandes

À taper dans la zone d'envoi du moniteur série, puis Entrée (majuscules acceptées) :

| Commande | Action |
|---|---|
| `r` | Relit et affiche la base de badges |
| `d` | Affiche le vidage brut des 1024 octets de l'EEPROM, 16 octets par ligne |

## Exemple d'affichage

```
==========================================
   CONTENU DE LA BASE DE BADGES (EEPROM)
==========================================
Taille EEPROM          : 1024 octets
Capacite maximale      : 254 badges

Indicateur admin (0x01): 0x80  -> badge admin enregistre
UID badge ADMIN        : 86 32 3C 1F

Nombre de badges (0x00): 2

 No   Adresse   UID
----  -------   -----------
  1   0x0006    B7 C9 47 1B
  2   0x000A    5E 01 9A 3C
==========================================
Envoyer "r" pour relire, "d" pour le vidage brut
```

| Ligne | Signification |
|---|---|
| Indicateur admin (0x01) | `0x80` : badge administrateur présent. Autre valeur : aucun |
| UID badge ADMIN | Les 4 octets d'identifiant du badge administrateur |
| Nombre de badges (0x00) | Nombre de badges autorisés, hors administrateur |
| No / Adresse / UID | Numéro d'ordre, emplacement en EEPROM et identifiant de chaque badge |

## Vidage brut (`d`)

```
====== VIDAGE BRUT DE L'EEPROM ======
Adresse  00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F
 0x0000  02 80 86 32 3C 1F B7 C9 47 1B 5E 01 9A 3C 00 00
 0x0010  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
...
```

Ici : `02` = deux badges, `80` = badge administrateur présent, `86 32 3C 1F` = badge administrateur, puis les deux badges autorisés. Après un RAZ, les octets valent `00` ; sur une carte neuve jamais utilisée, ils valent `FF`.

## Organisation de l'EEPROM lue

| Adresse | Contenu |
|---|---|
| `0x00` | Nombre de badges autorisés (hors administrateur) |
| `0x01` | `0x80` (128) si un badge administrateur est enregistré |
| `0x02`–`0x05` | UID du badge administrateur |
| `0x06`… | UID des badges autorisés, 4 octets chacun |

## Messages d'anomalie

| Message | Signification |
|---|---|
| `-> AUCUN badge admin enregistre` | Base vide ou effacée : le contrôle d'accès demandera un badge administrateur au prochain démarrage |
| `Aucun badge autorise dans la base.` | Compteur à 0 : aucun badge n'ouvre la porte |
| `!! Compteur INCOHERENT` | Le compteur dépasse la capacité (255 sur carte neuve, ou base abîmée). La liste n'est pas affichée ; utilisez `d`. Le contrôle d'accès remet ce compteur à 0 à son démarrage |
| `(identique au badge ADMIN !)` | Un badge de la liste a le même UID que le badge administrateur. Il ne peut pas être retiré en mode programmation : faites un RAZ et réenregistrez les badges |

## Retrouver un badge physique

Pour savoir à quel badge correspond un UID, présentez-le au lecteur avec le **programme de contrôle d'accès** en fonctionnement : le moniteur série affiche son UID complet. Si l'option `UID_LONG_4_DERNIERS` vaut `true`, un badge de 7 octets est mémorisé avec ses **4 derniers** octets.

## Dépannage

| Symptôme | Solution |
|---|---|
| Caractères incompréhensibles | Régler le moniteur série sur 9600 bauds |
| Rien ne s'affiche | Vérifier le port, envoyer `r`, ou appuyer sur RESET avec le moniteur ouvert |
| Les commandes ne répondent pas | Vérifier la saisie dans la zone d'envoi du moniteur (touche Entrée) |
| Les UID ne correspondent pas aux badges | Vérifier qu'il s'agit de la carte du contrôle d'accès ; tenir compte de `UID_LONG_4_DERNIERS` pour les badges de 7 octets |

## Documentation

- `Manuel_Lecture_EEPROM_Badges.pdf` : manuel complet de ce programme.
- `Manuel_Controle_Acces_RFID.pdf` et `README.md` du programme de contrôle d'accès.
