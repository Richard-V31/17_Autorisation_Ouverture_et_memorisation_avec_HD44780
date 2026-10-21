/* ============================================================================
 *  Lecture_EEPROM_Badges_R4
 *  Affiche sur le moniteur série (115200 bauds) la base de badges du programme
 *  « Autorisation_Ouverture_Securisee_R4 » (Arduino UNO R4 Minima / WiFi).
 *
 *  LECTURE SEULE : ce programme n'écrit rien dans l'EEPROM.
 *
 *  Format R4, version 2 (fiches les unes derrière les autres) :
 *    0x0000 (bloc 0) : drapeau 0xB4 (1) + version (1) + nombre de badges (2)
 *                      + UID admin (4) + compteur admin (2) + inscription admin (2)
 *    0x0400 ...      : fiches de 9 octets : UID (4) + compteur (2) + inscription (2) + état (1)
 *                      fiche n°1 en 0x0400, n°2 en 0x0409, n°3 en 0x0412, ...
 *  Une base encore en version 1 (badges répartis sur 7 blocs, pas encore
 *  convertie par le programme principal) est aussi lue correctement.
 *
 *  Commandes : "r" = relire, "d" = vidage brut de l'en-tête (bloc 0),
 *              "f" = vidage brut des fiches utilisées,
 *              "D" = vidage brut complet (8 Ko, long)
 * ========================================================================== */

#if !defined(ARDUINO_ARCH_RENESAS)
#error "Version prevue pour Arduino UNO R4. Pour l'Uno, utiliser Lecture_EEPROM_Badges_Securise."
#endif

#include <EEPROM.h>

const byte CODE_R4           = 0xB4;
const int  TAILLE_BLOC       = 1024;
const int  TAILLE_EEPROM     = 8192;
const int  ADR_PREMIER_BADGE = TAILLE_BLOC;   // 0x0400
const int  TAILLE_ENTREE     = 9;
const int  MAX_BADGES        = (TAILLE_EEPROM - ADR_PREMIER_BADGE) / TAILLE_ENTREE;   // 796
const byte NB_BLOCS_V1       = 7;             // ancienne version 1
const int  MAX_BADGES_V1     = NB_BLOCS_V1 * (TAILLE_BLOC / TAILLE_ENTREE);           // 791

struct __attribute__((packed)) Entete {
  uint8_t flag, version; uint16_t nb; uint8_t uidAdmin[4]; uint16_t cptAdmin, inscrAdmin;
};
struct __attribute__((packed)) Entree {
  uint8_t uid[4]; uint16_t cpt, inscr; uint8_t etat;
};
static_assert(sizeof(Entree) == TAILLE_ENTREE, "taille d'entree");

byte versionLue = 2;   // version de la base lue dans l'en-tête

// Fiche n° i (0..nb-1) : à la suite (version 2) ou répartie sur 7 blocs (version 1)
int adresseEntree(int i) {
  if (versionLue == 1)
    return (1 + i % NB_BLOCS_V1) * TAILLE_BLOC + (i / NB_BLOCS_V1) * TAILLE_ENTREE;
  return ADR_PREMIER_BADGE + i * TAILLE_ENTREE;
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

  versionLue = e.version;
  Serial.print(F("Version de la base : "));
  Serial.print(e.version);
  if (e.version == 2)      Serial.println(F("  (fiches les unes derriere les autres)"));
  else if (e.version == 1) Serial.println(F("  (ancienne : 7 blocs, sera convertie au prochain demarrage du programme principal)"));
  else { Serial.println(F("  -> version INCONNUE, lecture impossible")); return; }

  int maxBadges = (versionLue == 1) ? MAX_BADGES_V1 : MAX_BADGES;
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
  if (e.nb > maxBadges) { Serial.println(F("!! Nombre INCOHERENT")); return; }
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
  Serial.println(F("\"r\" relire - \"d\" en-tete - \"f\" fiches - \"D\" vidage complet"));
}

// Vidage hexadécimal des adresses debut à fin (exclue), par lignes de 16 octets
void vidage(unsigned int debut, unsigned int fin) {
  Serial.println();
  Serial.println(F("Adresse  00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F"));
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
  if (e.flag != CODE_R4 || e.version != 2 || e.nb == 0 || e.nb > MAX_BADGES) {
    Serial.println(F("Pas de fiches a afficher (base vide, inconnue ou en version 1)"));
    return;
  }
  vidage(ADR_PREMIER_BADGE, ADR_PREMIER_BADGE + e.nb * TAILLE_ENTREE);
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
    if (c == 'd') vidage(0, TAILLE_BLOC);
    if (c == 'f' || c == 'F') vidageFiches();
    if (c == 'D') vidage(0, EEPROM.length());
  }
}
