/* ============================================================================
 *  Contrôle d'accès RFID SÉCURISÉ (MFRC522) + LCD 20x4 I2C (librairie hd44780)
 *  Version « secret + compteur » pour badges MIFARE Classic 1K/4K
 *  >>> VERSION ARDUINO UNO R4 (Minima ou WiFi) <<<
 *  + Lecture de la base de badges (EEPROM) depuis le moniteur série
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
 *    (5 s minimum, tant que le BP est appuyé, 8 s maximum).
 *
 *  Commandes du moniteur série (115200 bauds) - LECTURE SEULE de l'EEPROM :
 *    r : affiche la base (admin, fiches : UID, passages, inscription, état)
 *    d : vidage brut de l'en-tête (bloc 0)
 *    f : vidage brut des fiches utilisées
 *    D : vidage brut complet (8 Ko, long)
 *    h : rappel des commandes
 *    Entrée seule (ligne vide) : comme "r"
 *  (d et D sont différenciés : minuscule = en-tête, MAJUSCULE = complet)
 *
 *  Résumé automatique (base + commandes) :
 *    - à la fin du démarrage ;
 *    - UNO R4 Minima : à chaque ouverture du moniteur série (détection USB) ;
 *    - UNO R4 WiFi : la carte ne peut pas savoir que le moniteur vient d'être
 *      ouvert (liaison via le module ESP32) -> appuyer sur Entrée ou sur RESET.
 *  Pour la touche Entrée, régler le moniteur sur « Nouvelle ligne ».
 *
 *  !!! AVANT D'ENREGISTRER LE PREMIER BADGE : personnaliser CLE_MAITRE !!!
 *  !!! puis la conserver précieusement et ne plus jamais la modifier.   !!!
 *  (si elle change, les badges déjà préparés ne sont plus reconnus)
 *
 *  EEPROM du R4 : 8 Ko simulés en mémoire flash, par blocs de 1 Ko. Chaque
 *  modification efface et réécrit le bloc entier (environ 100 000 fois par bloc).
 *  Pour ménager la flash :
 *    - chaque écriture est groupée (EEPROM.put) : 1 seul effacement par passage ;
 *    - l'en-tête (badge admin) est seul dans le bloc 0 ;
 *    - un retrait déplace seulement le dernier badge dans la place libérée ;
 *    - la base est recopiée en RAM au démarrage (lecture rapide).
 *
 *  Organisation de l'EEPROM (format R4, version 2) :
 *    0x0000 (bloc 0) : en-tête (12 octets)
 *        drapeau 0xB4 (1) + version (1) + nombre de badges (2)
 *        + UID admin (4) + compteur admin (2) + n° d'inscription admin (2)
 *    0x0400 ...      : FICHES des badges, rangées les unes derrière les autres,
 *        9 octets chacune : UID (4) + compteur (2) + n° d'inscription (2)
 *        + état (1 : 0 = actif, 1 = bloqué)
 *        fiche n°1 en 0x0400, n°2 en 0x0409, n°3 en 0x0412, ...
 *  Capacité : 796 badges ((8192 - 1024) / 9).
 *  Une base de la version 1 (badges répartis sur 7 blocs) est convertie
 *  automatiquement au premier démarrage : rien à réenregistrer.
 *
 *  Contenu du badge, secteur 1 :
 *    bloc 4 : 'C' 'A' + compteur (2) + n° d'inscription (2) + MAC (8) + 2 octets à 0
 *    bloc 7 : clé A (6) + droits d'accès FF 07 80 69 + clé B (6)
 *
 *  Câblage : identique à la version Uno. ATTENTION, les broches du R4 sont
 *  limitées à 8 mA : résistances de LED de 470 Ohm à 1 kOhm, buzzer par
 *  transistor s'il consomme plus de 8 mA.
 *  MFRC522 :
 *    VCC : 3.3V ATTENTION   RST : D9   SS : D10
 *    MOSI : D11   MISO : D12   SCK : D13   IRQ : non connecté
 * ========================================================================== */

#if !defined(ARDUINO_ARCH_RENESAS)
#error "Version prevue pour Arduino UNO R4 (Minima / WiFi). Pour l'Uno, utiliser Autorisation_Ouverture_Securisee_HD44780."
#endif

#include <EEPROM.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

// ------------------------------- Broches ------------------------------------
const byte PIN_BUZZ         = 8;
const byte PIN_LED_ROUGE    = 7;
const byte PIN_LED_VERTE    = 6;
const byte PIN_LED_BLEUE    = 5;
const byte PIN_SERRURE      = 4;
const byte PIN_BP_RAZ       = 3;   // BP effacement base -> GND
const byte PIN_BP_OUVERTURE = 2;   // BP ouverture forcée -> GND
const byte PIN_RC522_SS     = 10;
const byte PIN_RC522_RST    = 9;

// ------------------------------- Réglages -----------------------------------
const unsigned long DUREE_OUVERTURE_BADGE  = 10000UL; // ms (10 s)
const unsigned long DUREE_OUVERTURE_BP_MIN = 5000UL;  // ms
const unsigned long DUREE_OUVERTURE_BP_MAX = 8000UL;  // ms, sécurité si BP coincé
const unsigned long DUREE_MESSAGE          = 2000UL;  // durée des messages LCD
const unsigned long DUREE_MESSAGE_ALERTE   = 4000UL;  // badge suspect / bloqué
const unsigned long TIMEOUT_PROGRAMMATION  = 30000UL; // sortie auto du mode prog
const unsigned long ANTI_REBOND            = 50UL;    // ms
const unsigned long PAS_LEDS               = 200UL;   // ms, cycle / clignotement

// Badges à UID long : garder les 4 DERNIERS octets (true) ou les 4 premiers.
// (sans effet sur les MIFARE Classic à UID de 4 octets)
const bool UID_LONG_4_DERNIERS = true;

