/* ============================================================================
 *  Contrôle d'accès RFID SÉCURISÉ (MFRC522) + LCD 20x4 I2C (librairie hd44780)
 *  Version « secret + compteur » pour badges MIFARE Classic 1K/4K et Arduino Uno R3
 *  + affichage du contenu de la base sur le moniteur série (ex-Lecture_EEPROM_Badges_Securise)
 *
 *  En plus de l'UID, chaque badge contient dans son secteur 1 :
 *    - une clé d'accès propre au badge (calculée à partir de son UID et de la
 *      CLÉ MAÎTRE du programme) : un clone qui ne copie que l'UID est refusé ;
 *    - un compteur de passages signé (MAC) : une copie complète du badge est
 *      détectée dès que l'original et la copie servent tous les deux ;
 *    - un numéro d'inscription aléatoire, inclus dans la signature : chaque
 *      réenregistrement du badge rend inutilisables ses anciennes copies.
 *
 *  - À la 1re mise en service, le 1er badge compatible lu devient le BADGE
 *    ADMINISTRATEUR (il est protégé de la même façon).
 *  - Passer le badge admin : entrée en mode PROGRAMMATION.
 *      Badge inconnu  -> il est préparé (clé + compteur) et ajouté à la base
 *      Badge connu    -> il est remis en clé d'usine et retiré de la base
 *                        (c'est aussi la façon de réactiver un badge BLOQUÉ :
 *                         le retirer, puis le représenter pour le réenregistrer)
 *  - Sortie du mode programmation : nouveau passage du badge admin,
 *    ou automatiquement après 30 s sans action.
 *  - Effacement de la base : appuyer sur le BP RAZ (D3 à la masse), faire un
 *    RESET de la carte et garder le BP appuyé pendant le compte à rebours (5 s).
 *    Les badges gardent leur clé : ils peuvent être réenregistrés ensuite.
 *  - BP d'ouverture (D2 à la masse) : ouverture forcée de la porte
 *    (5 s minimum, tant que le BP est appuyé, 70 s maximum).
 *
 *  MONITEUR SÉRIE (115200 bauds) :
 *  - À l'ouverture du moniteur, la Uno redémarre : le contenu de la base
 *    (badge admin, liste des badges, passages, état) s'affiche à la fin du démarrage.
 *    (la serrure reste fermée pendant ce redémarrage)
 *  - Commandes à envoyer à tout moment (traitées quand le lecteur est en attente) :
 *      r : réafficher le contenu de la base
 *      d : vidage brut complet de l'EEPROM (1024 octets en hexadécimal)
 *      h : rappel des commandes
 *    Ces commandes ne font que LIRE l'EEPROM.
 *
 *  !!! AVANT D'ENREGISTRER LE PREMIER BADGE : personnaliser CLE_MAITRE !!!
 *  !!! puis la conserver précieusement et ne plus jamais la modifier.   !!!
 *  (si elle change, les badges déjà préparés ne sont plus reconnus)
 *
 *  Organisation de l'EEPROM (format sécurisé, différent de l'ancienne version) :
 *    0x00        : nombre de badges enregistrés (hors badge admin)
 *    0x01        : 0xA5 si un badge admin est enregistré (format sécurisé)
 *    0x02 - 0x05 : UID du badge admin
 *    0x06 - 0x07 : compteur du badge admin
 *    0x08 - 0x09 : numéro d'inscription du badge admin
 *    0x10 ...    : badges, 9 octets chacun : UID (4) + compteur (2)
 *                  + numéro d'inscription (2) + état (1 : 0 = actif, 1 = bloqué)
 *  Capacité : 112 badges.
 *
 *  Contenu du badge, secteur 1 :
 *    bloc 4 : 'C' 'A' + compteur (2) + n° d'inscription (2) + MAC (8) + 2 octets à 0
 *    bloc 7 : clé A (6) + droits d'accès FF 07 80 69 + clé B (6)
 *
 *  Câblage MFRC522 (Arduino Uno) :
 *    VCC : 3.3V ATTENTION   RST : D9   SS : D10
 *    MOSI : D11   MISO : D12   SCK : D13   IRQ : non connecté
 * ========================================================================== */

#include <EEPROM.h>                          // accès à la mémoire EEPROM interne (1024 octets sur Uno), conservée hors tension
#include <SPI.h>                             // bus SPI utilisé pour dialoguer avec le lecteur MFRC522
#include <MFRC522.h>                         // librairie du lecteur RFID MFRC522
#include <Wire.h>                            // bus I2C utilisé pour dialoguer avec le LCD
#include <hd44780.h>                         // librairie de base pour les écrans LCD HD44780
#include <hd44780ioClass/hd44780_I2Cexp.h>   // variante « LCD derrière un module I2C » (PCF8574)

// ------------------------------- Broches ------------------------------------
const byte PIN_BUZZ         = 8;   // buzzer (piloté avec tone())
const byte PIN_LED_ROUGE    = 7;   // LED rouge : refus / porte fermée / alerte
const byte PIN_LED_VERTE    = 6;   // LED verte : accès autorisé / porte ouverte
const byte PIN_LED_BLEUE    = 5;   // LED bleue : attente de badge
const byte PIN_SERRURE      = 4;   // commande de la serrure (HIGH = ouverte) via relais/transistor
const byte PIN_BP_RAZ       = 3;   // BP effacement base -> GND
const byte PIN_BP_OUVERTURE = 2;   // BP ouverture forcée -> GND
const byte PIN_RC522_SS     = 10;  // broche SS (sélection du composant SPI) du MFRC522
const byte PIN_RC522_RST    = 9;   // broche RST (reset) du MFRC522

// ------------------------------- Réglages -----------------------------------
const unsigned long DUREE_OUVERTURE_BADGE  = 10000UL; // ms (10 s) : temps d'ouverture après un badge valide
const unsigned long DUREE_OUVERTURE_BP_MIN = 5000UL;  // ms : 5s ouverture minimale par le BP, même si appui bref
const unsigned long DUREE_OUVERTURE_BP_MAX = 9000UL; // ms, 9s sécurité si BP coincé
const unsigned long DUREE_MESSAGE          = 2000UL;  // durée 2s des messages LCD
const unsigned long DUREE_MESSAGE_ALERTE   = 4000UL;  // 4s badge suspect / bloqué
const unsigned long TIMEOUT_PROGRAMMATION  = 20000UL; // 20s sortie auto du mode prog
const unsigned long ANTI_REBOND            = 50UL;    // ms : le BP doit être stable 50 ms pour être pris en compte
const unsigned long PAS_LEDS               = 200UL;   // ms, cycle / clignotement

// Badges à UID long : garder les 4 DERNIERS octets (true) ou les 4 premiers.
// (sans effet sur les MIFARE Classic à UID de 4 octets)
const bool UID_LONG_4_DERNIERS = true;   // la base ne stocke que 4 octets d'UID par badge

// ------------------------------- Sécurité -----------------------------------
// CLÉ MAÎTRE : 16 octets secrets. À PERSONNALISER avant le premier enregistrement
// (remplacer par vos propres valeurs au hasard), à conserver, à ne plus changer.
// PROGMEM : la clé est rangée en mémoire Flash (programme) et non en RAM.
const byte CLE_MAITRE[16] PROGMEM = {
  0xF8, 0x9B, 0xE3, 0x3B, 0x35, 0xA2, 0x18, 0xC0,   // octets 0 à 7 de la clé (-> k0 de SipHash)
  0x3D, 0xA2, 0x1D, 0x0B, 0xEB, 0xAE, 0xCB, 0xE2    // octets 8 à 15 de la clé (-> k1 de SipHash)
};
const byte SECTEUR_BADGE = 1;                       // secteur utilisé sur le badge
const byte BLOC_DONNEES  = SECTEUR_BADGE * 4;       // bloc 4 : 1er bloc du secteur 1 (compteur + signature)
const byte BLOC_VERROU   = SECTEUR_BADGE * 4 + 3;   // bloc 7 : dernier bloc du secteur = « trailer » (clés + droits)
const uint16_t AVANCE_MAX = 3;  // avance tolérée du compteur du badge (badge retiré trop tôt)
// Badge détecté au bord du champ (pendant qu'on l'approche) : l'UID est lu mais
// l'échange avec le secteur échoue. On réessaie alors sans rien afficher.
const byte ESSAIS_LECTURE = 6;            // nombre de tentatives
const unsigned long PAUSE_ESSAI = 60UL;   // ms entre deux tentatives (6 x 60 ms = 0,4 s)

