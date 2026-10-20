/* ============================================================================
 *  Lecture_EEPROM_Badges_Securise
 *  Affiche sur le moniteur série (115200 bauds) le contenu de l'EEPROM du
 *  contrôle d'accès. Reconnaît automatiquement les deux formats :
 *    - format SÉCURISÉ (Autorisation_Ouverture_Securisee_HD44780) : 0x01 = 0xA5
 *    - ancien format UID seul (Autorisation_Ouverture_et_memorisation...) : 0x01 = 128
 *
 *  LECTURE SEULE : ce programme n'écrit rien dans l'EEPROM.
 *  Aucun câblage nécessaire : seule la carte Arduino suffit.
 *
 *  Format sécurisé :
 *    0x00        : nombre de badges (hors admin)
 *    0x01        : 0xA5 si un badge admin est enregistré
 *    0x02 - 0x05 : UID admin      0x06 - 0x07 : compteur admin
 *    0x08 - 0x09 : n° d'inscription admin
 *    0x10 ...    : badges, 9 octets : UID (4) + compteur (2) + inscription (2) + état (1)
 *
 *  Utilisation :
 *    1. Téléverser ce programme
 *    2. Ouvrir le moniteur série à 115200 bauds
 *    3. Envoyer "r" pour relire, "d" pour un vidage brut complet de l'EEPROM
 *    4. Retéléverser ensuite le programme de contrôle d'accès
 *       (l'EEPROM n'est pas effacée par le téléversement)
 * ========================================================================== */

#include <EEPROM.h>

const byte CODE_SECURISE = 0xA5;
const byte CODE_ANCIEN   = 128;

void afficherHex(byte v) {
  if (v < 0x10) Serial.print('0');
  Serial.print(v, HEX);
}

void afficherAdresse(int adr) {
  Serial.print(F("0x"));
  if (adr < 0x1000) Serial.print('0');
  if (adr < 0x100)  Serial.print('0');
  if (adr < 0x10)   Serial.print('0');
  Serial.print(adr, HEX);
}

void afficherUID(int adr) {
  for (byte i = 0; i < 4; i++) {
    afficherHex(EEPROM.read(adr + i));
    if (i < 3) Serial.print(' ');
  }
}

uint16_t lire16(int adr) {
  return EEPROM.read(adr) | ((uint16_t)EEPROM.read(adr + 1) << 8);
}

// Nombre affiché sur une largeur fixe, aligné à droite
void afficherNombre(unsigned long v, byte largeur) {
  unsigned long p = 1;
  for (byte i = 1; i < largeur; i++) p *= 10;
  while (p > 1 && v < p) { Serial.print(' '); p /= 10; }
  Serial.print(v);
}

bool memeUID(int a, int b) {
  for (byte i = 0; i < 4; i++) if (EEPROM.read(a + i) != EEPROM.read(b + i)) return false;
  return true;
}

// ----------------------------------------------------------------------------
void afficherSecurise() {
  const int PREMIER = 0x10, TAILLE = 9;
  int maxi = (EEPROM.length() - PREMIER) / TAILLE;
  if (maxi > 254) maxi = 254;
  byte nb = EEPROM.read(0);

  Serial.println(F("Format              : SECURISE (cle + compteur dans le badge)"));
  Serial.print(F("Capacite maximale   : "));
  Serial.print(maxi);
  Serial.println(F(" badges"));
  Serial.println();
  Serial.print(F("Badge ADMIN         : "));
  afficherUID(2);
  Serial.print(F("   compteur "));
  Serial.print(lire16(6));
  Serial.print(F("   inscription 0x"));
  afficherHex(EEPROM.read(9));
  afficherHex(EEPROM.read(8));
  Serial.println();
  Serial.println();
  Serial.print(F("Nombre de badges    : "));
  Serial.println(nb);

  if (nb > maxi) {
    Serial.println(F("!! Compteur INCOHERENT : liste non affichee (envoyer \"d\")"));
    return;
  }
  if (nb == 0) {
    Serial.println(F("Aucun badge autorise dans la base."));
    return;
  }

  Serial.println();
  Serial.println(F(" No   Adresse   UID           Passages  Inscr.  Etat"));
  Serial.println(F("----  -------   -----------   --------  ------  -----------------"));
  byte bloques = 0;
  for (int n = 1; n <= nb; n++) {
    int adr = PREMIER + (n - 1) * TAILLE;
    afficherNombre(n, 3);
    Serial.print(F("   "));
    afficherAdresse(adr);
    Serial.print(F("    "));
    afficherUID(adr);
    Serial.print(F("   "));
    afficherNombre(lire16(adr + 4), 8);
    Serial.print(F("  0x"));
    afficherHex(EEPROM.read(adr + 7));
    afficherHex(EEPROM.read(adr + 6));
    Serial.print(F("  "));
    byte e = EEPROM.read(adr + 8);
    if (e == 0)      Serial.print(F("actif"));
    else if (e == 1) { Serial.print(F("BLOQUE (copie detectee)")); bloques++; }
    else             { Serial.print(F("inconnu (0x")); afficherHex(e); Serial.print(')'); }
    if (memeUID(adr, 2)) Serial.print(F("  (identique au badge ADMIN !)"));
    Serial.println();
  }
  if (bloques) {
    Serial.println();
    Serial.print(bloques);
    Serial.println(F(" badge(s) BLOQUE(S) : pour reactiver, le retirer puis le"));
    Serial.println(F("reenregistrer en mode programmation."));
  }
}

// ----------------------------------------------------------------------------
void afficherAncien() {
  const int PREMIER = 6;
  int maxi = (EEPROM.length() - PREMIER) / 4;
  if (maxi > 254) maxi = 254;
  byte nb = EEPROM.read(0);

  Serial.println(F("Format              : ANCIEN (UID seul)"));
  Serial.println(F("  -> le programme securise demandera de reenregistrer les badges"));
  Serial.println();
  Serial.print(F("Badge ADMIN         : "));
  afficherUID(2);
  Serial.println();
  Serial.print(F("Nombre de badges    : "));
  Serial.println(nb);
  if (nb > maxi) {
    Serial.println(F("!! Compteur INCOHERENT : liste non affichee (envoyer \"d\")"));
    return;
  }
  for (int n = 1; n <= nb; n++) {
    int adr = PREMIER + (n - 1) * 4;
    afficherNombre(n, 3);
    Serial.print(F("   "));
    afficherAdresse(adr);
    Serial.print(F("    "));
    afficherUID(adr);
    Serial.println();
  }
}

// ----------------------------------------------------------------------------
void afficherBase() {
  byte flag = EEPROM.read(1);
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

  if (flag == CODE_SECURISE)    afficherSecurise();
  else if (flag == CODE_ANCIEN) afficherAncien();
  else {
    Serial.println(F("Aucun badge administrateur enregistre (base vide ou effacee)."));
    Serial.println(F("Le controle d'acces demandera un badge admin au demarrage."));
  }
  Serial.println(F("=================================================="));
  Serial.println(F("Envoyer \"r\" pour relire, \"d\" pour le vidage brut"));
}

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

void setup() {
  Serial.begin(115200);
  while (!Serial) { }
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