// ------------------------------- Sécurité -----------------------------------
// CLÉ MAÎTRE : 16 octets secrets. À PERSONNALISER avant le premier enregistrement
// (remplacer par vos propres valeurs au hasard), à conserver, à ne plus changer.
const byte CLE_MAITRE[16] PROGMEM = {
  0xF8, 0x9B, 0xE3, 0x3B, 0x35, 0xA2, 0x18, 0xC0,
  0x3D, 0xA2, 0x1D, 0x0B, 0xEB, 0xAE, 0xCB, 0xE2
};
const byte SECTEUR_BADGE = 1;                       // secteur utilisé sur le badge
const byte BLOC_DONNEES  = SECTEUR_BADGE * 4;       // bloc 4
const byte BLOC_VERROU   = SECTEUR_BADGE * 4 + 3;   // bloc 7
const uint16_t AVANCE_MAX = 3;  // avance tolérée du compteur du badge (badge retiré trop tôt)
// Badge détecté au bord du champ (pendant qu'on l'approche) : l'UID est lu mais
// l'échange avec le secteur échoue. On réessaie alors sans rien afficher.
const byte ESSAIS_LECTURE = 6;            // nombre de tentatives
const unsigned long PAUSE_ESSAI = 60UL;   // ms entre deux tentatives (6 x 60 ms = 0,4 s)

// ------------------------------- EEPROM -------------------------------------
const byte CODE_ADMIN        = 0xB4;   // format R4 sécurisé
const byte VERSION_BASE      = 2;      // 2 = fiches les unes derrière les autres
const byte TAILLE_UID        = 4;
const int  TAILLE_BLOC       = 1024;   // bloc de la flash de données
const int  TAILLE_EEPROM     = 8192;   // EEPROM simulée du R4 (8 blocs)
const int  ADR_PREMIER_BADGE = TAILLE_BLOC;   // 0x0400 : début du tableau des fiches (bloc 1)
const int  TAILLE_ENTREE     = 9;      // UID 4 + compteur 2 + inscription 2 + état 1
const int  MAX_BADGES        = (TAILLE_EEPROM - ADR_PREMIER_BADGE) / TAILLE_ENTREE;   // 796
const byte NB_BLOCS_V1       = 7;      // ancienne version 1 : badges répartis sur 7 blocs
const int  MAX_BADGES_V1     = NB_BLOCS_V1 * (TAILLE_BLOC / TAILLE_ENTREE);           // 791
const byte ETAT_ACTIF        = 0;
const byte ETAT_BLOQUE       = 1;

// ------------------------------- Objets -------------------------------------
MFRC522 mfrc522(PIN_RC522_SS, PIN_RC522_RST);
hd44780_I2Cexp lcd;   // adresse I2C détectée automatiquement

// ------------------------------- Variables ----------------------------------
byte readCard[TAILLE_UID];    // UID du badge qui vient d'être lu
byte masterCard[TAILLE_UID];  // UID du badge administrateur

// Copie en RAM de la base (chargée au démarrage, écrite en même temps que l'EEPROM)
struct __attribute__((packed)) Entete {
  uint8_t  flag;
  uint8_t  version;
  uint16_t nb;
  uint8_t  uidAdmin[4];
  uint16_t cptAdmin;
  uint16_t inscrAdmin;
};
struct __attribute__((packed)) Entree {
  uint8_t  uid[4];
  uint16_t cpt;
  uint16_t inscr;
  uint8_t  etat;
};
static_assert(sizeof(Entree) == TAILLE_ENTREE, "taille d'entree");
Entete entete;
Entree base[MAX_BADGES];

enum Etat { ATTENTE, MESSAGE, OUVERT_BADGE, OUVERT_BP };
Etat etat = ATTENTE;
unsigned long debutEtat = 0;
unsigned long dureeMessage = DUREE_MESSAGE;

bool modeProg = false;
unsigned long derniereActionProg = 0;

bool bpDoitEtreRelache = false;  // empêche une réouverture si le BP est resté coincé

byte ledClignote = 0;
byte nbBasculements = 0;
bool etatLedClignote = false;
unsigned long dernierBasculement = 0;

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
static inline uint64_t rotl64(uint64_t x, byte b) { return (x << b) | (x >> (64 - b)); }

static uint64_t lireLE64(const byte *p) {
  uint64_t v = 0;
  for (int8_t i = 7; i >= 0; i--) v = (v << 8) | p[i];
  return v;
}

#define SIPROUND do { \
  v0 += v1; v1 = rotl64(v1, 13); v1 ^= v0; v0 = rotl64(v0, 32); \
  v2 += v3; v3 = rotl64(v3, 16); v3 ^= v2;                      \
  v0 += v3; v3 = rotl64(v3, 21); v3 ^= v0;                      \
  v2 += v1; v1 = rotl64(v1, 17); v1 ^= v2; v2 = rotl64(v2, 32); \
} while (0)

// Empreinte de 64 bits du message m, avec la clé maître
uint64_t siphash(const byte *m, byte len) {
  byte k[16];
  memcpy_P(k, CLE_MAITRE, 16);
  uint64_t k0 = lireLE64(k), k1 = lireLE64(k + 8);
  memset(k, 0, sizeof(k));
  uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
  uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
  uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
  uint64_t v3 = 0x7465646279746573ULL ^ k1;
  byte i = 0;
  for (; i + 8 <= len; i += 8) {
    uint64_t mi = lireLE64(m + i);
    v3 ^= mi; SIPROUND; SIPROUND; v0 ^= mi;
  }
  uint64_t b = ((uint64_t)len) << 56;
  for (byte j = 0; i + j < len; j++) b |= ((uint64_t)m[i + j]) << (8 * j);
  v3 ^= b; SIPROUND; SIPROUND; v0 ^= b;
  v2 ^= 0xff; SIPROUND; SIPROUND; SIPROUND; SIPROUND;
  return v0 ^ v1 ^ v2 ^ v3;
}

// domaine 1 = clé A, 2 = clé B, 3 = signature (MAC) du compteur et de l'inscription
void deriver(byte domaine, const byte uid[4], uint16_t cpt, uint16_t inscr, byte avecCpt,
             byte *sortie, byte n) {
  byte m[9] = { domaine, uid[0], uid[1], uid[2], uid[3],
                (byte)cpt, (byte)(cpt >> 8), (byte)inscr, (byte)(inscr >> 8) };
  uint64_t h = siphash(m, avecCpt ? 9 : 5);
  for (byte i = 0; i < n; i++) { sortie[i] = (byte)h; h >>= 8; }
}


// ============================================================================
//                                   LEDs
// ============================================================================
void ledsOff() {
  digitalWrite(PIN_LED_ROUGE, LOW);
  digitalWrite(PIN_LED_VERTE, LOW);
  digitalWrite(PIN_LED_BLEUE, LOW);
}

void allumerSeule(byte pin) {
  ledClignote = 0;
  nbBasculements = 0;
  ledsOff();
  digitalWrite(pin, HIGH);
}

