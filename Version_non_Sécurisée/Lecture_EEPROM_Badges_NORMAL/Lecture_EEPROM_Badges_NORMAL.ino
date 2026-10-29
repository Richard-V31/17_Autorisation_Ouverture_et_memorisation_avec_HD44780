/* ============================================================================
 *  Lecture de la base de badges en EEPROM
 *  pour le programme Autorisation_Ouverture_NORMAL_HD44780 (identification par UID)
 *
 *  Programme en LECTURE SEULE : il n'écrit JAMAIS dans l'EEPROM.
 *  On peut donc le téléverser, consulter la base, puis remettre le programme
 *  de contrôle d'accès : les badges enregistrés sont conservés.
 *  (aucun câblage nécessaire : seule la carte Arduino est utilisée)
 *
 *  MONITEUR SÉRIE à 115200 bauds :
 *  - Le contenu de la base s'affiche au démarrage.
 *  - Commandes :
 *      r : réafficher le contenu de la base
 *      d : vidage brut complet de l'EEPROM (1024 octets en hexadécimal)
 *      h : rappel des commandes
 *
 *  Organisation de l'EEPROM (format version normale) :
 *    0x00        : nombre de badges enregistrés (hors badge admin)
 *    0x01        : 128 (0x80) si un badge admin est enregistré
 *    0x02 - 0x05 : UID du badge admin
 *    0x06 ...    : badges, 4 octets (UID) chacun
 *  Capacité : 254 badges.
 * ========================================================================== */

#include <EEPROM.h>                    // accès en lecture à l'EEPROM interne

const int  ADR_NB_BADGES     = 0x00;   // nombre de badges enregistrés (hors admin)
const int  ADR_FLAG_ADMIN    = 0x01;   // drapeau « admin enregistré »
const int  ADR_UID_ADMIN     = 0x02;   // 4 octets d'UID du badge admin
const int  ADR_PREMIER_BADGE = 0x06;   // début de la 1re fiche badge
const byte CODE_ADMIN        = 128;    // drapeau de la version normale (0x80)
const byte CODE_SECURISE     = 0xA5;   // drapeau de la version sécurisée (format différent)
const byte TAILLE_UID        = 4;      // octets d'UID par badge


// ------------------------------ Utilitaires ---------------------------------
int nbBadgesMax() {                    // (1024 - 6) / 4 = 254 sur une Uno
  int places = (EEPROM.length() - ADR_PREMIER_BADGE) / TAILLE_UID;
  return (places > 254) ? 254 : places;
}

int adresseBadge(int slot) {           // slot de 1 à nombre de badges
  return ADR_PREMIER_BADGE + (slot - 1) * TAILLE_UID;
}

void afficherHex(byte v) {             // octet en hexadécimal sur 2 caractères
  if (v < 0x10) Serial.print('0');
  Serial.print(v, HEX);
}

void afficherAdresse(int adr) {        // adresse au format 0x0000
  Serial.print(F("0x"));
  if (adr < 0x1000) Serial.print('0');
  if (adr < 0x100)  Serial.print('0');
  if (adr < 0x10)   Serial.print('0');
  Serial.print(adr, HEX);
}

void afficherUIDEeprom(int adr) {      // 4 octets d'UID lus à partir de adr
  for (byte i = 0; i < TAILLE_UID; i++) {
    afficherHex(EEPROM.read(adr + i));
    if (i < TAILLE_UID - 1) Serial.print(' ');
  }
}

void afficherNombre(unsigned long v, byte largeur) { // nombre aligné à droite
  unsigned long p = 1;
  for (byte i = 1; i < largeur; i++) p *= 10;
  while (p > 1 && v < p) { Serial.print(' '); p /= 10; }
  Serial.print(v);
}

bool memeUIDEeprom(int a, int b) {     // compare deux UID rangés en EEPROM
  for (byte i = 0; i < TAILLE_UID; i++) if (EEPROM.read(a + i) != EEPROM.read(b + i)) return false;
  return true;
}

bool uidVide(int adr) {                // UID 00 00 00 00 ou FF FF FF FF (fiche suspecte)
  bool zero = true, ff = true;
  for (byte i = 0; i < TAILLE_UID; i++) {
    byte v = EEPROM.read(adr + i);
    if (v != 0x00) zero = false;
    if (v != 0xFF) ff = false;
  }
  return zero || ff;
}