// ------------------------------- EEPROM -------------------------------------
// L'EEPROM de l'Uno fait 1024 octets (adresses 0x000 à 0x3FF). Chaque adresse
// contient UN octet (0 à 255). Elle garde son contenu sans alimentation, c'est
// donc là qu'est rangée la base des badges. Les constantes ci-dessous donnent
// l'ADRESSE où se trouve chaque information (plan mémoire) :
//
//   Adresse  | Contenu
//   ---------+----------------------------------------------------------------
//   0x00     | nombre de badges utilisateurs enregistrés (0 à 112)
//   0x01     | drapeau : 0xA5 = un badge admin valide est enregistré
//   0x02-05  | UID du badge admin (4 octets)
//   0x06-07  | compteur de passages du badge admin (16 bits, octet faible d'abord)
//   0x08-09  | numéro d'inscription du badge admin (16 bits)
//   0x0A-0F  | inutilisés (réserve)
//   0x10 ... | fiches des badges, 9 octets chacune, l'une après l'autre
//Autorisation_Ouverture_Securisee_HD44780.ino. pour Arduino UNO R3

//Section EEPROM :  Voici ce qu’il faut retenir :
//Une adresse contient un octet. C’est pourquoi le compteur et le numéro d’inscription, codés sur 16 bits, occupent chacun 2 adresses, avec l’octet faible en premier (fonctions lire16 et ecrire16).
//Zone admin, de 0x00 à 0x09. Elle contient le nombre de badges, le drapeau, puis l’UID, le compteur et l’inscription du badge admin. Les adresses 0x0A à 0x0F restent libres en réserve.
//Fiches badges, à partir de 0x10. Chaque fiche fait 9 octets : UID (4), compteur (2), inscription (2) et état (1). La fiche n°1 commence en 0x10, la n°2 en 0x19, la n°3 en 0x22, etc.
//Capacité. (1024 − 16) / 9 donne 112 fiches.
//CODE_ADMIN = 0xA5. Ce motif binaire, 10100101, ne peut pas apparaître par hasard dans une EEPROM vierge (remplie de 0xFF) ou effacée (remplie de 0x00).
//CODE_ANCIEN = 128. Cette valeur permet de reconnaître une base créée par l’ancienne version du programme et de la remettre à zéro.

// Une fiche de badge (9 octets) :
//   +0..+3 : UID   +4..+5 : compteur   +6..+7 : n° d'inscription   +8 : état
// Fiche n°1 en 0x10, fiche n°2 en 0x19, fiche n°3 en 0x22, ...
// Capacité : (1024 - 16) / 9 = 112 fiches (division entière).
const int  ADR_NB_BADGES     = 0x00;   // adresse 0 : nombre de badges enregistrés (hors admin)
const int  ADR_FLAG_ADMIN    = 0x01;   // adresse 1 : drapeau « admin enregistré / format de la base »
const int  ADR_UID_ADMIN     = 0x02;   // adresses 2 à 5 : les 4 octets d'UID du badge admin
const int  ADR_CPT_ADMIN     = 0x06;   // adresses 6 et 7 : compteur de passages du badge admin
const int  ADR_INSCR_ADMIN   = 0x08;   // adresses 8 et 9 : numéro d'inscription du badge admin
const int  ADR_PREMIER_BADGE = 0x10;   // adresse 16 : début de la 1re fiche badge (0x0A-0x0F laissés libres)
const byte CODE_ADMIN        = 0xA5;   // valeur du drapeau quand la base est au format sécurisé
                                       // (0xA5 = 10100101 : motif peu probable dans une EEPROM vierge,
                                       //  qui contient 0xFF, ou effacée, qui contient 0x00)
const byte CODE_ANCIEN       = 128;    // valeur du drapeau de l'ancienne version (UID seul) :
                                       // si on la trouve, la base est remise à zéro (format incompatible)
const byte TAILLE_UID        = 4;      // nombre d'octets d'UID mémorisés par badge
const byte TAILLE_ENTREE     = 9;      // UID 4 + compteur 2 + inscription 2 + état 1 = taille d'une fiche
const byte ETAT_ACTIF        = 0;      // valeur de l'octet « état » : badge utilisable
const byte ETAT_BLOQUE       = 1;      // valeur de l'octet « état » : badge bloqué (copie détectée)

// ------------------------------- Objets -------------------------------------
MFRC522 mfrc522(PIN_RC522_SS, PIN_RC522_RST);   // objet lecteur RFID (broches SS et RST)
hd44780_I2Cexp lcd;   // adresse I2C détectée automatiquement

// ------------------------------- Variables ----------------------------------
byte readCard[TAILLE_UID];    // UID du badge qui vient d'être lu
byte masterCard[TAILLE_UID];  // UID du badge administrateur

enum Etat { ATTENTE, MESSAGE, OUVERT_BADGE, OUVERT_BP };   // états possibles de l'automate principal
Etat etat = ATTENTE;                         // état courant (au départ : attente d'un badge)
unsigned long debutEtat = 0;                 // instant (millis) d'entrée dans l'état courant
unsigned long dureeMessage = DUREE_MESSAGE;  // durée d'affichage du message en cours

bool modeProg = false;                       // true = mode programmation actif
unsigned long derniereActionProg = 0;        // instant de la dernière action en mode prog (pour le timeout)

bool bpDoitEtreRelache = false;  // empêche une réouverture si le BP est resté coincé

byte ledClignote = 0;                // broche de la LED qui clignote (0 = aucune)
byte nbBasculements = 0;             // nombre de changements d'état restants (2 par clignotement)
bool etatLedClignote = false;        // état actuel (allumée/éteinte) de la LED qui clignote
unsigned long dernierBasculement = 0;// instant du dernier changement d'état de la LED

// Résultats des opérations sur le badge
enum Resultat : byte {
  R_OK,            // opération réussie
  R_ECHEC,         // authentification ou lecture/écriture impossible (badge retiré, clone…)
  R_FAUX,          // données du badge invalides (non authentique)
  R_SUSPECT,       // compteur en retard : copie détectée
  R_INCOMPATIBLE,  // pas un MIFARE Classic
  R_CLE_INCONNUE   // MIFARE Classic, mais ni clé d'usine ni notre clé
};


// ============================================================================
//                         SipHash-2-4 (clés et signature)
// ============================================================================
// Rotation à gauche de b bits d'un nombre de 64 bits (les bits sortis à gauche reviennent à droite)
static inline uint64_t rotl64(uint64_t x, byte b) { return (x << b) | (x >> (64 - b)); }

// Lit 8 octets en mémoire et les assemble en un nombre 64 bits « little-endian » (octet faible en premier)
static uint64_t lireLE64(const byte *p) {
  uint64_t v = 0;                                     // résultat, initialisé à 0
  for (int8_t i = 7; i >= 0; i--) v = (v << 8) | p[i];// on part de l'octet de poids fort (p[7]) vers p[0]
  return v;                                           // renvoie le nombre 64 bits
}

// Un « tour » de mélange SipHash (additions, rotations, OU exclusifs sur v0..v3).
// (pas de commentaire // dans la macro : le '\' de fin de ligne le prolongerait)
#define SIPROUND do { \
  v0 += v1; v1 = rotl64(v1, 13); v1 ^= v0; v0 = rotl64(v0, 32); \
  v2 += v3; v3 = rotl64(v3, 16); v3 ^= v2;                      \
  v0 += v3; v3 = rotl64(v3, 21); v3 ^= v0;                      \
  v2 += v1; v1 = rotl64(v1, 17); v1 ^= v2; v2 = rotl64(v2, 32); \
} while (0)