void lancerClignotement(byte pin, byte nbFois) {
  ledsOff();
  ledClignote = pin;
  nbBasculements = nbFois * 2;
  etatLedClignote = false;
  dernierBasculement = millis();
}

void gererLeds() {
  unsigned long maintenant = millis();

  if (etat == MESSAGE) {
    if (ledClignote != 0 && nbBasculements > 0 &&
        maintenant - dernierBasculement >= PAS_LEDS) {
      dernierBasculement = maintenant;
      etatLedClignote = !etatLedClignote;
      digitalWrite(ledClignote, etatLedClignote);
      nbBasculements--;
    }
    return;
  }

  if (etat == ATTENTE) {
    if (modeProg) {
      byte phase = (maintenant / PAS_LEDS) % 3;
      digitalWrite(PIN_LED_VERTE, phase == 0);
      digitalWrite(PIN_LED_BLEUE, phase == 1);
      digitalWrite(PIN_LED_ROUGE, phase == 2);
    } else {
      allumerSeule(PIN_LED_BLEUE);
    }
  }
}


// ============================================================================
//                                   LCD
// ============================================================================
void afficher(const __FlashStringHelper* l0, const __FlashStringHelper* l1,
              const __FlashStringHelper* l2, const __FlashStringHelper* l3) {
  const __FlashStringHelper* lignes[4] = { l0, l1, l2, l3 };
  lcd.clear();
  for (byte i = 0; i < 4; i++) {
    if (lignes[i] != nullptr) {
      lcd.setCursor(0, i);
      lcd.print(lignes[i]);
    }
  }
}

void afficherEcranAttente() {
  if (modeProg) {
    afficher(F("MODE PROGRAMMATION"),
             F("Passer un badge pour"),
             F("  Ajout ou Retrait"),
             nullptr);
    lcd.setCursor(0, 3);
    lcd.print(F("Badges en base: "));
    lcd.print(entete.nb);
  } else {
    afficher(F("--------------------"),
             F("      Attente"),
             F("   Lecture Badge"),
             F("--------------------"));
  }
}

void entrerMessageDuree(unsigned long duree) {
  etat = MESSAGE;
  debutEtat = millis();
  dureeMessage = duree;
}

void entrerMessage() {
  entrerMessageDuree(DUREE_MESSAGE);
}


// ============================================================================
//                                  Buzzer
// ============================================================================
void bipAutorise() {
  const int notes[] = { 523, 783, 1046, 1568, 2093 };
  for (byte i = 0; i < 5; i++) {
    tone(PIN_BUZZ, notes[i], 50);
    delay(50);
  }
}

void bipRefus() {
  tone(PIN_BUZZ, 370, 50);
  delay(100);
  tone(PIN_BUZZ, 370, 300);
}

void bipAlerte() {
  for (byte i = 0; i < 3; i++) {
    tone(PIN_BUZZ, 2500, 120);
    delay(200);
  }
}


// ============================================================================
//                           Base de badges (EEPROM)
// ============================================================================
int nbBadgesMax() {
  return MAX_BADGES;
}

// La base est un tableau de fiches collées les unes derrière les autres :
// fiche n° i (0..nb-1) à l'adresse 0x0400 + i x 9.
int adresseEntree(int i) {
  return ADR_PREMIER_BADGE + i * TAILLE_ENTREE;   // ex. i = 2 -> 0x0400 + 18 = 0x0412
}

// Adresse d'une fiche dans l'ancienne version 1 (conversion et lecture série)
int adresseEntreeV1(int i) {
  return (1 + i % NB_BLOCS_V1) * TAILLE_BLOC + (i / NB_BLOCS_V1) * TAILLE_ENTREE;
}

void sauverEntete()          { EEPROM.put(0, entete); }               // 1 effacement (bloc 0)
void sauverEntree(int i)     { EEPROM.put(adresseEntree(i), base[i]); }  // 1 effacement

void chargerBase() {
  EEPROM.get(0, entete);
  if (entete.flag != CODE_ADMIN) return;          // pas encore d'admin : rien à charger
  if (entete.nb > MAX_BADGES) {
    Serial.println(F("Nombre de badges invalide -> remis a 0"));
    entete.nb = 0;
    sauverEntete();
  }

  if (entete.version == 1) {                      // base version 1 : badges répartis sur 7 blocs
    Serial.println(F("Base version 1 : conversion (fiches les unes derriere les autres)"));
    for (int i = 0; i < entete.nb; i++) EEPROM.get(adresseEntreeV1(i), base[i]); // lecture ancien format
    for (int i = 0; i < entete.nb; i++) sauverEntree(i);                         // réécriture à la suite
    entete.version = VERSION_BASE;
    sauverEntete();                               // version mise à jour en dernier
    return;
  }

  for (int i = 0; i < entete.nb; i++) EEPROM.get(adresseEntree(i), base[i]);
}

// slot = n° de 1 à nb (comme la version Uno) ; entrée RAM = base[slot - 1]
uint16_t compteurBadge(int slot)      { return base[slot - 1].cpt; }
uint16_t inscriptionBadge(int slot)   { return base[slot - 1].inscr; }
byte     etatBadge(int slot)          { return base[slot - 1].etat; }

void ecrireCompteurBadge(int slot, uint16_t v) {
  base[slot - 1].cpt = v;
  EEPROM.put(adresseEntree(slot - 1) + 4, v);      // 2 octets, 1 seul effacement
}

void ecrireEtatBadge(int slot, byte e) {
  base[slot - 1].etat = e;
  EEPROM.update(adresseEntree(slot - 1) + 8, e);
}

void ecrireCompteurAdmin(uint16_t v) {
  entete.cptAdmin = v;
  sauverEntete();
}

// Efface toute l'EEPROM (8 blocs de 1 Ko, 1 effacement par bloc)
void effacerEEPROM() {
  static uint8_t zero[TAILLE_BLOC];               // initialisé à 0
  for (unsigned int b = 0; b < EEPROM.length() / TAILLE_BLOC; b++) {
    EEPROM.put(b * TAILLE_BLOC, zero);
  }
  memset(&entete, 0, sizeof(entete));
}

// Numéro d'inscription « au hasard » (bruit de l'entrée analogique A0 + temps)
uint16_t nouvelleInscription() {
  uint16_t v = (uint16_t)micros();
  for (byte i = 0; i < 8; i++) v = (v << 3) ^ (v >> 13) ^ analogRead(A0) ^ (uint16_t)micros();
  return v;
}

