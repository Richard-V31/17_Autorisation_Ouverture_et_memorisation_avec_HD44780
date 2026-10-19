/* ============================================================================
 *  Lecture_EEPROM_Badges
 *  Affiche sur le moniteur série (9600 bauds) le contenu de l'EEPROM utilisé
 *  par le programme "Autorisation_Ouverture_et_memorisation_avec_HD44780".
 *
 *  LECTURE SEULE : ce programme n'écrit rien dans l'EEPROM.
 *  Aucun câblage nécessaire : seule la carte Arduino suffit.
 *
 *  Organisation de l'EEPROM lue :
 *    0x00        : nombre de badges enregistrés (hors badge admin)
 *    0x01        : 128 (0x80) si un badge admin est enregistré
 *    0x02 - 0x05 : UID du badge admin
 *    0x06 ...    : UID des badges autorisés, 4 octets chacun
 *
 *  Utilisation :
 *    1. Téléverser ce programme
 *    2. Ouvrir le moniteur série à 9600 bauds
 *    3. Envoyer "r" pour relire, "d" pour un vidage brut complet de l'EEPROM
 *    4. Retéléverser ensuite le programme de contrôle d'accès
 *       (la base de badges est conservée, l'EEPROM n'est pas effacée
 *        par le téléversement)
 * ========================================================================== */

#include <EEPROM.h>

const int  ADR_NB_BADGES     = 0;
const int  ADR_FLAG_ADMIN    = 1;
const int  ADR_UID_ADMIN     = 2;
const int  ADR_PREMIER_BADGE = 6;
const byte CODE_ADMIN        = 128;
const byte TAILLE_UID        = 4;

// ------------------------------------------------------------------
// Affiche un octet en hexadécimal sur 2 chiffres (ex : 0A au lieu de A)
void afficherHex(byte valeur) {
  if (valeur < 0x10) Serial.print('0');
  Serial.print(valeur, HEX);
}

// Affiche une adresse en hexadécimal sur 4 chiffres (ex : 0x0006)
void afficherAdresse(int adr) {
  Serial.print(F("0x"));
  if (adr < 0x1000) Serial.print('0');
  if (adr < 0x100)  Serial.print('0');
  if (adr < 0x10)   Serial.print('0');
  Serial.print(adr, HEX);
}

// Affiche les 4 octets d'UID stockés à partir de l'adresse adr
void afficherUID(int adr) {
  for (byte i = 0; i < TAILLE_UID; i++) {
    afficherHex(EEPROM.read(adr + i));
    if (i < TAILLE_UID - 1) Serial.print(' ');
  }
}

int nbBadgesMax() {
  int places = (EEPROM.length() - ADR_PREMIER_BADGE) / TAILLE_UID;
  return (places > 254) ? 254 : places;
}

// ------------------------------------------------------------------
void afficherBase() {
  byte nb   = EEPROM.read(ADR_NB_BADGES);
  byte flag = EEPROM.read(ADR_FLAG_ADMIN);

  Serial.println();
  Serial.println(F("=========================================="));
  Serial.println(F("   CONTENU DE LA BASE DE BADGES (EEPROM)"));
  Serial.println(F("=========================================="));
  Serial.print(F("Taille EEPROM          : "));
  Serial.print(EEPROM.length());
  Serial.println(F(" octets"));
  Serial.print(F("Capacite maximale      : "));
  Serial.print(nbBadgesMax());
  Serial.println(F(" badges"));
  Serial.println();

  // ---- Badge administrateur ----
  Serial.print(F("Indicateur admin (0x01): 0x"));
  afficherHex(flag);
  if (flag == CODE_ADMIN) {
    Serial.println(F("  -> badge admin enregistre"));
    Serial.print(F("UID badge ADMIN        : "));
    afficherUID(ADR_UID_ADMIN);
    Serial.println();
  } else {
    Serial.println(F("  -> AUCUN badge admin enregistre"));
  }
  Serial.println();

  // ---- Nombre de badges ----
  Serial.print(F("Nombre de badges (0x00): "));
  Serial.println(nb);

  if (nb > nbBadgesMax()) {
    Serial.println(F("!! Compteur INCOHERENT (EEPROM neuve = 255, ou base corrompue)"));
    Serial.println(F("!! La liste ci-dessous n'est pas affichee."));
    Serial.println(F("!! Envoyer \"d\" pour voir le contenu brut."));
    return;
  }

  // ---- Liste des badges autorisés ----
  if (nb == 0) {
    Serial.println(F("Aucun badge autorise dans la base."));
  } else {
    Serial.println();
    Serial.println(F(" No   Adresse   UID"));
    Serial.println(F("----  -------   -----------"));
    for (int n = 1; n <= nb; n++) {
      int adr = ADR_PREMIER_BADGE + (n - 1) * TAILLE_UID;
      if (n < 100) Serial.print(' ');
      if (n < 10)  Serial.print(' ');
      Serial.print(n);
      Serial.print(F("   "));
      afficherAdresse(adr);
      Serial.print(F("    "));
      afficherUID(adr);

      // Signale un doublon éventuel avec le badge admin
      bool estAdmin = true;
      for (byte i = 0; i < TAILLE_UID; i++) {
        if (EEPROM.read(adr + i) != EEPROM.read(ADR_UID_ADMIN + i)) estAdmin = false;
      }
      if (estAdmin && flag == CODE_ADMIN) Serial.print(F("   (identique au badge ADMIN !)"));
      Serial.println();
    }
  }
  Serial.println(F("=========================================="));
  Serial.println(F("Envoyer \"r\" pour relire, \"d\" pour le vidage brut"));
}

// ------------------------------------------------------------------
// Vidage brut : 16 octets par ligne
void vidageBrut() {
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

// ------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  while (!Serial) { }   // utile sur Leonardo / Micro, sans effet sur Uno
  delay(500);
  afficherBase();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') afficherBase();
    if (c == 'd' || c == 'D') vidageBrut();
  }
}
