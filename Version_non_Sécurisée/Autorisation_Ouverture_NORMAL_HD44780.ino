/* ============================================================================
 *  Contrôle d'accès RFID (MFRC522) + LCD 20x4 I2C (librairie hd44780)
 *  Version NORMALE : identification par l'UID du badge uniquement (sans sécurité
 *  anti-clonage). Mêmes fonctions que la version sécurisée :
 *  automate non bloquant, LEDs animées, anti-rebond du BP, sortie automatique du
 *  mode programmation, affichage du contenu de la base sur le moniteur série.
 *  Tout type de badge lisible par le MFRC522 est accepté (MIFARE Classic,
 *  Ultralight, NTAG...).
 *
 *  - À la 1re mise en service, le 1er badge lu devient le BADGE ADMINISTRATEUR.
 *  - Passer le badge admin : entrée en mode PROGRAMMATION.
 *      Badge inconnu  -> il est ajouté à la base
 *      Badge connu    -> il est retiré de la base
 *  - Sortie du mode programmation : nouveau passage du badge admin,
 *    ou automatiquement après 20 s sans action.
 *  - Effacement de la base : appuyer sur le BP RAZ (D3 à la masse), faire un
 *    RESET de la carte et garder le BP appuyé pendant le compte à rebours (5 s).
 *  - BP d'ouverture (D2 à la masse) : ouverture forcée de la porte
 *    (5 s minimum, tant que le BP est appuyé, 9 s maximum).
 *
 *  MONITEUR SÉRIE (115200 bauds) :
 *  - À l'ouverture du moniteur, la Uno redémarre : le contenu de la base
 *    (badge admin, liste des badges) s'affiche à la fin du démarrage.
 *    (la serrure reste fermée pendant ce redémarrage)
 *  - Commandes à envoyer à tout moment (traitées quand le lecteur est en attente) :
 *      r : réafficher le contenu de la base
 *      d : vidage brut complet de l'EEPROM (1024 octets en hexadécimal)
 *      h : rappel des commandes
 *    Ces commandes ne font que LIRE l'EEPROM.
 *
 *  Organisation de l'EEPROM (même format que l'ancienne version normale :
 *  une base existante est conservée) :
 *    0x00        : nombre de badges enregistrés (hors badge admin)
 *    0x01        : 128 (0x80) si un badge admin est enregistré
 *    0x02 - 0x05 : UID du badge admin
 *    0x06 ...    : badges, 4 octets (UID) chacun
 *  Capacité : 254 badges.
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
const byte PIN_LED_ROUGE    = 7;   // LED rouge : refus / porte fermée
const byte PIN_LED_VERTE    = 6;   // LED verte : accès autorisé / porte ouverte
const byte PIN_LED_BLEUE    = 5;   // LED bleue : attente de badge
const byte PIN_SERRURE      = 4;   // commande de la serrure (HIGH = ouverte) via relais/transistor
const byte PIN_BP_RAZ       = 3;   // BP effacement base -> GND
const byte PIN_BP_OUVERTURE = 2;   // BP ouverture forcée -> GND
const byte PIN_RC522_SS     = 10;  // broche SS (sélection du composant SPI) du MFRC522
const byte PIN_RC522_RST    = 9;   // broche RST (reset) du MFRC522

// ------------------------------- Réglages -----------------------------------
const unsigned long DUREE_OUVERTURE_BADGE  = 10000UL; // ms (10 s) : temps d'ouverture après un badge valide
const unsigned long DUREE_OUVERTURE_BP_MIN = 5000UL;  // ms : 5 s ouverture minimale par le BP, même si appui bref
const unsigned long DUREE_OUVERTURE_BP_MAX = 9000UL;  // ms : 9 s sécurité si BP coincé
const unsigned long DUREE_MESSAGE          = 2000UL;  // durée 2 s des messages LCD
const unsigned long TIMEOUT_PROGRAMMATION  = 20000UL; // 20 s : sortie auto du mode prog
const unsigned long ANTI_REBOND            = 50UL;    // ms : le BP doit être stable 50 ms pour être pris en compte
const unsigned long PAS_LEDS               = 200UL;   // ms, cycle / clignotement

// Badges à UID long (7 ou 10 octets) : garder les 4 DERNIERS octets (true) ou les 4 premiers (false).
// (sans effet sur les badges à UID de 4 octets)
// false = comportement de l'ancienne version (les 4 premiers octets).
const bool UID_LONG_4_DERNIERS = false;  // la base ne stocke que 4 octets d'UID par badge

// ------------------------------- EEPROM -------------------------------------
//   Adresse  | Contenu
//   ---------+----------------------------------------------------------------
//   0x00     | nombre de badges utilisateurs enregistrés (0 à 254)
//   0x01     | drapeau : 128 (0x80) = un badge admin est enregistré
//   0x02-05  | UID du badge admin (4 octets)
//   0x06 ... | UID des badges, 4 octets chacun, l'un après l'autre
// Badge n°1 en 0x06, Badge n°2 en 0x0A, Badge n°3 en 0x0E, ...
  // Si un Badge Administrateur est déja enregistré on trouve
  // A l'adresse 0x00h le nombre de badge enregistré
  // A l'adresse 0x01h de l'EEPROM sera inscrit 128 (0x80)ou un autre code au choix
  // Cette valeur indique que l'on a identifié un Badge Administrateur ici Code 86 32 3C 1F
  // A l'adresse 0x06h l'UID du 1er badge enregistré
  // Un badge autorisé a été enregistré ici Code B7 C9 47 1B
  // 0x00 - 0x01 - 0x02 0x03 0x04 0x05 - 0x06 0x07 0x08 0x09 - 0x0A 0x0B 0x0C 0x0D Adresse EPROM
  //  01  -  80  -  86   32   3C   1F  -  B7   C9   47   1B  -      Badge N°2      Valeur 
  //           
  // 01 Nbr de badges enregistrés dans la Base sans le Badge Administrateur - Ici 1 badge
  // 80 Un Badge Administrateur est enregistré (0x80 = 128)
  // 86 32 3C 1F Code du Badge Administrateur
  // B7 C9 47 1B Code du Badge Validé
  // Capacité : (1024 - 6) / 4 = 254 fiches.


const int  ADR_NB_BADGES     = 0x00;   // adresse 0 : nombre de badges enregistrés (hors admin)
const int  ADR_FLAG_ADMIN    = 0x01;   // adresse 1 : drapeau « admin enregistré »
const int  ADR_UID_ADMIN     = 0x02;   // adresses 2 à 5 : les 4 octets d'UID du badge admin
const int  ADR_PREMIER_BADGE = 0x06;   // adresse 6 : début de la 1re fiche badge
const byte CODE_ADMIN        = 128;    // valeur du drapeau quand un badge admin est enregistré
const byte CODE_SECURISE     = 0xA5;   // drapeau d'une base créée par la version SÉCURISÉE :
                                       // format incompatible -> la base est remise à zéro
const byte TAILLE_UID        = 4;      // nombre d'octets d'UID mémorisés par badge

// ------------------------------- Objets -------------------------------------
MFRC522 mfrc522(PIN_RC522_SS, PIN_RC522_RST);   // objet lecteur RFID (broches SS et RST)
hd44780_I2Cexp lcd;   // adresse I2C détectée automatiquement (pour la forcer : lcd(0x27))

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


// ============================================================================
//                           Base de badges (EEPROM)
// ============================================================================

  // Contenu EEPROM
  // Si un Badge Administrateur est déja enregistré on trouve
  // A l'adresse 0x00h le nombre de badge enregistré
  // A l'adresse 0x01h de l'EEPROM sera inscrit 128 (0x80)ou un autre code au choix
  // Cette valeur indique que l'on a identifié un Badge Administrateur ici Code 86 32 3C 1F
  // A l'adresse 0x06h l'UID du 1er badge enregistré
  // Un badge autorisé a été enregistré ici Code B7 C9 47 1B
  // 0x00 - 0x01 - 0x02 0x03 0x04 0x05 - 0x06 0x07 0x08 0x09 Adresse EPROM
  //  01  -  80  -  86   32   3C   1F  -  B7   C9   47   1B  Valeur 
  //           
  // 01 Nbr de badges enregistrés dans la Base sans le Badge Administrateur - Ici 1 badge
  // 80 Un Badge Administrateur est enregistré (0x80 = 128)
  // 86 32 3C 1F Code du Badge Administrateur
  // B7 C9 47 1B Code du Badge Validé
int nbBadgesMax() {                      // nombre de fiches qui tiennent dans l'EEPROM
  int places = (EEPROM.length() - ADR_PREMIER_BADGE) / TAILLE_UID; // (1024 - 6) / 4 = 254
  return (places > 254) ? 254 : places;  // limité à 254 car le nombre est stocké sur 1 octet
}

int adresseBadge(int slot) {             // slot de 1 à nombre de badges
  return ADR_PREMIER_BADGE + (slot - 1) * TAILLE_UID;   // ex. slot 3 -> 0x06 + 2*4 = 0x0E
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

// Ajoute un badge. false si la base est pleine.
bool ajouterBadge(const byte uid[]) {
  int num = EEPROM.read(ADR_NB_BADGES);          // nombre actuel de badges
  if (num >= nbBadgesMax()) return false;        // plus de place
  int adr = adresseBadge(num + 1);               // adresse de la première fiche libre
  for (byte j = 0; j < TAILLE_UID; j++) EEPROM.update(adr + j, uid[j]); // UID
  EEPROM.update(ADR_NB_BADGES, num + 1);         // compteur en dernier (coupure secteur)
  return true;
}

// Retire l'entrée n° slot en décalant les suivantes
void supprimerEntree(int slot) {
  int num = EEPROM.read(ADR_NB_BADGES);          // nombre actuel de badges
  for (int s = slot; s < num; s++) {             // chaque fiche suivante remonte d'un cran
    int dest = adresseBadge(s);
    for (byte j = 0; j < TAILLE_UID; j++) {      // copie les 4 octets de la fiche s+1 vers s
      EEPROM.update(dest + j, EEPROM.read(dest + TAILLE_UID + j));
    }
  }
  int dernier = adresseBadge(num);               // la dernière fiche est maintenant en double
  for (byte j = 0; j < TAILLE_UID; j++) EEPROM.update(dernier + j, 0); // on l'efface
  EEPROM.update(ADR_NB_BADGES, num - 1);         // un badge de moins
}


// ============================================================================
//               Moniteur série : affichage du contenu de la base
//               (LECTURE SEULE)
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
  int maxi = nbBadgesMax();                      // 254 sur une Uno
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
    Serial.println(F(" No   Adresse   UID"));
    Serial.println(F("----  -------   -----------"));
    for (int n = 1; n <= nb; n++) {              // une ligne par fiche
      int adr = adresseBadge(n);
      afficherNombre(n, 3);
      Serial.print(F("   "));
      afficherAdresse(adr);
      Serial.print(F("    "));
      afficherUIDEeprom(adr);
      if (memeUIDEeprom(adr, ADR_UID_ADMIN)) Serial.print(F("  (identique au badge ADMIN !)"));
      Serial.println();
    }
  }
  Serial.println(F("=================================================="));
  afficherAideSerie();
}

void vidageBrutSerie() {                         // 1024 octets, 16 par ligne
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
//                             Lecture du badge
// ============================================================================
void afficherUID(const byte uid[], byte taille) { // affiche un UID en hexadécimal sur le moniteur série
  for (byte i = 0; i < taille; i++) {
    if (uid[i] < 0x10) Serial.print('0');        // zéro de tête (ex. 0A au lieu de A)
    Serial.print(uid[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
}

// Lit un nouveau badge (UID dans readCard).
// Appeler terminerCarte() une fois le traitement fini.
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

void entrerProgrammation() {
  modeProg = true;
  derniereActionProg = millis();                   // départ du délai de 20 s
  Serial.println(F("Entree dans le mode Programmation"));
  Serial.print(F("Il y a "));
  Serial.print(EEPROM.read(ADR_NB_BADGES));
  Serial.println(F(" badge(s) dans la base"));
  afficher(F("Entre dans le Mode"), F("  PROGRAMMATION..."), nullptr, nullptr);
  entrerMessage();
}

void sortirProgrammation(bool parTimeout) {        // parTimeout = sortie automatique après 20 s
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
  if (ajouterBadge(readCard)) {
    Serial.println(F("Badge enregistre dans la base"));
    afficher(F("Badge ENREGISTRE"), F("dans la BASE..."), nullptr, nullptr);
    tone(PIN_BUZZ, 2200, 80);
    lancerClignotement(PIN_LED_VERTE, 3);
  } else {                                         // base pleine (254)
    Serial.println(F("ERREUR : base pleine"));
    afficher(F("....ERREUR!....."), F("  Base pleine"), F("Badge non enregistre"), nullptr);
    lancerClignotement(PIN_LED_ROUGE, 3);
    bipRefus();
  }
  entrerMessage();
}

// Mode programmation : retrait d'un badge connu
void retirerBadge(int slot) {
  supprimerEntree(slot);                           // retire sa fiche de l'EEPROM
  Serial.println(F("Badge supprime de la base"));
  afficher(F("Badge SUPPRIME"), F("de la BASE..."), nullptr, nullptr);
  tone(PIN_BUZZ, 1800, 80);
  lancerClignotement(PIN_LED_BLEUE, 3);
  entrerMessage();
}

// Traitement d'un badge qui vient d'être lu
void traiterBadge() {
  // ---- Badge administrateur : bascule du mode programmation ----
  if (isMaster(readCard)) {
    if (modeProg) sortirProgrammation(false);
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
  if (slot) ouvrirParBadge();                      // badge connu : ouverture
  else      refuser();                             // badge inconnu : refus
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
  if (flag == CODE_SECURISE) {                     // base de la version sécurisée (format différent)
    Serial.println(F("Base de la version securisee detectee : badges a reenregistrer"));
    afficher(F("Base version"), F("securisee detectee:"), F("badges a"), F("reenregistrer"));
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
  Serial.println(F("Pas de Badge Administrateur"));
  Serial.println(F("Placer un badge pour le definir comme badge administrateur"));
  afficher(F("    Pas de Badge"),
           F("   Administrateur"),
           F("     Enregistre"),
           F("  Placer une Carte"));

  while (!getID()) {                               // attend un badge, LED bleue clignotante
    digitalWrite(PIN_LED_BLEUE, HIGH);
    delay(200);
    digitalWrite(PIN_LED_BLEUE, LOW);
    delay(200);
  }
  terminerCarte();

  EEPROM.update(ADR_NB_BADGES, 0);                 // aucun badge utilisateur
  for (byte j = 0; j < TAILLE_UID; j++) {
    EEPROM.update(ADR_UID_ADMIN + j, readCard[j]); // UID admin en 0x02-0x05
  }
  EEPROM.update(ADR_FLAG_ADMIN, CODE_ADMIN);       // validé en dernier
  Serial.println(F("Badge Administrateur Enregistre"));
  afficher(F("Badge Administrateur"), F("    Enregistre"), nullptr, nullptr);
  tone(PIN_BUZZ, 2200, 80);
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

  int status = lcd.begin(20, 4);                   // LCD 20 colonnes x 4 lignes
  if (status) {                                    // status <> 0 : LCD non trouvé ou erreur
    Serial.println(F("ERREUR : ecran LCD non trouve"));
    hd44780::fatalError(status);                   // fait clignoter la LED D13 (code erreur) et bloque
  }
  lcd.backlight();                                 // rétroéclairage allumé

  SPI.begin();                                     // démarre le bus SPI
  mfrc522.PCD_Init();                              // initialise le lecteur RFID
  // Pour augmenter la distance de lecture :
  // mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);

  Serial.println(F("Lecteur de Badges - version normale (UID)"));
  ShowReaderDetails();                             // vérifie le lecteur (bloque si absent)

  if (digitalRead(PIN_BP_RAZ) == LOW) {            // BP RAZ appuyé pendant le reset ?
    demandeEffacement();
  }

  verifierBase();                                  // contrôle de l'EEPROM

  if (EEPROM.read(ADR_FLAG_ADMIN) != CODE_ADMIN) { // pas d'admin enregistré
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
          sortirProgrammation(true);               // 20 s sans action
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
      bool finSecurite = (duree >= DUREE_OUVERTURE_BP_MAX);       // 9 s atteintes
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