bool checkTwo(const byte a[], const byte b[]) {
  for (byte k = 0; k < TAILLE_UID; k++) {
    if (a[k] != b[k]) return false;
  }
  return true;
}

// Renvoie le n° d'emplacement du badge (1..n), ou 0 s'il est absent
int findIDSLOT(const byte uid[]) {
  for (int i = 0; i < entete.nb; i++) {
    if (checkTwo(base[i].uid, uid)) return i + 1;
  }
  return 0;
}

bool isMaster(const byte uid[]) {
  return checkTwo(uid, masterCard);
}

// Ajoute un badge (déjà préparé) avec compteur 0. false si la base est pleine.
bool ajouterBadge(const byte uid[], uint16_t inscr) {
  if (entete.nb >= MAX_BADGES) return false;
  int i = entete.nb;
  memcpy(base[i].uid, uid, TAILLE_UID);
  base[i].cpt = 0;
  base[i].inscr = inscr;
  base[i].etat = ETAT_ACTIF;
  sauverEntree(i);
  entete.nb++;
  sauverEntete();                                 // nombre en dernier (coupure secteur)
  return true;
}

// Retire l'entrée n° slot : le dernier badge prend sa place (2 écritures seulement)
void supprimerEntree(int slot) {
  int i = slot - 1;
  int dernier = entete.nb - 1;
  if (i != dernier) {
    base[i] = base[dernier];
    sauverEntree(i);
  }
  entete.nb--;
  sauverEntete();
}


// ============================================================================
//               Moniteur série : lecture de la base (LECTURE SEULE)
// ============================================================================
// Tout est relu directement dans l'EEPROM (et non dans la copie en RAM) :
// on voit exactement ce qui est enregistré dans la flash.

void hex2(byte v) { if (v < 0x10) Serial.print('0'); Serial.print(v, HEX); }
void hex4(uint16_t v) { hex2(v >> 8); hex2(v & 0xFF); }
void uid4(const uint8_t *u) { for (byte i = 0; i < 4; i++) { hex2(u[i]); if (i < 3) Serial.print(' '); } }

// Affiche un nombre aligné à droite sur « largeur » caractères
void nombre(unsigned long v, byte largeur) {
  unsigned long p = 1;
  for (byte i = 1; i < largeur; i++) p *= 10;
  while (p > 1 && v < p) { Serial.print(' '); p /= 10; }
  Serial.print(v);
}

// Adresse de la fiche n° i selon la version lue dans l'EEPROM
int adresseEntreeLue(int i, byte version) {
  return (version == 1) ? adresseEntreeV1(i) : adresseEntree(i);
}

void afficherAideSerie() {
  Serial.println(F("Commandes serie : \"r\" base - \"d\" en-tete - \"f\" fiches - \"D\" vidage complet - \"h\" aide"));
}

void afficherBaseSerie() {
  Entete e;
  EEPROM.get(0, e);
  Serial.println();
  Serial.println(F("=================================================="));
  Serial.println(F("   BASE DE BADGES - UNO R4 (EEPROM simulee 8 Ko)"));
  Serial.println(F("=================================================="));
  Serial.print(F("Mode actuel        : "));
  Serial.println(modeProg ? F("PROGRAMMATION") : F("normal"));

  if (e.flag != CODE_ADMIN) {
    Serial.print(F("Drapeau (0x0000)   : 0x"));
    hex2(e.flag);
    Serial.println(F("  -> aucun badge administrateur (base vide ou effacee)"));
    Serial.println(F("=================================================="));
    afficherAideSerie();
    return;
  }

  Serial.print(F("Version de la base : "));
  Serial.print(e.version);
  if (e.version == 2)      Serial.println(F("  (fiches les unes derriere les autres)"));
  else if (e.version == 1) Serial.println(F("  (ancienne : 7 blocs, sera convertie au prochain demarrage)"));
  else { Serial.println(F("  -> version INCONNUE, lecture impossible")); afficherAideSerie(); return; }

  int maxBadges = (e.version == 1) ? MAX_BADGES_V1 : MAX_BADGES;
  Serial.print(F("Capacite maximale  : "));
  Serial.print(maxBadges);
  Serial.println(F(" badges"));
  Serial.print(F("Badge ADMIN        : "));
  uid4(e.uidAdmin);
  Serial.print(F("   compteur "));
  Serial.print(e.cptAdmin);
  Serial.print(F("   inscription 0x"));
  hex4(e.inscrAdmin);
  Serial.println();
  Serial.print(F("Nombre de badges   : "));
  Serial.println(e.nb);
  if (e.nb > maxBadges) { Serial.println(F("!! Nombre INCOHERENT")); afficherAideSerie(); return; }
  if (e.nb == 0) {
    Serial.println(F("Aucun badge autorise dans la base."));
    Serial.println(F("=================================================="));
    afficherAideSerie();
    return;
  }

  Serial.println();
  Serial.println(F("  No  Bloc  Adresse  UID           Passages  Inscr.  Etat"));
  Serial.println(F(" ---  ----  -------  -----------   --------  ------  ----------------------"));
  int bloques = 0;
  for (int i = 0; i < e.nb; i++) {
    Entree b;
    int adr = adresseEntreeLue(i, e.version);
    EEPROM.get(adr, b);
    nombre(i + 1, 4);
    Serial.print(F("     "));
    Serial.print(adr / TAILLE_BLOC);
    Serial.print(F("   0x"));
    hex4(adr);
    Serial.print(F("  "));
    uid4(b.uid);
    Serial.print(F("   "));
    nombre(b.cpt, 8);
    Serial.print(F("  0x"));
    hex4(b.inscr);
    Serial.print(F("  "));
    if (b.etat == ETAT_ACTIF)       Serial.print(F("actif"));
    else if (b.etat == ETAT_BLOQUE) { Serial.print(F("BLOQUE (copie detectee)")); bloques++; }
    else                            { Serial.print(F("inconnu 0x")); hex2(b.etat); }
    if (memcmp(b.uid, e.uidAdmin, 4) == 0) Serial.print(F("  (identique au badge ADMIN !)"));
    Serial.println();
  }
  if (bloques) {
    Serial.println();
    Serial.print(bloques);
    Serial.println(F(" badge(s) BLOQUE(S) : pour reactiver, le retirer puis le reenregistrer."));
  }
  Serial.println(F("=================================================="));
  afficherAideSerie();
}

