/* ============================================================================
 *  Contrôle d'accès RFID SÉCURISÉ (MFRC522) + LCD 20x4 I2C (librairie hd44780)
 *  Version « secret + compteur » pour badges MIFARE Classic 1K/4K
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
const unsigned long DUREE_OUVERTURE_BP_MAX = 70000UL; // ms, sécurité si BP coincé
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
const int  ADR_NB_BADGES     = 0x00;
const int  ADR_FLAG_ADMIN    = 0x01;
const int  ADR_UID_ADMIN     = 0x02;
const int  ADR_CPT_ADMIN     = 0x06;
const int  ADR_INSCR_ADMIN   = 0x08;
const int  ADR_PREMIER_BADGE = 0x10;
const byte CODE_ADMIN        = 0xA5;   // format sécurisé
const byte CODE_ANCIEN       = 128;    // ancienne version (UID seul)
const byte TAILLE_UID        = 4;
const byte TAILLE_ENTREE     = 9;      // UID 4 + compteur 2 + inscription 2 + état 1
const byte ETAT_ACTIF        = 0;
const byte ETAT_BLOQUE       = 1;

// ------------------------------- Objets -------------------------------------
MFRC522 mfrc522(PIN_RC522_SS, PIN_RC522_RST);
hd44780_I2Cexp lcd;   // adresse I2C détectée automatiquement

// ------------------------------- Variables ----------------------------------
byte readCard[TAILLE_UID];    // UID du badge qui vient d'être lu
byte masterCard[TAILLE_UID];  // UID du badge administrateur

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
    lcd.print(EEPROM.read(ADR_NB_BADGES));
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
  int places = (EEPROM.length() - ADR_PREMIER_BADGE) / TAILLE_ENTREE;
  return (places > 254) ? 254 : places;
}

int adresseBadge(int slot) {                 // slot de 1 à nombre de badges
  return ADR_PREMIER_BADGE + (slot - 1) * TAILLE_ENTREE;
}

uint16_t lire16(int adr) { return EEPROM.read(adr) | ((uint16_t)EEPROM.read(adr + 1) << 8); }
void ecrire16(int adr, uint16_t v) { EEPROM.update(adr, (byte)v); EEPROM.update(adr + 1, (byte)(v >> 8)); }

uint16_t compteurBadge(int slot)            { return lire16(adresseBadge(slot) + 4); }
void     ecrireCompteurBadge(int slot, uint16_t v) { ecrire16(adresseBadge(slot) + 4, v); }
uint16_t inscriptionBadge(int slot)        { return lire16(adresseBadge(slot) + 6); }
byte     etatBadge(int slot)                { return EEPROM.read(adresseBadge(slot) + 8); }
void     ecrireEtatBadge(int slot, byte e)  { EEPROM.update(adresseBadge(slot) + 8, e); }

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
  int count = EEPROM.read(ADR_NB_BADGES);
  for (int i = 1; i <= count; i++) {
    int adr = adresseBadge(i);
    bool egal = true;
    for (byte j = 0; j < TAILLE_UID; j++) {
      if (EEPROM.read(adr + j) != uid[j]) { egal = false; break; }
    }
    if (egal) return i;
  }
  return 0;
}

bool isMaster(const byte uid[]) {
  return checkTwo(uid, masterCard);
}

// Ajoute un badge (déjà préparé) avec compteur 0. false si la base est pleine.
bool ajouterBadge(const byte uid[], uint16_t inscr) {
  int num = EEPROM.read(ADR_NB_BADGES);
  if (num >= nbBadgesMax()) return false;
  int adr = adresseBadge(num + 1);
  for (byte j = 0; j < TAILLE_UID; j++) EEPROM.update(adr + j, uid[j]);
  ecrire16(adr + 4, 0);
  ecrire16(adr + 6, inscr);
  EEPROM.update(adr + 8, ETAT_ACTIF);
  EEPROM.update(ADR_NB_BADGES, num + 1);    // compteur en dernier (coupure secteur)
  return true;
}

// Retire l'entrée n° slot en décalant les suivantes
void supprimerEntree(int slot) {
  int num = EEPROM.read(ADR_NB_BADGES);
  for (int s = slot; s < num; s++) {
    int dest = adresseBadge(s);
    for (byte j = 0; j < TAILLE_ENTREE; j++) {
      EEPROM.update(dest + j, EEPROM.read(dest + TAILLE_ENTREE + j));
    }
  }
  int dernier = adresseBadge(num);
  for (byte j = 0; j < TAILLE_ENTREE; j++) EEPROM.update(dernier + j, 0);
  EEPROM.update(ADR_NB_BADGES, num - 1);
}


// ============================================================================
//                        Opérations sur le badge (secteur 1)
// ============================================================================
void afficherUID(const byte uid[], byte taille) {
  for (byte i = 0; i < taille; i++) {
    if (uid[i] < 0x10) Serial.print('0');
    Serial.print(uid[i], HEX);
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
    afficher(F("ATTENTION"), F("Erreur Communication"), F("Lecteur RFID"), nullptr);
    allumerSeule(PIN_LED_ROUGE);
    while (true);
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
  Serial.print(EEPROM.read(ADR_NB_BADGES));
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
  if (EEPROM.read(ADR_NB_BADGES) >= nbBadgesMax()) {
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
    Resultat r = utiliserCarteEssais(readCard, lire16(ADR_CPT_ADMIN), lire16(ADR_INSCR_ADMIN), nouveau);
    if (r != R_OK) {
      Serial.println(F("Badge ADMIN refuse"));
      signalerEchec(r == R_SUSPECT ? R_FAUX : r);   // l'admin n'est jamais bloqué
      return;
    }
    ecrire16(ADR_CPT_ADMIN, nouveau);
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
    for (unsigned int x = 0; x < EEPROM.length(); x++) {
      EEPROM.update(x, 0);
    }
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
  byte flag = EEPROM.read(ADR_FLAG_ADMIN);
  if (flag == CODE_ANCIEN) {
    Serial.println(F("Ancienne base (UID seul) detectee : badges a reenregistrer"));
    afficher(F("Ancienne base"), F("detectee :"), F("badges a"), F("reenregistrer"));
    EEPROM.update(ADR_FLAG_ADMIN, 0);
    EEPROM.update(ADR_NB_BADGES, 0);
    delay(3000);
  }
  if (EEPROM.read(ADR_NB_BADGES) > nbBadgesMax()) {
    Serial.println(F("Compteur de badges invalide -> remis a 0"));
    EEPROM.update(ADR_NB_BADGES, 0);
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

  EEPROM.update(ADR_NB_BADGES, 0);
  for (byte j = 0; j < TAILLE_UID; j++) {
    EEPROM.update(ADR_UID_ADMIN + j, readCard[j]);
  }
  ecrire16(ADR_CPT_ADMIN, 0);
  ecrire16(ADR_INSCR_ADMIN, inscr);
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

  Serial.println(F("Lecteur de Badges - version securisee (secret + compteur)"));
  ShowReaderDetails();

  if (digitalRead(PIN_BP_RAZ) == LOW) {
    demandeEffacement();
  }

  verifierBase();

  if (EEPROM.read(ADR_FLAG_ADMIN) != CODE_ADMIN) {
    enregistrerAdmin();
  }

  for (byte i = 0; i < TAILLE_UID; i++) {
    masterCard[i] = EEPROM.read(ADR_UID_ADMIN + i);
  }
  Serial.println(F("-------------------"));
  Serial.print(F("Badge Administrateur UID = "));
  afficherUID(masterCard, TAILLE_UID);
  Serial.print(F("Badges en base : "));
  Serial.print(EEPROM.read(ADR_NB_BADGES));
  Serial.print(F(" / "));
  Serial.println(nbBadgesMax());
  Serial.println(F("Lecteur Pret - En attente de lecture de Badge"));
  Serial.println(F("-------------------"));

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

  gererLeds();
}