// Empreinte de 64 bits du message m, avec la clé maître
uint64_t siphash(const byte *m, byte len) {
  byte k[16];                                   // copie temporaire de la clé en RAM
  memcpy_P(k, CLE_MAITRE, 16);                  // lecture de la clé maître depuis la Flash
  uint64_t k0 = lireLE64(k), k1 = lireLE64(k + 8); // clé découpée en deux moitiés de 64 bits
  memset(k, 0, sizeof(k));                      // efface la copie en RAM (ne pas laisser traîner le secret)
  uint64_t v0 = 0x736f6d6570736575ULL ^ k0;     // initialisation de l'état interne (constantes SipHash
  uint64_t v1 = 0x646f72616e646f6dULL ^ k1;     //  « somepseudorandomlygeneratedbytes »)
  uint64_t v2 = 0x6c7967656e657261ULL ^ k0;     //  mélangées avec la clé
  uint64_t v3 = 0x7465646279746573ULL ^ k1;
  byte i = 0;                                   // position dans le message
  for (; i + 8 <= len; i += 8) {                // traite le message par paquets complets de 8 octets
    uint64_t mi = lireLE64(m + i);              // paquet courant en nombre 64 bits
    v3 ^= mi; SIPROUND; SIPROUND; v0 ^= mi;     // injection du paquet + 2 tours (le « 2 » de SipHash-2-4)
  }
  uint64_t b = ((uint64_t)len) << 56;           // dernier paquet : longueur du message dans l'octet de poids fort
  for (byte j = 0; i + j < len; j++) b |= ((uint64_t)m[i + j]) << (8 * j); // + octets restants (< 8)
  v3 ^= b; SIPROUND; SIPROUND; v0 ^= b;         // injection du dernier paquet + 2 tours
  v2 ^= 0xff; SIPROUND; SIPROUND; SIPROUND; SIPROUND; // finalisation : 4 tours (le « 4 » de SipHash-2-4)
  return v0 ^ v1 ^ v2 ^ v3;                     // empreinte = combinaison des 4 variables d'état
}

// domaine 1 = clé A, 2 = clé B, 3 = signature (MAC) du compteur et de l'inscription
void deriver(byte domaine, const byte uid[4], uint16_t cpt, uint16_t inscr, byte avecCpt,
             byte *sortie, byte n) {
  byte m[9] = { domaine, uid[0], uid[1], uid[2], uid[3],                    // message = domaine + UID
                (byte)cpt, (byte)(cpt >> 8), (byte)inscr, (byte)(inscr >> 8) }; // + compteur + inscription
  uint64_t h = siphash(m, avecCpt ? 9 : 5);     // 9 octets (avec compteur) ou 5 (domaine + UID seulement)
  for (byte i = 0; i < n; i++) { sortie[i] = (byte)h; h >>= 8; } // recopie les n premiers octets de l'empreinte
}


// ============================================================================
//                                   LEDs
// ============================================================================
void ledsOff() {                         // éteint les trois LEDs
  digitalWrite(PIN_LED_ROUGE, LOW);
  digitalWrite(PIN_LED_VERTE, LOW);
  digitalWrite(PIN_LED_BLEUE, LOW);
}

void allumerSeule(byte pin) {            // allume une seule LED, fixe, et arrête tout clignotement
  ledClignote = 0;                       // plus de LED en clignotement
  nbBasculements = 0;                    // plus de basculement en attente
  ledsOff();                             // éteint tout
  digitalWrite(pin, HIGH);               // allume la LED demandée
}

void lancerClignotement(byte pin, byte nbFois) { // programme nbFois clignotements de la LED pin
  ledsOff();                             // part de LEDs éteintes
  ledClignote = pin;                     // LED concernée
  nbBasculements = nbFois * 2;           // 1 clignotement = allumage + extinction
  etatLedClignote = false;               // LED actuellement éteinte
  dernierBasculement = millis();         // point de départ du chronométrage
}

void gererLeds() {                       // appelée à chaque loop() : anime les LEDs sans bloquer
  unsigned long maintenant = millis();   // heure courante

  if (etat == MESSAGE) {                 // pendant un message : on gère le clignotement éventuel
    if (ledClignote != 0 && nbBasculements > 0 &&            // une LED doit clignoter
        maintenant - dernierBasculement >= PAS_LEDS) {       // et 200 ms sont passées
      dernierBasculement = maintenant;   // nouveau point de départ
      etatLedClignote = !etatLedClignote;// inverse l'état
      digitalWrite(ledClignote, etatLedClignote); // applique sur la LED
      nbBasculements--;                  // un basculement de moins à faire
    }
    return;                              // rien d'autre à faire pendant un message
  }

  if (etat == ATTENTE) {                 // en attente de badge
    if (modeProg) {                      // mode programmation : chenillard vert -> bleu -> rouge
      byte phase = (maintenant / PAS_LEDS) % 3;   // phase 0, 1 ou 2, change toutes les 200 ms
      digitalWrite(PIN_LED_VERTE, phase == 0);
      digitalWrite(PIN_LED_BLEUE, phase == 1);
      digitalWrite(PIN_LED_ROUGE, phase == 2);
    } else {
      allumerSeule(PIN_LED_BLEUE);       // mode normal : bleu fixe
    }
  }
}


// ============================================================================
//                                   LCD
// ============================================================================
// Efface l'écran et affiche jusqu'à 4 lignes (textes en Flash via F(), nullptr = ligne vide)
void afficher(const __FlashStringHelper* l0, const __FlashStringHelper* l1,
              const __FlashStringHelper* l2, const __FlashStringHelper* l3) {
  const __FlashStringHelper* lignes[4] = { l0, l1, l2, l3 };  // regroupe les 4 lignes dans un tableau
  lcd.clear();                           // efface l'écran
  for (byte i = 0; i < 4; i++) {         // pour chacune des 4 lignes
    if (lignes[i] != nullptr) {          // si un texte est fourni
      lcd.setCursor(0, i);               // curseur en début de ligne i
      lcd.print(lignes[i]);              // affiche le texte
    }
  }
}

void afficherEcranAttente() {            // écran de repos (normal ou programmation)
  if (modeProg) {
    afficher(F("MODE PROGRAMMATION"),
             F("Passer un badge pour"),
             F("  Ajout ou Retrait"),
             nullptr);
    lcd.setCursor(0, 3);                 // 4e ligne
    lcd.print(F("Badges en base: "));
    lcd.print(EEPROM.read(ADR_NB_BADGES)); // nombre de badges lu à l'adresse 0x00 de l'EEPROM
  } else {
    afficher(F("--------------------"),
             F("      Attente"),
             F("   Lecture Badge"),
             F("--------------------"));
  }
}

void entrerMessageDuree(unsigned long duree) { // passe en état MESSAGE pour « duree » ms
  etat = MESSAGE;
  debutEtat = millis();                  // mémorise le début
  dureeMessage = duree;                  // mémorise la durée
}

void entrerMessage() {                   // message de durée standard (2 s)
  entrerMessageDuree(DUREE_MESSAGE);
}


// ============================================================================
//                                  Buzzer
// ============================================================================
void bipAutorise() {                     // arpège montant : accès autorisé
  const int notes[] = { 523, 783, 1046, 1568, 2093 };   // fréquences en Hz (do, sol, do, sol, do)
  for (byte i = 0; i < 5; i++) {
    tone(PIN_BUZZ, notes[i], 50);        // joue la note 50 ms
    delay(50);                           // attend la fin de la note
  }
}

void bipRefus() {                        // deux bips graves : refus
  tone(PIN_BUZZ, 370, 50);               // bip court
  delay(100);
  tone(PIN_BUZZ, 370, 300);              // bip long
}

void bipAlerte() {                       // trois bips aigus : alerte copie
  for (byte i = 0; i < 3; i++) {
    tone(PIN_BUZZ, 2500, 120);
    delay(200);
  }
}


// ============================================================================
//                           Base de badges (EEPROM)
// ============================================================================
int nbBadgesMax() {                      // nombre de fiches qui tiennent dans l'EEPROM
  int places = (EEPROM.length() - ADR_PREMIER_BADGE) / TAILLE_ENTREE; // (1024 - 16) / 9 = 112
  return (places > 254) ? 254 : places;  // limité à 254 car le nombre est stocké sur 1 octet
}

int adresseBadge(int slot) {                 // slot de 1 à nombre de badges
  return ADR_PREMIER_BADGE + (slot - 1) * TAILLE_ENTREE;   // ex. slot 3 -> 0x10 + 2*9 = 0x22
}

// Lecture/écriture d'un nombre 16 bits sur 2 adresses EEPROM (octet faible en premier).
// update() n'écrit que si la valeur change : économise l'EEPROM (~100 000 écritures par case).
uint16_t lire16(int adr) { return EEPROM.read(adr) | ((uint16_t)EEPROM.read(adr + 1) << 8); }
void ecrire16(int adr, uint16_t v) { EEPROM.update(adr, (byte)v); EEPROM.update(adr + 1, (byte)(v >> 8)); }