// Vidage hexadécimal des adresses debut à fin (exclue), par lignes de 16 octets
void vidage(unsigned int debut, unsigned int fin) {
  Serial.println();
  Serial.println(F("  Adr   00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F"));
  for (unsigned int ligne = debut & ~0x0F; ligne < fin; ligne += 16) {
    Serial.print(F("0x"));
    hex4(ligne);
    Serial.print(F("  "));
    for (byte i = 0; i < 16; i++) { hex2(EEPROM.read(ligne + i)); Serial.print(' '); }
    Serial.println();
  }
}

// Vidage de la zone des fiches réellement utilisées (version 2)
void vidageFiches() {
  Entete e;
  EEPROM.get(0, e);
  if (e.flag != CODE_ADMIN || e.version != 2 || e.nb == 0 || e.nb > MAX_BADGES) {
    Serial.println(F("Pas de fiches a afficher (base vide, inconnue ou en version 1)"));
    return;
  }
  vidage(ADR_PREMIER_BADGE, ADR_PREMIER_BADGE + e.nb * TAILLE_ENTREE);
}

// Lecture des commandes du moniteur série (appelée dans loop et pendant les attentes)
void gererCommandesSerie() {
  static bool ligneVide = true;   // aucun caractère tapé depuis la dernière fin de ligne
  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'r': case 'R': afficherBaseSerie();          break;
      case 'd':           vidage(0, TAILLE_BLOC);       break;   // en-tête (bloc 0)
      case 'D':           vidage(0, EEPROM.length());   break;   // complet (8 Ko)
      case 'f': case 'F': vidageFiches();               break;
      case 'h': case 'H': case '?': afficherAideSerie(); break;
      case '\r': break;
      case '\n':                                       // Entrée seule -> résumé
        if (ligneVide) afficherBaseSerie();
        ligneVide = true;
        break;
      default: break;   // espaces et autres caractères ignorés
    }
    if (c != '\r' && c != '\n') ligneVide = false;
  }
}

// Affiche automatiquement le résumé quand le moniteur série vient d'être ouvert.
// Possible seulement sur l'UNO R4 Minima (USB natif : le signal DTR indique
// l'ouverture du port). Sur l'UNO R4 WiFi, Serial passe par le module ESP32
// et paraît toujours « connecté » : la détection est alors désactivée.
#if defined(ARDUINO_MINIMA) || defined(ARDUINO_UNOR4_MINIMA)
  #define DETECTION_MONITEUR 1
#else
  #define DETECTION_MONITEUR 0
#endif

bool moniteurConnecte = false;

void surveillerMoniteur() {
#if DETECTION_MONITEUR
  static bool enAttente = false;
  static unsigned long depuis = 0;
  bool connecte = (bool)Serial;

  if (connecte && !moniteurConnecte) {      // le moniteur vient d'être ouvert
    enAttente = true;
    depuis = millis();
  }
  if (!connecte) enAttente = false;
  moniteurConnecte = connecte;

  // petite pause pour laisser le moniteur se préparer (sans bloquer la porte)
  if (enAttente && millis() - depuis >= 300) {
    enAttente = false;
    afficherBaseSerie();
  }
#endif
}


// ============================================================================
//                        Opérations sur le badge (secteur 1)
// ============================================================================
void afficherUID(const byte uid[], byte taille) {
  for (byte i = 0; i < taille; i++) {
    hex2(uid[i]);
    Serial.print(' ');
  }
  Serial.println();
}

// Lit un nouveau badge (UID dans readCard). Le badge reste sélectionné :
// appeler terminerCarte() une fois le traitement fini.
bool getID() {
  if (!mfrc522.PICC_IsNewCardPresent()) return false;
  if (!mfrc522.PICC_ReadCardSerial())   return false;

  byte taille = mfrc522.uid.size;
  byte debut = (UID_LONG_4_DERNIERS && taille > TAILLE_UID) ? taille - TAILLE_UID : 0;

  Serial.print(F("Badge scanne ("));
  Serial.print(taille);
  Serial.print(F(" octets) UID: "));
  afficherUID(mfrc522.uid.uidByte, taille);

  for (byte i = 0; i < TAILLE_UID; i++) {
    readCard[i] = mfrc522.uid.uidByte[debut + i];
  }
  return true;
}

void terminerCarte() {
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

bool estMifareClassic() {
  MFRC522::PICC_Type t = mfrc522.PICC_GetType(mfrc522.uid.sak);
  return t == MFRC522::PICC_TYPE_MIFARE_MINI || t == MFRC522::PICC_TYPE_MIFARE_1K ||
         t == MFRC522::PICC_TYPE_MIFARE_4K;
}

void cleUsine(MFRC522::MIFARE_Key &k) {
  for (byte i = 0; i < 6; i++) k.keyByte[i] = 0xFF;
}

void cleBadge(const byte uid[4], byte domaine, MFRC522::MIFARE_Key &k) {
  deriver(domaine, uid, 0, 0, 0, k.keyByte, 6);
}

bool authentifier(MFRC522::MIFARE_Key &k) {
  return mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, BLOC_VERROU,
                                  &k, &mfrc522.uid) == MFRC522::STATUS_OK;
}

// Après un échec d'authentification, le badge ne répond plus : on le réveille
bool reselectionner() {
  mfrc522.PCD_StopCrypto1();
  byte atqa[2];
  byte taille = sizeof(atqa);
  if (mfrc522.PICC_WakeupA(atqa, &taille) != MFRC522::STATUS_OK) return false;
  return mfrc522.PICC_Select(&mfrc522.uid) == MFRC522::STATUS_OK;
}

void preparerBloc(const byte uid[4], uint16_t cpt, uint16_t inscr, byte bloc[16]) {
  memset(bloc, 0, 16);
  bloc[0] = 'C';
  bloc[1] = 'A';
  bloc[2] = (byte)cpt;
  bloc[3] = (byte)(cpt >> 8);
  bloc[4] = (byte)inscr;
  bloc[5] = (byte)(inscr >> 8);
  deriver(3, uid, cpt, inscr, 1, bloc + 6, 8);
}

bool ecrireBlocDonnees(const byte uid[4], uint16_t cpt, uint16_t inscr) {
  byte bloc[16];
  preparerBloc(uid, cpt, inscr, bloc);
  return mfrc522.MIFARE_Write(BLOC_DONNEES, bloc, 16) == MFRC522::STATUS_OK;
}

