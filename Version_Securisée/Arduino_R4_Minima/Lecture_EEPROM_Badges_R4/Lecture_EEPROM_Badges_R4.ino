/* ============================================================================
 *  Lecture_EEPROM_Badges_R4
 *  Affiche sur le moniteur série (115200 bauds) la base de badges du programme
 *  « Autorisation_Ouverture_Securisee_R4 » (Arduino UNO R4 Minima / WiFi).
 *
 *  LECTURE SEULE : ce programme n'écrit rien dans l'EEPROM.
 *
 *  Format R4 :
 *    bloc 0, 0x0000 : drapeau 0xB4 (1) + version (1) + nombre de badges (2)
 *                     + UID admin (4) + compteur admin (2) + inscription admin (2)
 *    blocs 1 à 7    : badges, 9 octets : UID (4) + compteur (2) + inscription (2) + état (1)
 *                     badge n° i (0, 1, 2…) -> bloc 1 + (i % 7), rang i / 7
 *
 *  Commandes : "r" = relire, "d" = vidage brut du bloc 0 (en-tête),
 *              "D" = vidage brut complet (8 Ko, long)
 * ========================================================================== */

#if !defined(ARDUINO_ARCH_RENESAS)
#error "Version prevue pour Arduino UNO R4. Pour l'Uno, utiliser Lecture_EEPROM_Badges_Securise."
#endif

#include <EEPROM.h>

const byte CODE_R4 = 0xB4;
const int  TAILLE_BLOC = 1024, NB_BLOCS = 7, TAILLE_ENTREE = 9;
const int  MAX_BADGES = NB_BLOCS * (TAILLE_BLOC / TAILLE_ENTREE);   // 791

struct __attribute__((packed)) Entete {
  uint8_t flag, version; uint16_t nb; uint8_t uidAdmin[4]; uint16_t cptAdmin, inscrAdmin;
};
struct __attribute__((packed)) Entree {
  uint8_t uid[4]; uint16_t cpt, inscr; uint8_t etat;
};

int adresseEntree(int i) {
  return (1 + i % NB_BLOCS) * TAILLE_BLOC + (i / NB_BLOCS) * TAILLE_ENTREE;
}

void hex2(byte v) { if (v < 0x10) Serial.print('0'); Serial.print(v, HEX); }
void hex4(uint16_t v) { hex2(v >> 8); hex2(v & 0xFF); }
void uid4(const uint8_t *u) { for (byte i = 0; i < 4; i++) { hex2(u[i]); if (i < 3) Serial.print(' '); } }
void nombre(unsigned long v, byte largeur) {
  unsigned long p = 1;
  for (byte i = 1; i < largeur; i++) p *= 10;
  while (p > 1 && v < p) { Serial.print(' '); p /= 10; }
  Serial.print(v);
}

void afficherBase() {
  Entete e;
  EEPROM.get(0, e);
  Serial.println();
  Serial.println(F("=================================================="));
  Serial.println(F("   BASE DE BADGES - UNO R4 (EEPROM simulee 8 Ko)"));
  Serial.println(F("=================================================="));
  if (e.flag != CODE_R4) {
    Serial.print(F("Drapeau (0x0000) : 0x"));
    hex2(e.flag);
    Serial.println(F("  -> aucun badge administrateur (base vide ou effacee)"));
    Serial.println(F("=================================================="));
    return;
  }
  Serial.print(F("Capacite maximale : "));
  Serial.print(MAX_BADGES);
  Serial.println(F(" badges"));
  Serial.print(F("Badge ADMIN       : "));
  uid4(e.uidAdmin);
  Serial.print(F("   compteur "));
  Serial.print(e.cptAdmin);
  Serial.print(F("   inscription 0x"));
  hex4(e.inscrAdmin);
  Serial.println();
  Serial.print(F("Nombre de badges  : "));
  Serial.println(e.nb);
  if (e.nb > MAX_BADGES) { Serial.println(F("!! Nombre INCOHERENT")); return; }
  if (e.nb == 0) { Serial.println(F("Aucun badge autorise dans la base.")); return; }

  Serial.println();
  Serial.println(F("  No  Bloc  Adresse  UID           Passages  Inscr.  Etat"));
  Serial.println(F(" ---  ----  -------  -----------   --------  ------  ----------------------"));
  int bloques = 0;
  for (int i = 0; i < e.nb; i++) {
    Entree b;
    int adr = adresseEntree(i);
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
    if (b.etat == 0)      Serial.print(F("actif"));
    else if (b.etat == 1) { Serial.print(F("BLOQUE (copie detectee)")); bloques++; }
    else                  { Serial.print(F("inconnu 0x")); hex2(b.etat); }
    if (memcmp(b.uid, e.uidAdmin, 4) == 0) Serial.print(F("  (identique au badge ADMIN !)"));
    Serial.println();
  }
  if (bloques) {
    Serial.println();
    Serial.print(bloques);
    Serial.println(F(" badge(s) BLOQUE(S) : pour reactiver, le retirer puis le reenregistrer."));
  }
  Serial.println(F("=================================================="));
  Serial.println(F("\"r\" relire  -  \"d\" vidage du bloc 0  -  \"D\" vidage complet"));
}

void vidage(unsigned int fin) {
  Serial.println();
  Serial.println(F("Adresse  00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F"));
  for (unsigned int ligne = 0; ligne < fin; ligne += 16) {
    Serial.print(F("0x"));
    hex4(ligne);
    Serial.print(F("  "));
    for (byte i = 0; i < 16; i++) { hex2(EEPROM.read(ligne + i)); Serial.print(' '); }
    Serial.println();
  }
}

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) { }   // attente du moniteur (USB natif du R4)
  afficherBase();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') afficherBase();
    if (c == 'd') vidage(TAILLE_BLOC);
    if (c == 'D') vidage(EEPROM.length());
  }
}