// Accès aux champs d'une fiche (décalages +4, +6, +8 dans la fiche de 9 octets)
uint16_t compteurBadge(int slot)            { return lire16(adresseBadge(slot) + 4); }    // compteur
void     ecrireCompteurBadge(int slot, uint16_t v) { ecrire16(adresseBadge(slot) + 4, v); }
uint16_t inscriptionBadge(int slot)        { return lire16(adresseBadge(slot) + 6); }     // n° d'inscription
byte     etatBadge(int slot)                { return EEPROM.read(adresseBadge(slot) + 8); } // état actif/bloqué
void     ecrireEtatBadge(int slot, byte e)  { EEPROM.update(adresseBadge(slot) + 8, e); }

// Numéro d'inscription « au hasard » (bruit de l'entrée analogique A0 + temps)
uint16_t nouvelleInscription() {
  uint16_t v = (uint16_t)micros();       // point de départ : l'horloge en µs
  for (byte i = 0; i < 8; i++) v = (v << 3) ^ (v >> 13) ^ analogRead(A0) ^ (uint16_t)micros(); // mélange avec le bruit de A0 (laisser A0 en l'air)
  return v;
}

bool checkTwo(const byte a[], const byte b[]) { // compare deux UID de 4 octets
  for (byte k = 0; k < TAILLE_UID; k++) {
    if (a[k] != b[k]) return false;      // un octet diffère : UID différents
  }
  return true;                           // tous identiques
}

// Renvoie le n° d'emplacement du badge (1..n), ou 0 s'il est absent
int findIDSLOT(const byte uid[]) {
  int count = EEPROM.read(ADR_NB_BADGES);        // nombre de fiches occupées
  for (int i = 1; i <= count; i++) {             // parcourt chaque fiche
    int adr = adresseBadge(i);                   // adresse de la fiche i
    bool egal = true;
    for (byte j = 0; j < TAILLE_UID; j++) {      // compare les 4 octets d'UID
      if (EEPROM.read(adr + j) != uid[j]) { egal = false; break; }
    }
    if (egal) return i;                          // trouvé
  }
  return 0;                                      // pas trouvé
}

bool isMaster(const byte uid[]) {                // true si l'UID est celui du badge admin
  return checkTwo(uid, masterCard);
}

// Ajoute un badge (déjà préparé) avec compteur 0. false si la base est pleine.
bool ajouterBadge(const byte uid[], uint16_t inscr) {
  int num = EEPROM.read(ADR_NB_BADGES);          // nombre actuel de badges
  if (num >= nbBadgesMax()) return false;        // plus de place
  int adr = adresseBadge(num + 1);               // adresse de la première fiche libre
  for (byte j = 0; j < TAILLE_UID; j++) EEPROM.update(adr + j, uid[j]); // UID
  ecrire16(adr + 4, 0);                          // compteur à 0
  ecrire16(adr + 6, inscr);                      // n° d'inscription
  EEPROM.update(adr + 8, ETAT_ACTIF);            // état actif
  EEPROM.update(ADR_NB_BADGES, num + 1);    // compteur en dernier (coupure secteur)
  return true;
}

// Retire l'entrée n° slot en décalant les suivantes
void supprimerEntree(int slot) {
  int num = EEPROM.read(ADR_NB_BADGES);          // nombre actuel de badges
  for (int s = slot; s < num; s++) {             // chaque fiche suivante remonte d'un cran
    int dest = adresseBadge(s);
    for (byte j = 0; j < TAILLE_ENTREE; j++) {   // copie les 9 octets de la fiche s+1 vers s
      EEPROM.update(dest + j, EEPROM.read(dest + TAILLE_ENTREE + j));
    }
  }
  int dernier = adresseBadge(num);               // la dernière fiche est maintenant en double
  for (byte j = 0; j < TAILLE_ENTREE; j++) EEPROM.update(dernier + j, 0); // on l'efface
  EEPROM.update(ADR_NB_BADGES, num - 1);         // un badge de moins
}


// ============================================================================
//               Moniteur série : affichage du contenu de la base
//               (reprise de Lecture_EEPROM_Badges_Securise, LECTURE SEULE)
// ============================================================================
void afficherHex(byte v) {                       // un octet en hexadécimal sur 2 caractères
  if (v < 0x10) Serial.print('0');               // zéro de tête (ex. 0A au lieu de A)
  Serial.print(v, HEX);
}

void afficherAdresse(int adr) {                  // adresse EEPROM au format 0x0000
  Serial.print(F("0x"));
  if (adr < 0x1000) Serial.print('0');
  if (adr < 0x100)  Serial.print('0');
  if (adr < 0x10)   Serial.print('0');
  Serial.print(adr, HEX);
}

void afficherUIDEeprom(int adr) {                // 4 octets d'UID lus en EEPROM à partir de adr
  for (byte i = 0; i < TAILLE_UID; i++) {
    afficherHex(EEPROM.read(adr + i));
    if (i < TAILLE_UID - 1) Serial.print(' ');
  }
}

void afficherNombre(unsigned long v, byte largeur) { // nombre aligné à droite sur « largeur » caractères
  unsigned long p = 1;
  for (byte i = 1; i < largeur; i++) p *= 10;    // p = 10^(largeur-1)
  while (p > 1 && v < p) { Serial.print(' '); p /= 10; } // espaces de remplissage
  Serial.print(v);
}

bool memeUIDEeprom(int a, int b) {               // compare deux UID rangés en EEPROM
  for (byte i = 0; i < TAILLE_UID; i++) if (EEPROM.read(a + i) != EEPROM.read(b + i)) return false;
  return true;
}

void afficherAideSerie() {                       // rappel des commandes du moniteur série
  Serial.println(F("Commandes : \"r\" = relire la base, \"d\" = vidage brut EEPROM, \"h\" = aide"));
}

void afficherBaseSerie() {                       // tableau complet de la base
  byte flag = EEPROM.read(ADR_FLAG_ADMIN);       // drapeau en 0x01
  int maxi = nbBadgesMax();                      // 112 sur une Uno
  byte nb = EEPROM.read(ADR_NB_BADGES);          // nombre de badges utilisateurs

  Serial.println();
  Serial.println(F("=================================================="));
  Serial.println(F("     CONTENU DE LA BASE DE BADGES (EEPROM)"));
  Serial.println(F("=================================================="));
  Serial.print(F("Taille EEPROM       : "));
  Serial.print(EEPROM.length());
  Serial.println(F(" octets"));
  Serial.print(F("Indicateur (0x01)   : 0x"));
  afficherHex(flag);
  Serial.println();
  Serial.print(F("Mode en cours       : "));
  Serial.println(modeProg ? F("PROGRAMMATION") : F("normal"));

  if (flag != CODE_ADMIN) {                      // pas (encore) de badge admin
    Serial.println(F("Aucun badge administrateur enregistre (base vide ou effacee)."));
    Serial.println(F("=================================================="));
    afficherAideSerie();
    return;
  }

  Serial.print(F("Capacite maximale   : "));
  Serial.print(maxi);
  Serial.println(F(" badges"));
  Serial.println();
  Serial.print(F("Badge ADMIN         : "));
  afficherUIDEeprom(ADR_UID_ADMIN);
  Serial.print(F("   compteur "));
  Serial.print(lire16(ADR_CPT_ADMIN));
  Serial.print(F("   inscription 0x"));
  afficherHex(EEPROM.read(ADR_INSCR_ADMIN + 1)); // octet fort d'abord pour la lecture
  afficherHex(EEPROM.read(ADR_INSCR_ADMIN));
  Serial.println();
  Serial.println();
  Serial.print(F("Nombre de badges    : "));
  Serial.println(nb);

  if (nb > maxi) {                               // valeur aberrante
    Serial.println(F("!! Compteur INCOHERENT : liste non affichee (envoyer \"d\")"));
  } else if (nb == 0) {
    Serial.println(F("Aucun badge autorise dans la base."));
  } else {
    Serial.println();
    Serial.println(F(" No   Adresse   UID           Passages  Inscr.  Etat"));
    Serial.println(F("----  -------   -----------   --------  ------  -----------------"));
    byte bloques = 0;                            // nombre de badges bloqués
    for (int n = 1; n <= nb; n++) {              // une ligne par fiche
      int adr = adresseBadge(n);
      afficherNombre(n, 3);
      Serial.print(F("   "));
      afficherAdresse(adr);
      Serial.print(F("    "));
      afficherUIDEeprom(adr);
      Serial.print(F("   "));
      afficherNombre(compteurBadge(n), 8);
      Serial.print(F("  0x"));
      afficherHex(EEPROM.read(adr + 7));         // inscription, octet fort
      afficherHex(EEPROM.read(adr + 6));         // inscription, octet faible
      Serial.print(F("  "));
      byte e = etatBadge(n);
      if (e == ETAT_ACTIF)       Serial.print(F("actif"));
      else if (e == ETAT_BLOQUE) { Serial.print(F("BLOQUE (copie detectee)")); bloques++; }
      else                       { Serial.print(F("inconnu (0x")); afficherHex(e); Serial.print(')'); }
      if (memeUIDEeprom(adr, ADR_UID_ADMIN)) Serial.print(F("  (identique au badge ADMIN !)"));
      Serial.println();
    }
    if (bloques) {
      Serial.println();
      Serial.print(bloques);
      Serial.println(F(" badge(s) BLOQUE(S) : pour reactiver, le retirer puis le"));
      Serial.println(F("reenregistrer en mode programmation."));
    }
  }
  Serial.println(F("=================================================="));
  afficherAideSerie();
}