bool ecrireVerrou(MFRC522::MIFARE_Key &a, MFRC522::MIFARE_Key &b) {
  byte bloc[16];
  memcpy(bloc, a.keyByte, 6);
  bloc[6] = 0xFF; bloc[7] = 0x07; bloc[8] = 0x80; bloc[9] = 0x69;   // droits d'accès standard
  memcpy(bloc + 10, b.keyByte, 6);
  return mfrc522.MIFARE_Write(BLOC_VERROU, bloc, 16) == MFRC522::STATUS_OK;
}

// Lit et vérifie le compteur signé du badge (authentification avec sa clé)
Resultat lireCompteurCarte(const byte uid[4], uint16_t &cpt, uint16_t &inscr) {
  MFRC522::MIFARE_Key k;
  cleBadge(uid, 1, k);
  if (!authentifier(k)) return R_ECHEC;
  byte buf[18];
  byte taille = sizeof(buf);
  if (mfrc522.MIFARE_Read(BLOC_DONNEES, buf, &taille) != MFRC522::STATUS_OK) return R_ECHEC;
  if (buf[0] != 'C' || buf[1] != 'A') return R_FAUX;
  cpt = buf[2] | ((uint16_t)buf[3] << 8);
  inscr = buf[4] | ((uint16_t)buf[5] << 8);
  byte attendu[16];
  preparerBloc(uid, cpt, inscr, attendu);
  if (memcmp(buf, attendu, 16) != 0) return R_FAUX;   // signature invalide
  return R_OK;
}

// Passage d'un badge enregistré : vérifie inscription et compteur, avance de 1.
// cptBase / inscrBase = valeurs mémorisées ; nouveau = compteur à mémoriser si R_OK.
Resultat utiliserCarte(const byte uid[4], uint16_t cptBase, uint16_t inscrBase, uint16_t &nouveau) {
  if (!estMifareClassic()) return R_INCOMPATIBLE;
  uint16_t cpt, inscr;
  Resultat r = lireCompteurCarte(uid, cpt, inscr);
  if (r != R_OK) return r;
  if (inscr != inscrBase) {                        // copie d'une ancienne inscription
    Serial.println(F("Numero d'inscription different : ancienne copie du badge"));
    return R_FAUX;
  }
  uint16_t avance = (uint16_t)(cpt - cptBase);    // calcul modulo 65536
  if (avance > AVANCE_MAX) {
    Serial.print(F("Compteur badge = "));
    Serial.print(cpt);
    Serial.print(F(" / base = "));
    Serial.println(cptBase);
    return R_SUSPECT;
  }
  nouveau = cpt + 1;
  if (!ecrireBlocDonnees(uid, nouveau, inscr)) return R_ECHEC;   // badge inchangé : rien de perdu
  return R_OK;
}

// Prépare un badge neuf (clé d'usine) ou déjà préparé par ce système :
// compteur à 0 et nouveau numéro d'inscription
Resultat initialiserCarte(const byte uid[4], uint16_t inscr) {
  if (!estMifareClassic()) return R_INCOMPATIBLE;
  MFRC522::MIFARE_Key a, b;
  bool neuf = true;
  cleUsine(a);
  if (!authentifier(a)) {
    neuf = false;
    cleBadge(uid, 1, a);
    if (!reselectionner() || !authentifier(a)) return R_CLE_INCONNUE;
  }
  if (!ecrireBlocDonnees(uid, 0, inscr)) return R_ECHEC;   // données d'abord…
  if (neuf) {
    cleBadge(uid, 1, a);
    cleBadge(uid, 2, b);
    if (!ecrireVerrou(a, b)) return R_ECHEC;        // …clés ensuite
  }
  return R_OK;
}

// Remet le badge en configuration d'usine (bloc 4 à zéro, clés FF…FF)
bool remettreUsine(const byte uid[4]) {
  MFRC522::MIFARE_Key a, b;
  cleBadge(uid, 1, a);
  if (!authentifier(a)) return false;
  byte zero[16];
  memset(zero, 0, 16);
  if (mfrc522.MIFARE_Write(BLOC_DONNEES, zero, 16) != MFRC522::STATUS_OK) return false;
  cleUsine(a);
  cleUsine(b);
  return ecrireVerrou(a, b);
}

// ---- Versions avec réessais : on recommence tant que l'échec est dû à la lecture ----
// (R_ECHEC = authentification ou lecture/écriture impossible, souvent badge encore en
//  approche). Les autres résultats (faux, suspect, incompatible) sont définitifs.
bool nouvelEssai(byte essai) {
  if (essai == 0) return true;
  delay(PAUSE_ESSAI);
  if (!reselectionner()) return false;
  Serial.print(F("  nouvelle tentative de lecture "));
  Serial.println(essai + 1);
  return true;
}

Resultat utiliserCarteEssais(const byte uid[4], uint16_t cptBase, uint16_t inscrBase, uint16_t &nouveau) {
  Resultat r = R_ECHEC;
  for (byte e = 0; e < ESSAIS_LECTURE; e++) {
    if (!nouvelEssai(e)) continue;
    r = utiliserCarte(uid, cptBase, inscrBase, nouveau);
    if (r != R_ECHEC) break;
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
    if (remettreUsine(uid)) return true;
  }
  return false;
}

void ShowReaderDetails() {
  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  Serial.print(F("MFRC522 Version: 0x"));
  Serial.print(v, HEX);
  if      (v == 0x91) Serial.print(F(" = v1.0"));
  else if (v == 0x92) Serial.print(F(" = v2.0"));
  else if (v == 0x82 || v == 0x88 || v == 0x12) Serial.print(F(" = compatible (clone)"));
  else                Serial.print(F(" (Inconnu)"));
  Serial.println();

  if (v == 0x00 || v == 0xFF) {
    Serial.println(F("ATTENTION: Erreur de Communication"));
    Serial.println(F("Verifier que le RFID-MFRC522 soit correctement connecte"));
    Serial.println(F("Les commandes serie restent disponibles pour lire l'EEPROM"));
    afficherAideSerie();
    afficher(F("ATTENTION"), F("Erreur Communication"), F("Lecteur RFID"), nullptr);
    allumerSeule(PIN_LED_ROUGE);
    while (true) {
      gererCommandesSerie();   // arrêt, mais lecture de la base toujours possible
    }
  }
}