// --------------------------- Affichage de la base ---------------------------
void afficherAide() {
  Serial.println(F("Commandes : \"r\" = relire la base, \"d\" = vidage brut EEPROM, \"h\" = aide"));
}

void afficherBase() {
  byte flag = EEPROM.read(ADR_FLAG_ADMIN);
  byte nb   = EEPROM.read(ADR_NB_BADGES);
  int maxi  = nbBadgesMax();

  Serial.println();
  Serial.println(F("=================================================="));
  Serial.println(F("     CONTENU DE LA BASE DE BADGES (EEPROM)"));
  Serial.println(F("          format version NORMALE (UID)"));
  Serial.println(F("=================================================="));
  Serial.print(F("Taille EEPROM       : "));
  Serial.print(EEPROM.length());
  Serial.println(F(" octets"));
  Serial.print(F("Indicateur (0x01)   : 0x"));
  afficherHex(flag);
  Serial.println();

  if (flag == CODE_SECURISE) {         // base créée par la version sécurisée
    Serial.println(F("Base au format de la version SECURISEE : ce programme ne"));
    Serial.println(F("sait pas la decoder (envoyer \"d\" pour le vidage brut)."));
    Serial.println(F("=================================================="));
    afficherAide();
    return;
  }
  if (flag != CODE_ADMIN) {            // pas de badge admin
    Serial.println(F("Aucun badge administrateur enregistre (base vide ou effacee)."));
    Serial.println(F("=================================================="));
    afficherAide();
    return;
  }

  Serial.print(F("Capacite maximale   : "));
  Serial.print(maxi);
  Serial.println(F(" badges"));
  Serial.println();
  Serial.print(F("Badge ADMIN         : "));
  afficherUIDEeprom(ADR_UID_ADMIN);
  Serial.println();
  Serial.println();
  Serial.print(F("Nombre de badges    : "));
  Serial.println(nb);

  if (nb > maxi) {                     // valeur aberrante
    Serial.println(F("!! Compteur INCOHERENT : liste non affichee (envoyer \"d\")"));
  } else if (nb == 0) {
    Serial.println(F("Aucun badge autorise dans la base."));
  } else {
    Serial.println();
    Serial.println(F(" No   Adresse   UID"));
    Serial.println(F("----  -------   -----------"));
    byte doublons = 0;
    for (int n = 1; n <= nb; n++) {
      int adr = adresseBadge(n);
      afficherNombre(n, 3);
      Serial.print(F("   "));
      afficherAdresse(adr);
      Serial.print(F("    "));
      afficherUIDEeprom(adr);
      if (memeUIDEeprom(adr, ADR_UID_ADMIN)) Serial.print(F("  (identique au badge ADMIN !)"));
      if (uidVide(adr))                      Serial.print(F("  (fiche vide ?)"));
      for (int m = 1; m < n; m++) {          // même UID déjà présent plus haut ?
        if (memeUIDEeprom(adr, adresseBadge(m))) {
          Serial.print(F("  (doublon du No "));
          Serial.print(m);
          Serial.print(')');
          doublons++;
          break;
        }
      }
      Serial.println();
    }
    if (doublons) {
      Serial.println();
      Serial.print(doublons);
      Serial.println(F(" doublon(s) detecte(s)."));
    }
    Serial.println();
    Serial.print(F("Places libres       : "));
    Serial.println(maxi - nb);
  }
  Serial.println(F("=================================================="));
  afficherAide();
}

void vidageBrut() {                    // 1024 octets, 16 par ligne
  Serial.println();
  Serial.println(F("====== VIDAGE BRUT DE L'EEPROM ======"));
  Serial.println(F("  Adr   00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F"));
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


// ---------------------------------- Programme -------------------------------
void setup() {
  Serial.begin(115200);
  while (!Serial) { }                  // attend le port série (cartes USB natives : R4, Leonardo...)
  delay(500);
  Serial.println(F("Lecture de la base de badges (lecture seule)"));
  afficherBase();
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R')                  afficherBase();
    else if (c == 'd' || c == 'D')             vidageBrut();
    else if (c == 'h' || c == 'H' || c == '?') afficherAide();
  }
}