void vidageBrutSerie() {                         // 1024 octets, 16 par ligne
  Serial.println();
  Serial.println(F("====== VIDAGE BRUT DE L'EEPROM ======"));
  Serial.println(F("Adresse  00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F"));
  for (unsigned int ligne = 0; ligne < EEPROM.length(); ligne += 16) {
    afficherAdresse(ligne);
    Serial.print(F("  "));
    for (byte i = 0; i < 16 && ligne + i < EEPROM.length(); i++) {
      afficherHex(EEPROM.read(ligne + i));
      Serial.print(' ');
    }
    Serial.println();
  }
  Serial.println(F("====================================="));
}

void gererCommandesSerie() {                     // lit les caractères reçus du moniteur série
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R')      afficherBaseSerie();
    else if (c == 'd' || c == 'D') vidageBrutSerie();
    else if (c == 'h' || c == 'H' || c == '?') afficherAideSerie();
    // les autres caractères (fin de ligne...) sont ignorés
  }
}


// ============================================================================
//                        Opérations sur le badge (secteur 1)
// ============================================================================
void afficherUID(const byte uid[], byte taille) { // affiche un UID en hexadécimal sur le moniteur série
  for (byte i = 0; i < taille; i++) {
    if (uid[i] < 0x10) Serial.print('0');        // zéro de tête (ex. 0A au lieu de A)
    Serial.print(uid[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
}

// Lit un nouveau badge (UID dans readCard). Le badge reste sélectionné :
// appeler terminerCarte() une fois le traitement fini.
bool getID() {
  if (!mfrc522.PICC_IsNewCardPresent()) return false; // aucun nouveau badge dans le champ
  if (!mfrc522.PICC_ReadCardSerial())   return false; // lecture de l'UID ratée

  byte taille = mfrc522.uid.size;                     // taille réelle de l'UID (4, 7 ou 10 octets)
  byte debut = (UID_LONG_4_DERNIERS && taille > TAILLE_UID) ? taille - TAILLE_UID : 0; // octets à garder

  Serial.print(F("Badge scanne ("));
  Serial.print(taille);
  Serial.print(F(" octets) UID: "));
  afficherUID(mfrc522.uid.uidByte, taille);

  for (byte i = 0; i < TAILLE_UID; i++) {
    readCard[i] = mfrc522.uid.uidByte[debut + i];     // recopie 4 octets d'UID
  }
  return true;
}

void terminerCarte() {                   // fin du dialogue avec le badge
  mfrc522.PICC_HaltA();                  // met le badge en sommeil
  mfrc522.PCD_StopCrypto1();             // arrête le chiffrement Crypto1 du lecteur
}

bool estMifareClassic() {                // true si le badge est un MIFARE Classic (Mini, 1K ou 4K)
  MFRC522::PICC_Type t = mfrc522.PICC_GetType(mfrc522.uid.sak);   // type déduit de l'octet SAK
  return t == MFRC522::PICC_TYPE_MIFARE_MINI || t == MFRC522::PICC_TYPE_MIFARE_1K ||
         t == MFRC522::PICC_TYPE_MIFARE_4K;
}

void cleUsine(MFRC522::MIFARE_Key &k) {  // clé par défaut d'un badge neuf : FF FF FF FF FF FF
  for (byte i = 0; i < 6; i++) k.keyByte[i] = 0xFF;
}

void cleBadge(const byte uid[4], byte domaine, MFRC522::MIFARE_Key &k) { // clé propre au badge (A ou B)
  deriver(domaine, uid, 0, 0, 0, k.keyByte, 6);   // 6 octets calculés depuis UID + clé maître
}

bool authentifier(MFRC522::MIFARE_Key &k) { // authentification clé A sur le secteur 1
  return mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, BLOC_VERROU,
                                  &k, &mfrc522.uid) == MFRC522::STATUS_OK;
}

// Après un échec d'authentification, le badge ne répond plus : on le réveille
bool reselectionner() {
  mfrc522.PCD_StopCrypto1();             // coupe le chiffrement en cours
  byte atqa[2];                          // réponse ATQA du badge
  byte taille = sizeof(atqa);
  if (mfrc522.PICC_WakeupA(atqa, &taille) != MFRC522::STATUS_OK) return false; // réveil (même badge en HALT)
  return mfrc522.PICC_Select(&mfrc522.uid) == MFRC522::STATUS_OK;              // re-sélection par son UID
}

// Construit le contenu du bloc 4 : 'C' 'A' + compteur + inscription + MAC (8) + 2 zéros
void preparerBloc(const byte uid[4], uint16_t cpt, uint16_t inscr, byte bloc[16]) {
  memset(bloc, 0, 16);                   // tout à zéro
  bloc[0] = 'C';                         // marqueur « Contrôle d'Accès »
  bloc[1] = 'A';
  bloc[2] = (byte)cpt;                   // compteur, octet faible
  bloc[3] = (byte)(cpt >> 8);            // compteur, octet fort
  bloc[4] = (byte)inscr;                 // inscription, octet faible
  bloc[5] = (byte)(inscr >> 8);          // inscription, octet fort
  deriver(3, uid, cpt, inscr, 1, bloc + 6, 8);   // signature (MAC) de 8 octets en positions 6 à 13
}

bool ecrireBlocDonnees(const byte uid[4], uint16_t cpt, uint16_t inscr) { // écrit le bloc 4 sur le badge
  byte bloc[16];
  preparerBloc(uid, cpt, inscr, bloc);
  return mfrc522.MIFARE_Write(BLOC_DONNEES, bloc, 16) == MFRC522::STATUS_OK;
}

bool ecrireVerrou(MFRC522::MIFARE_Key &a, MFRC522::MIFARE_Key &b) { // écrit le bloc 7 (clés + droits)
  byte bloc[16];
  memcpy(bloc, a.keyByte, 6);            // octets 0-5 : clé A
  bloc[6] = 0xFF; bloc[7] = 0x07; bloc[8] = 0x80; bloc[9] = 0x69;   // droits d'accès standard
  memcpy(bloc + 10, b.keyByte, 6);       // octets 10-15 : clé B
  return mfrc522.MIFARE_Write(BLOC_VERROU, bloc, 16) == MFRC522::STATUS_OK;
}

// Lit et vérifie le compteur signé du badge (authentification avec sa clé)
Resultat lireCompteurCarte(const byte uid[4], uint16_t &cpt, uint16_t &inscr) {
  MFRC522::MIFARE_Key k;
  cleBadge(uid, 1, k);                   // calcule la clé A du badge
  if (!authentifier(k)) return R_ECHEC;  // clé refusée ou badge parti
  byte buf[18];                          // 16 octets + 2 de CRC
  byte taille = sizeof(buf);
  if (mfrc522.MIFARE_Read(BLOC_DONNEES, buf, &taille) != MFRC522::STATUS_OK) return R_ECHEC; // lecture bloc 4
  if (buf[0] != 'C' || buf[1] != 'A') return R_FAUX;  // marqueur absent
  cpt = buf[2] | ((uint16_t)buf[3] << 8);             // compteur lu
  inscr = buf[4] | ((uint16_t)buf[5] << 8);           // inscription lue
  byte attendu[16];
  preparerBloc(uid, cpt, inscr, attendu);             // recalcule le bloc tel qu'il devrait être
  if (memcmp(buf, attendu, 16) != 0) return R_FAUX;   // signature invalide
  return R_OK;
}

// Passage d'un badge enregistré : vérifie inscription et compteur, avance de 1.
// cptBase / inscrBase = valeurs mémorisées ; nouveau = compteur à mémoriser si R_OK.
Resultat utiliserCarte(const byte uid[4], uint16_t cptBase, uint16_t inscrBase, uint16_t &nouveau) {
  if (!estMifareClassic()) return R_INCOMPATIBLE;
  uint16_t cpt, inscr;
  Resultat r = lireCompteurCarte(uid, cpt, inscr);   // lecture + vérification de signature
  if (r != R_OK) return r;
  if (inscr != inscrBase) {                        // copie d'une ancienne inscription
    Serial.println(F("Numero d'inscription different : ancienne copie du badge"));
    return R_FAUX;
  }
  uint16_t avance = (uint16_t)(cpt - cptBase);    // calcul modulo 65536
  if (avance > AVANCE_MAX) {                       // compteur en retard (avance « négative » = énorme) ou trop en avance
    Serial.print(F("Compteur badge = "));
    Serial.print(cpt);
    Serial.print(F(" / base = "));
    Serial.println(cptBase);
    return R_SUSPECT;
  }
  nouveau = cpt + 1;                               // valeur suivante
  if (!ecrireBlocDonnees(uid, nouveau, inscr)) return R_ECHEC;   // badge inchangé : rien de perdu
  return R_OK;
}

// Prépare un badge neuf (clé d'usine) ou déjà préparé par ce système :
// compteur à 0 et nouveau numéro d'inscription
Resultat initialiserCarte(const byte uid[4], uint16_t inscr) {
  if (!estMifareClassic()) return R_INCOMPATIBLE;
  MFRC522::MIFARE_Key a, b;
  bool neuf = true;                                // on suppose d'abord un badge neuf
  cleUsine(a);
  if (!authentifier(a)) {                          // la clé d'usine ne marche pas…
    neuf = false;                                  // …badge déjà préparé ?
    cleBadge(uid, 1, a);                           // essai avec notre clé
    if (!reselectionner() || !authentifier(a)) return R_CLE_INCONNUE;
  }
  if (!ecrireBlocDonnees(uid, 0, inscr)) return R_ECHEC;   // données d'abord…
  if (neuf) {                                      // badge neuf : on installe nos clés
    cleBadge(uid, 1, a);
    cleBadge(uid, 2, b);
    if (!ecrireVerrou(a, b)) return R_ECHEC;        // …clés ensuite
  }
  return R_OK;
}

// Remet le badge en configuration d'usine (bloc 4 à zéro, clés FF…FF)
bool remettreUsine(const byte uid[4]) {
  MFRC522::MIFARE_Key a, b;
  cleBadge(uid, 1, a);                             // notre clé A
  if (!authentifier(a)) return false;
  byte zero[16];
  memset(zero, 0, 16);
  if (mfrc522.MIFARE_Write(BLOC_DONNEES, zero, 16) != MFRC522::STATUS_OK) return false; // bloc 4 effacé
  cleUsine(a);
  cleUsine(b);
  return ecrireVerrou(a, b);                       // remet les clés FF…FF
}

// ---- Versions avec réessais : on recommence tant que l'échec est dû à la lecture ----
// (R_ECHEC = authentification ou lecture/écriture impossible, souvent badge encore en
//  approche). Les autres résultats (faux, suspect, incompatible) sont définitifs.
bool nouvelEssai(byte essai) {                     // prépare la tentative n° essai (0 = première)
  if (essai == 0) return true;                     // 1re tentative : rien à faire
  delay(PAUSE_ESSAI);                              // petite pause
  if (!reselectionner()) return false;             // réveille le badge
  Serial.print(F("  nouvelle tentative de lecture "));
  Serial.println(essai + 1);
  return true;
}

Resultat utiliserCarteEssais(const byte uid[4], uint16_t cptBase, uint16_t inscrBase, uint16_t &nouveau) {
  Resultat r = R_ECHEC;
  for (byte e = 0; e < ESSAIS_LECTURE; e++) {      // jusqu'à 6 tentatives
    if (!nouvelEssai(e)) continue;
    r = utiliserCarte(uid, cptBase, inscrBase, nouveau);
    if (r != R_ECHEC) break;                       // résultat définitif : on arrête
  }
  return r;
}

Resultat initialiserCarteEssais(const byte uid[4], uint16_t inscr) {
  Resultat r = R_ECHEC;
  for (byte e = 0; e < ESSAIS_LECTURE; e++) {
    if (!nouvelEssai(e)) continue;
    r = initialiserCarte(uid, inscr);
    if (r != R_ECHEC && r != R_CLE_INCONNUE) break;   // clé inconnue : peut être un échec de lecture
  }
  return r;
}

bool remettreUsineEssais(const byte uid[4]) {
  for (byte e = 0; e < ESSAIS_LECTURE; e++) {
    if (!nouvelEssai(e)) continue;
    if (remettreUsine(uid)) return true;           // réussi
  }
  return false;                                    // échec après toutes les tentatives
}

void ShowReaderDetails() {                         // vérifie la présence du MFRC522
  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);   // registre de version du composant
  Serial.print(F("MFRC522 Version: 0x"));
  Serial.print(v, HEX);
  if      (v == 0x91) Serial.print(F(" = v1.0"));
  else if (v == 0x92) Serial.print(F(" = v2.0"));
  else if (v == 0x82 || v == 0x88 || v == 0x12) Serial.print(F(" = compatible (clone)"));
  else                Serial.print(F(" (Inconnu)"));
  Serial.println();

  if (v == 0x00 || v == 0xFF) {                    // 0x00 ou 0xFF = pas de réponse sur le SPI
    Serial.println(F("ATTENTION: Erreur de Communication"));
    Serial.println(F("Verifier que le RFID-MFRC522 soit correctement connecte"));
    afficher(F("ATTENTION"), F("Erreur Communication"), F("Lecteur RFID"), nullptr);
    allumerSeule(PIN_LED_ROUGE);
    afficherBaseSerie();                           // la base reste consultable même sans lecteur
    while (true) gererCommandesSerie();            // blocage définitif (commandes r/d/h toujours actives)
  }
}


// ============================================================================
//                        BP d'ouverture (anti-rebond)
// ============================================================================
bool bpOuvertureAppuye() {                         // renvoie true si le BP est appuyé (filtré)
  static bool etatStable = HIGH;                   // dernier état validé (static = conservé entre appels)
  static bool dernierLu = HIGH;                    // dernière lecture brute
  static unsigned long dernierChangement = 0;      // instant du dernier changement brut

  bool lu = digitalRead(PIN_BP_OUVERTURE);         // lecture brute (LOW = appuyé, pull-up)
  if (lu != dernierLu) {                           // la lecture a changé
    dernierLu = lu;
    dernierChangement = millis();                  // relance le chrono
  }
  if (millis() - dernierChangement >= ANTI_REBOND) { // stable depuis 50 ms
    etatStable = dernierLu;                        // on valide
  }
  return etatStable == LOW;
}


// ============================================================================
//                         Actions (ouverture, refus, prog)
// ============================================================================
void fermerPorte() {
  digitalWrite(PIN_SERRURE, LOW);                  // serrure verrouillée
  Serial.println(F("Fermeture de la Porte"));
  allumerSeule(PIN_LED_ROUGE);
  afficher(F("Porte FERMEE...."), nullptr, nullptr, nullptr);
  entrerMessage();                                 // message 2 s puis retour à l'attente
}

void ouvrirParBadge() {
  Serial.println(F("Bonjour, Acces Autorise - Ouverture de la Porte"));
  afficher(F("      BONJOUR"),
           F("** Acces Autorise **"),
           F("  Ouverture Porte"),
           F("     **********"));
  allumerSeule(PIN_LED_VERTE);
  digitalWrite(PIN_SERRURE, HIGH);                 // ouvre la serrure
  bipAutorise();
  etat = OUVERT_BADGE;                             // la fermeture se fera dans loop() après 10 s
  debutEtat = millis();
}

void ouvrirParBP() {
  Serial.println(F("Ouverture forcee par BP"));
  afficher(F(" Ouverture Forcee"), F("     Par BP"), nullptr, nullptr);
  allumerSeule(PIN_LED_VERTE);
  tone(PIN_BUZZ, 1500, 100);
  digitalWrite(PIN_SERRURE, HIGH);                 // ouvre la serrure
  etat = OUVERT_BP;                                // fermeture gérée dans loop()
  debutEtat = millis();
}

void refuser() {                                   // badge absent de la base
  Serial.println(F("Acces Refuse (badge inconnu)"));
  afficher(F("** Acces REFUSE **"),
           F("--------------------"),
           F("   Porte FERMEE.."),
           F("--------------------"));
  allumerSeule(PIN_LED_ROUGE);
  bipRefus();
  entrerMessage();
}

// Messages d'échec d'un badge enregistré (ou du badge admin)
void signalerEchec(Resultat r) {
  allumerSeule(PIN_LED_ROUGE);
  switch (r) {
    case R_SUSPECT:                                // copie détectée
      Serial.println(F("ALERTE : compteur en retard -> COPIE du badge detectee"));
      afficher(F("** Badge SUSPECT **"), F("  Copie detectee"),
               F("   Badge BLOQUE"), F("Voir administrateur"));
      lancerClignotement(PIN_LED_ROUGE, 6);
      bipAlerte();
      entrerMessageDuree(DUREE_MESSAGE_ALERTE);
      return;
    case R_FAUX:                                   // signature ou inscription fausse
      Serial.println(F("Refus : donnees du badge invalides (non authentique)"));
      afficher(F("** Acces REFUSE **"), F("Badge NON"), F("AUTHENTIQUE"), nullptr);
      break;
    case R_INCOMPATIBLE:                           // pas un MIFARE Classic
      Serial.println(F("Refus : badge non MIFARE Classic"));
      afficher(F("** Acces REFUSE **"), F("Badge non"), F("compatible"), nullptr);
      break;
    default:   // R_ECHEC (après toutes les tentatives)
      Serial.println(F("Lecture incomplete du badge (badge mal place, retire trop tot ou copie)"));
      afficher(F("  Lecture du badge"), F("     incomplete"),
               F("Representez-le bien"), F("a plat, sans bouger"));
      break;
  }
  bipRefus();
  entrerMessage();
}

void afficherBloque() {                            // badge marqué BLOQUÉ en EEPROM
  Serial.println(F("Refus : badge BLOQUE (copie detectee precedemment)"));
  afficher(F("** Acces REFUSE **"), F("   Badge BLOQUE"),
           F("Voir administrateur"), nullptr);
  allumerSeule(PIN_LED_ROUGE);
  bipRefus();
  entrerMessageDuree(DUREE_MESSAGE_ALERTE);
}

void entrerProgrammation() {
  modeProg = true;
  derniereActionProg = millis();                   // départ du délai de 30 s
  Serial.println(F("Entree dans le mode Programmation"));
  Serial.print(F("Il y a "));
  Serial.print(EEPROM.read(ADR_NB_BADGES));
  Serial.println(F(" badge(s) dans la base"));
  afficher(F("Entre dans le Mode"), F("  PROGRAMMATION..."), nullptr, nullptr);
  entrerMessage();
}

void sortirProgrammation(bool parTimeout) {        // parTimeout = sortie automatique après 30 s
  modeProg = false;
  if (parTimeout) {
    Serial.println(F("Sortie du mode Programmation (delai depasse)"));
    afficher(F("  FIN PROGRAMMATION"), nullptr, F("   Delai depasse"), nullptr);
  } else {
    Serial.println(F("Badge Administrateur lu - Sortie du mode Programmation"));
    afficher(F("      Badge"), F(" Administrateur lu"), nullptr, F("  FIN PROGRAMMATION"));
  }
  allumerSeule(PIN_LED_BLEUE);
  entrerMessage();
}

// Mode programmation : ajout d'un badge inconnu
void enregistrerBadge() {
  if (EEPROM.read(ADR_NB_BADGES) >= nbBadgesMax()) {   // base pleine (112)
    Serial.println(F("ERREUR : base pleine"));
    afficher(F("....ERREUR!....."), F("  Base pleine"), F("Badge non enregistre"), nullptr);
    lancerClignotement(PIN_LED_ROUGE, 3);
    entrerMessage();
    return;
  }
  uint16_t inscr = nouvelleInscription();          // tire un n° d'inscription
  Resultat r = initialiserCarteEssais(readCard, inscr);   // prépare le badge
  if (r == R_OK && ajouterBadge(readCard, inscr)) {       // puis l'ajoute à l'EEPROM
    Serial.println(F("Badge prepare (cle + compteur) et enregistre dans la base"));
    afficher(F("Badge ENREGISTRE"), F("dans la BASE..."), F("(cle + compteur"), F(" installes)"));
    tone(PIN_BUZZ, 2200, 80);
    lancerClignotement(PIN_LED_VERTE, 3);
  } else {
    Serial.print(F("ERREUR enregistrement, code "));
    Serial.println((int)r);
    if (r == R_INCOMPATIBLE)
      afficher(F("....ERREUR!....."), F("Badge non compatible"), F("(MIFARE Classic"), F(" requis)"));
    else if (r == R_CLE_INCONNUE)
      afficher(F("....ERREUR!....."), F("Badge non compatible"), F("(cle inconnue)"), nullptr);
    else
      afficher(F("....ERREUR!....."), F("Ecriture impossible"), F("Representez le"), F("badge"));
    lancerClignotement(PIN_LED_ROUGE, 3);
    bipRefus();
  }
  entrerMessage();
}

// Mode programmation : retrait d'un badge connu
void retirerBadge(int slot) {
  bool usine = remettreUsineEssais(readCard);      // tente de remettre le badge en clé d'usine
  supprimerEntree(slot);                           // retire sa fiche de l'EEPROM dans tous les cas
  Serial.println(usine ? F("Badge remis en cle d'usine et supprime de la base")
                       : F("Badge supprime de la base (badge non remis a zero)"));
  afficher(F("Badge SUPPRIME"), F("de la BASE..."), nullptr,
           usine ? nullptr : F("(non remis a zero)"));
  tone(PIN_BUZZ, 1800, 80);
  lancerClignotement(PIN_LED_BLEUE, 3);
  entrerMessage();
}

// Traitement d'un badge qui vient d'être lu (le badge est encore sélectionné)
void traiterBadge() {
  uint16_t nouveau = 0;                            // futur compteur à mémoriser

  // ---- Badge administrateur : toujours vérifié (clé + compteur) ----
  if (isMaster(readCard)) {
    if (modeProg) derniereActionProg = millis();
    Resultat r = utiliserCarteEssais(readCard, lire16(ADR_CPT_ADMIN), lire16(ADR_INSCR_ADMIN), nouveau);
    if (r != R_OK) {
      Serial.println(F("Badge ADMIN refuse"));
      signalerEchec(r == R_SUSPECT ? R_FAUX : r);   // l'admin n'est jamais bloqué
      return;
    }
    ecrire16(ADR_CPT_ADMIN, nouveau);              // mémorise le compteur admin (adresses 0x06-0x07)
    if (modeProg) sortirProgrammation(false);      // bascule du mode programmation
    else          entrerProgrammation();
    return;
  }

  int slot = findIDSLOT(readCard);                 // cherche le badge dans la base (0 = inconnu)

  // ---- Mode programmation : ajout ou retrait ----
  if (modeProg) {
    derniereActionProg = millis();
    if (slot) retirerBadge(slot);                  // connu -> retrait
    else      enregistrerBadge();                  // inconnu -> ajout
    return;
  }

  // ---- Mode normal ----
  if (slot == 0)                          { refuser(); return; }        // inconnu
  if (etatBadge(slot) == ETAT_BLOQUE)     { afficherBloque(); return; } // bloqué

  Resultat r = utiliserCarteEssais(readCard, compteurBadge(slot), inscriptionBadge(slot), nouveau);
  if (r == R_OK) {
    ecrireCompteurBadge(slot, nouveau);            // mémorise le nouveau compteur
    ouvrirParBadge();
  } else {
    if (r == R_SUSPECT) ecrireEtatBadge(slot, ETAT_BLOQUE);   // copie : on bloque la fiche
    signalerEchec(r);
  }
}


// ============================================================================
//                     Fonctions de démarrage (setup)
// ============================================================================
void demandeEffacement() {                         // BP RAZ appuyé au démarrage
  digitalWrite(PIN_LED_ROUGE, HIGH);
  Serial.println(F("BP effacement de la base appuye"));
  Serial.println(F("Relacher dans les 5 s pour annuler"));
  afficher(F("** BP RAZ Appuye **"), nullptr, F("      Vous avez"), F("    s pour annuler"));

  for (int compteur = 5; compteur > 0; compteur--) {   // compte à rebours 5..1
    lcd.setCursor(3, 3);
    lcd.print(compteur);
    delay(1000);
  }

  if (digitalRead(PIN_BP_RAZ) == LOW) {            // toujours appuyé : on efface
    Serial.println(F("EFFACEMENT DE LA BASE"));
    afficher(nullptr, F("     EFFACEMENT"), F("     DE LA BASE"), nullptr);
    for (unsigned int x = 0; x < EEPROM.length(); x++) {
      EEPROM.update(x, 0);                         // met les 1024 octets à 0 (admin compris)
    }
    Serial.println(F("Base effacee"));
    afficher(F("DATA BASE Effacee"), nullptr, nullptr, nullptr);
    for (byte i = 0; i < 3; i++) {                 // 3 clignotements rouges
      digitalWrite(PIN_LED_ROUGE, LOW);
      delay(200);
      digitalWrite(PIN_LED_ROUGE, HIGH);
      delay(200);
    }
  } else {                                         // relâché : annulation
    Serial.println(F("Effacement annule"));
    afficher(nullptr, F("  Effacement Annule"), nullptr, nullptr);
    delay(1000);
  }
  digitalWrite(PIN_LED_ROUGE, LOW);
}

void verifierBase() {                              // contrôle de cohérence de l'EEPROM
  byte flag = EEPROM.read(ADR_FLAG_ADMIN);         // drapeau en 0x01
  if (flag == CODE_ANCIEN) {                       // base de l'ancienne version
    Serial.println(F("Ancienne base (UID seul) detectee : badges a reenregistrer"));
    afficher(F("Ancienne base"), F("detectee :"), F("badges a"), F("reenregistrer"));
    EEPROM.update(ADR_FLAG_ADMIN, 0);              // plus d'admin -> il faudra en définir un
    EEPROM.update(ADR_NB_BADGES, 0);               // base vidée
    delay(3000);
  }
  if (EEPROM.read(ADR_NB_BADGES) > nbBadgesMax()) {// nombre aberrant (ex. 255 sur EEPROM vierge)
    Serial.println(F("Compteur de badges invalide -> remis a 0"));
    EEPROM.update(ADR_NB_BADGES, 0);
  }
}

void enregistrerAdmin() {                          // définition du badge admin (1re mise en service)
  uint16_t inscr = 0;
  Serial.println(F("Pas de Badge Administrateur"));
  while (true) {                                   // boucle jusqu'à un badge admin valide
    Serial.println(F("Placer un badge MIFARE Classic pour le definir comme badge administrateur"));
    afficher(F("    Pas de Badge"),
             F("   Administrateur"),
             F("     Enregistre"),
             F("  Placer une Carte"));

    while (!getID()) {                             // attend un badge, LED bleue clignotante
      digitalWrite(PIN_LED_BLEUE, HIGH);
      delay(200);
      digitalWrite(PIN_LED_BLEUE, LOW);
      delay(200);
    }

    inscr = nouvelleInscription();
    Resultat r = initialiserCarteEssais(readCard, inscr);   // prépare le badge
    terminerCarte();
    if (r == R_OK) break;                          // succès : sortie de la boucle

    Serial.print(F("Badge refuse, code "));
    Serial.println((int)r);
    if (r == R_INCOMPATIBLE || r == R_CLE_INCONNUE)
      afficher(F("....ERREUR!....."), F("Badge non compatible"), F("Essayez un autre"), F("badge"));
    else
      afficher(F("....ERREUR!....."), F("Ecriture impossible"), F("Representez le"), F("badge"));
    bipRefus();
    delay(2500);
  }

  EEPROM.update(ADR_NB_BADGES, 0);                 // aucun badge utilisateur
  for (byte j = 0; j < TAILLE_UID; j++) {
    EEPROM.update(ADR_UID_ADMIN + j, readCard[j]); // UID admin en 0x02-0x05
  }
  ecrire16(ADR_CPT_ADMIN, 0);                      // compteur admin en 0x06-0x07
  ecrire16(ADR_INSCR_ADMIN, inscr);                // inscription admin en 0x08-0x09
  EEPROM.update(ADR_FLAG_ADMIN, CODE_ADMIN);   // validé en dernier
  Serial.println(F("Badge Administrateur Enregistre"));
  afficher(F("Badge Administrateur"), F("    Enregistre"), nullptr, nullptr);
  delay(1500);
}


// ============================================================================
//                                  SETUP
// ============================================================================
void setup() {
  pinMode(PIN_SERRURE, OUTPUT);
  digitalWrite(PIN_SERRURE, LOW);                  // porte fermée dès le démarrage
  pinMode(PIN_LED_ROUGE, OUTPUT);
  pinMode(PIN_LED_VERTE, OUTPUT);
  pinMode(PIN_LED_BLEUE, OUTPUT);
  pinMode(PIN_BUZZ, OUTPUT);
  ledsOff();

  pinMode(PIN_BP_RAZ, INPUT_PULLUP);               // résistance de tirage interne : HIGH au repos
  pinMode(PIN_BP_OUVERTURE, INPUT_PULLUP);

  Serial.begin(115200);                            // moniteur série à 115200 bauds
  lcd.begin(20, 4);                                // LCD 20 colonnes x 4 lignes
  lcd.backlight();                                 // rétroéclairage allumé

  SPI.begin();                                     // démarre le bus SPI
  mfrc522.PCD_Init();                              // initialise le lecteur RFID
  // Pour augmenter la distance de lecture :
  // mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);

  Serial.println(F("Lecteur de Badges - version securisee (secret + compteur)"));
  ShowReaderDetails();                             // vérifie le lecteur (bloque si absent)

  if (digitalRead(PIN_BP_RAZ) == LOW) {            // BP RAZ appuyé pendant le reset ?
    demandeEffacement();
  }

  verifierBase();                                  // contrôle de l'EEPROM

  if (EEPROM.read(ADR_FLAG_ADMIN) != CODE_ADMIN) { // pas d'admin valide
    enregistrerAdmin();
  }

  for (byte i = 0; i < TAILLE_UID; i++) {
    masterCard[i] = EEPROM.read(ADR_UID_ADMIN + i);// charge l'UID admin en RAM
  }

  afficherBaseSerie();                             // contenu de la base sur le moniteur série
  Serial.println(F("Lecteur Pret - En attente de lecture de Badge"));
  Serial.println(F("-------------------"));

  afficherEcranAttente();
}


// ============================================================================
//                                   LOOP
// ============================================================================
void loop() {
  unsigned long maintenant = millis();             // heure courante
  bool bp = bpOuvertureAppuye();                   // état filtré du BP d'ouverture
  if (!bp) bpDoitEtreRelache = false;              // BP relâché : il pourra resservir

  switch (etat) {                                  // automate à états

    case ATTENTE:
    case MESSAGE:
      digitalWrite(PIN_SERRURE, LOW);   // sécurité : porte fermée hors ouverture

      if (etat == MESSAGE && maintenant - debutEtat >= dureeMessage) { // message terminé
        etat = ATTENTE;
        afficherEcranAttente();
      }

      if (bp && !bpDoitEtreRelache) {              // BP appuyé (et pas resté coincé)
        ouvrirParBP();
        break;
      }

      if (etat == ATTENTE) {                       // on ne lit les badges qu'en attente
        if (modeProg && maintenant - derniereActionProg >= TIMEOUT_PROGRAMMATION) {
          sortirProgrammation(true);               // 30 s sans action
          break;
        }
        if (getID()) {                             // un badge est présenté
          traiterBadge();
          terminerCarte();
        } else {
          gererCommandesSerie();                   // pas de badge : commandes r / d / h du moniteur
        }
      }
      break;

    case OUVERT_BADGE:
      if (maintenant - debutEtat >= DUREE_OUVERTURE_BADGE) { // 10 s écoulées
        fermerPorte();
      }
      break;

    case OUVERT_BP: {
      unsigned long duree = maintenant - debutEtat;          // temps d'ouverture écoulé
      bool finNormale = (duree >= DUREE_OUVERTURE_BP_MIN) && !bp; // ≥ 5 s et BP relâché
      bool finSecurite = (duree >= DUREE_OUVERTURE_BP_MAX);       // 70 s atteintes
      if (finNormale || finSecurite) {
        if (bp) {                                  // fermeture alors que le BP est encore appuyé
          bpDoitEtreRelache = true;                // il faudra le relâcher avant une nouvelle ouverture
          Serial.println(F("BP maintenu trop longtemps : fermeture de securite"));
        }
        fermerPorte();
      }
      break;
    }
  }

  gererLeds();                                     // animation des LEDs à chaque tour
}