// ============================================================================
//                        BP d'ouverture (anti-rebond)
// ============================================================================
bool bpOuvertureAppuye() {
  static bool etatStable = HIGH;
  static bool dernierLu = HIGH;
  static unsigned long dernierChangement = 0;

  bool lu = digitalRead(PIN_BP_OUVERTURE);
  if (lu != dernierLu) {
    dernierLu = lu;
    dernierChangement = millis();
  }
  if (millis() - dernierChangement >= ANTI_REBOND) {
    etatStable = dernierLu;
  }
  return etatStable == LOW;
}


// ============================================================================
//                         Actions (ouverture, refus, prog)
// ============================================================================
void fermerPorte() {
  digitalWrite(PIN_SERRURE, LOW);
  Serial.println(F("Fermeture de la Porte"));
  allumerSeule(PIN_LED_ROUGE);
  afficher(F("Porte FERMEE...."), nullptr, nullptr, nullptr);
  entrerMessage();
}

void ouvrirParBadge() {
  Serial.println(F("Bonjour, Acces Autorise - Ouverture de la Porte"));
  afficher(F("      BONJOUR"),
           F("** Acces Autorise **"),
           F("  Ouverture Porte"),
           F("     **********"));
  allumerSeule(PIN_LED_VERTE);
  digitalWrite(PIN_SERRURE, HIGH);
  bipAutorise();
  etat = OUVERT_BADGE;
  debutEtat = millis();
}

void ouvrirParBP() {
  Serial.println(F("Ouverture forcee par BP"));
  afficher(F(" Ouverture Forcee"), F("     Par BP"), nullptr, nullptr);
  allumerSeule(PIN_LED_VERTE);
  tone(PIN_BUZZ, 1500, 100);
  digitalWrite(PIN_SERRURE, HIGH);
  etat = OUVERT_BP;
  debutEtat = millis();
}

void refuser() {
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
    case R_SUSPECT:
      Serial.println(F("ALERTE : compteur en retard -> COPIE du badge detectee"));
      afficher(F("** Badge SUSPECT **"), F("  Copie detectee"),
               F("   Badge BLOQUE"), F("Voir administrateur"));
      lancerClignotement(PIN_LED_ROUGE, 6);
      bipAlerte();
      entrerMessageDuree(DUREE_MESSAGE_ALERTE);
      return;
    case R_FAUX:
      Serial.println(F("Refus : donnees du badge invalides (non authentique)"));
      afficher(F("** Acces REFUSE **"), F("Badge NON"), F("AUTHENTIQUE"), nullptr);
      break;
    case R_INCOMPATIBLE:
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

void afficherBloque() {
  Serial.println(F("Refus : badge BLOQUE (copie detectee precedemment)"));
  afficher(F("** Acces REFUSE **"), F("   Badge BLOQUE"),
           F("Voir administrateur"), nullptr);
  allumerSeule(PIN_LED_ROUGE);
  bipRefus();
  entrerMessageDuree(DUREE_MESSAGE_ALERTE);
}

void entrerProgrammation() {
  modeProg = true;
  derniereActionProg = millis();
  Serial.println(F("Entree dans le mode Programmation"));
  Serial.print(F("Il y a "));
  Serial.print(entete.nb);
  Serial.println(F(" badge(s) dans la base"));
  afficher(F("Entre dans le Mode"), F("  PROGRAMMATION..."), nullptr, nullptr);
  entrerMessage();
}

void sortirProgrammation(bool parTimeout) {
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
  if (entete.nb >= MAX_BADGES) {
    Serial.println(F("ERREUR : base pleine"));
    afficher(F("....ERREUR!....."), F("  Base pleine"), F("Badge non enregistre"), nullptr);
    lancerClignotement(PIN_LED_ROUGE, 3);
    entrerMessage();
    return;
  }
  uint16_t inscr = nouvelleInscription();
  Resultat r = initialiserCarteEssais(readCard, inscr);
  if (r == R_OK && ajouterBadge(readCard, inscr)) {
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
  bool usine = remettreUsineEssais(readCard);
  supprimerEntree(slot);
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
  uint16_t nouveau = 0;

  // ---- Badge administrateur : toujours vérifié (clé + compteur) ----
  if (isMaster(readCard)) {
    if (modeProg) derniereActionProg = millis();
    Resultat r = utiliserCarteEssais(readCard, entete.cptAdmin, entete.inscrAdmin, nouveau);
    if (r != R_OK) {
      Serial.println(F("Badge ADMIN refuse"));
      signalerEchec(r == R_SUSPECT ? R_FAUX : r);   // l'admin n'est jamais bloqué
      return;
    }
    ecrireCompteurAdmin(nouveau);
    if (modeProg) sortirProgrammation(false);
    else          entrerProgrammation();
    return;
  }

  int slot = findIDSLOT(readCard);

  // ---- Mode programmation : ajout ou retrait ----
  if (modeProg) {
    derniereActionProg = millis();
    if (slot) retirerBadge(slot);
    else      enregistrerBadge();
    return;
  }

  // ---- Mode normal ----
  if (slot == 0)                          { refuser(); return; }
  if (etatBadge(slot) == ETAT_BLOQUE)     { afficherBloque(); return; }

  Resultat r = utiliserCarteEssais(readCard, compteurBadge(slot), inscriptionBadge(slot), nouveau);
  if (r == R_OK) {
    ecrireCompteurBadge(slot, nouveau);
    ouvrirParBadge();
  } else {
    if (r == R_SUSPECT) ecrireEtatBadge(slot, ETAT_BLOQUE);
    signalerEchec(r);
  }
}


// ============================================================================
//                     Fonctions de démarrage (setup)
// ============================================================================
void demandeEffacement() {
  digitalWrite(PIN_LED_ROUGE, HIGH);
  Serial.println(F("BP effacement de la base appuye"));
  Serial.println(F("Relacher dans les 5 s pour annuler"));
  afficher(F("** BP RAZ Appuye **"), nullptr, F("      Vous avez"), F("    s pour annuler"));

  for (int compteur = 5; compteur > 0; compteur--) {
    lcd.setCursor(3, 3);
    lcd.print(compteur);
    delay(1000);
  }

  if (digitalRead(PIN_BP_RAZ) == LOW) {
    Serial.println(F("EFFACEMENT DE LA BASE"));
    afficher(nullptr, F("     EFFACEMENT"), F("     DE LA BASE"), nullptr);
    effacerEEPROM();
    Serial.println(F("Base effacee"));
    afficher(F("DATA BASE Effacee"), nullptr, nullptr, nullptr);
    for (byte i = 0; i < 3; i++) {
      digitalWrite(PIN_LED_ROUGE, LOW);
      delay(200);
      digitalWrite(PIN_LED_ROUGE, HIGH);
      delay(200);
    }
  } else {
    Serial.println(F("Effacement annule"));
    afficher(nullptr, F("  Effacement Annule"), nullptr, nullptr);
    delay(1000);
  }
  digitalWrite(PIN_LED_ROUGE, LOW);
}

void verifierBase() {
  chargerBase();
  if (entete.flag != CODE_ADMIN && (EEPROM.read(1) == 0xA5 || EEPROM.read(1) == 128)) {
    Serial.println(F("Base d'une autre version detectee : badges a reenregistrer"));
    afficher(F("Ancienne base"), F("detectee :"), F("badges a"), F("reenregistrer"));
    delay(3000);
  }
}

void enregistrerAdmin() {
  uint16_t inscr = 0;
  Serial.println(F("Pas de Badge Administrateur"));
  while (true) {
    Serial.println(F("Placer un badge MIFARE Classic pour le definir comme badge administrateur"));
    afficher(F("    Pas de Badge"),
             F("   Administrateur"),
             F("     Enregistre"),
             F("  Placer une Carte"));

    while (!getID()) {
      gererCommandesSerie();         // lecture EEPROM possible pendant l'attente
      digitalWrite(PIN_LED_BLEUE, HIGH);
      delay(200);
      digitalWrite(PIN_LED_BLEUE, LOW);
      delay(200);
    }

    inscr = nouvelleInscription();
    Resultat r = initialiserCarteEssais(readCard, inscr);
    terminerCarte();
    if (r == R_OK) break;

    Serial.print(F("Badge refuse, code "));
    Serial.println((int)r);
    if (r == R_INCOMPATIBLE || r == R_CLE_INCONNUE)
      afficher(F("....ERREUR!....."), F("Badge non compatible"), F("Essayez un autre"), F("badge"));
    else
      afficher(F("....ERREUR!....."), F("Ecriture impossible"), F("Representez le"), F("badge"));
    bipRefus();
    delay(2500);
  }

  entete.flag = CODE_ADMIN;
  entete.version = VERSION_BASE;
  entete.nb = 0;
  memcpy(entete.uidAdmin, readCard, TAILLE_UID);
  entete.cptAdmin = 0;
  entete.inscrAdmin = inscr;
  sauverEntete();                                 // en-tête complet en une seule écriture
  Serial.println(F("Badge Administrateur Enregistre"));
  afficher(F("Badge Administrateur"), F("    Enregistre"), nullptr, nullptr);
  delay(1500);
}


// ============================================================================
//                                  SETUP
// ============================================================================
void setup() {
  pinMode(PIN_SERRURE, OUTPUT);
  digitalWrite(PIN_SERRURE, LOW);
  pinMode(PIN_LED_ROUGE, OUTPUT);
  pinMode(PIN_LED_VERTE, OUTPUT);
  pinMode(PIN_LED_BLEUE, OUTPUT);
  pinMode(PIN_BUZZ, OUTPUT);
  ledsOff();

  pinMode(PIN_BP_RAZ, INPUT_PULLUP);
  pinMode(PIN_BP_OUVERTURE, INPUT_PULLUP);

  Serial.begin(115200);
  lcd.begin(20, 4);
  lcd.backlight();

  SPI.begin();
  mfrc522.PCD_Init();
  // Pour augmenter la distance de lecture :
  // mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);

  Serial.println(F("Lecteur de Badges - version securisee (secret + compteur) - UNO R4"));
  ShowReaderDetails();

  if (digitalRead(PIN_BP_RAZ) == LOW) {
    demandeEffacement();
  }

  verifierBase();

  if (entete.flag != CODE_ADMIN) {
    enregistrerAdmin();
  }

  for (byte i = 0; i < TAILLE_UID; i++) {
    masterCard[i] = entete.uidAdmin[i];
  }
  Serial.println(F("-------------------"));
  Serial.print(F("Badge Administrateur UID = "));
  afficherUID(masterCard, TAILLE_UID);
  Serial.print(F("Badges en base : "));
  Serial.print(entete.nb);
  Serial.print(F(" / "));
  Serial.println(nbBadgesMax());
  Serial.println(F("Lecteur Pret - En attente de lecture de Badge"));
  Serial.println(F("-------------------"));
  afficherBaseSerie();                    // résumé de la base + commandes
  moniteurConnecte = (bool)Serial;        // évite un 2e affichage si le moniteur est déjà ouvert

  afficherEcranAttente();
}


// ============================================================================
//                                   LOOP
// ============================================================================
void loop() {
  unsigned long maintenant = millis();
  bool bp = bpOuvertureAppuye();
  if (!bp) bpDoitEtreRelache = false;

  switch (etat) {

    case ATTENTE:
    case MESSAGE:
      digitalWrite(PIN_SERRURE, LOW);   // sécurité : porte fermée hors ouverture

      if (etat == MESSAGE && maintenant - debutEtat >= dureeMessage) {
        etat = ATTENTE;
        afficherEcranAttente();
      }

      if (bp && !bpDoitEtreRelache) {
        ouvrirParBP();
        break;
      }

      if (etat == ATTENTE) {
        if (modeProg && maintenant - derniereActionProg >= TIMEOUT_PROGRAMMATION) {
          sortirProgrammation(true);
          break;
        }
        if (getID()) {
          traiterBadge();
          terminerCarte();
        }
      }
      break;

    case OUVERT_BADGE:
      if (maintenant - debutEtat >= DUREE_OUVERTURE_BADGE) {
        fermerPorte();
      }
      break;

    case OUVERT_BP: {
      unsigned long duree = maintenant - debutEtat;
      bool finNormale = (duree >= DUREE_OUVERTURE_BP_MIN) && !bp;
      bool finSecurite = (duree >= DUREE_OUVERTURE_BP_MAX);
      if (finNormale || finSecurite) {
        if (bp) {
          bpDoitEtreRelache = true;
          Serial.println(F("BP maintenu trop longtemps : fermeture de securite"));
        }
        fermerPorte();
      }
      break;
    }
  }

  surveillerMoniteur();    // résumé automatique à l'ouverture du moniteur (R4 Minima)
  gererCommandesSerie();   // commandes "r", "d", "f", "D", "h", Entrée du moniteur série
  gererLeds();
}
