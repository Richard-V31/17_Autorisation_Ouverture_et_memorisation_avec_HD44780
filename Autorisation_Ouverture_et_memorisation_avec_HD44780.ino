/* ============================================================================
 *  Contrôle d'accès RFID (MFRC522) + LCD 20x4 I2C (librairie hd44780)
 *  Version corrigée et non bloquante (millis)
 *
 *  - À la 1re mise en service, le 1er badge lu devient le BADGE ADMINISTRATEUR.
 *  - Passer le badge admin : entrée en mode PROGRAMMATION.
 *      Badge inconnu  -> il est ajouté à la base
 *      Badge connu    -> il est retiré de la base
 *  - Sortie du mode programmation : nouveau passage du badge admin,
 *    ou automatiquement après 30 s sans action (sécurité).
 *  - Effacement de la base : appuyer sur le BP RAZ (D3 à la masse), faire un
 *    RESET de la carte et garder le BP appuyé pendant le compte à rebours (5 s).
 *  - BP d'ouverture (D2 à la masse) : ouverture forcée de la porte
 *    (3 s minimum, tant que le BP est appuyé, 30 s maximum).
 *
 *  Organisation de l'EEPROM :
 *    0x00        : nombre de badges enregistrés (hors badge admin)
 *    0x01        : 128 (0x80) si un badge admin est enregistré
 *    0x02 - 0x05 : UID du badge admin
 *    0x06 ...    : UID des badges autorisés, 4 octets chacun
 *  Exemple :
 *    0x00 - 0x01 - 0x02 0x03 0x04 0x05 - 0x06 0x07 0x08 0x09
 *     01  -  80  -  86   32   3C   1F  -  B7   C9   47   1B
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
const unsigned long DUREE_OUVERTURE_BP_MIN = 3000UL;  // ms
const unsigned long DUREE_OUVERTURE_BP_MAX = 30000UL; // ms, sécurité si BP coincé
const unsigned long DUREE_MESSAGE          = 2000UL;  // durée des messages LCD
const unsigned long TIMEOUT_PROGRAMMATION  = 30000UL; // sortie auto du mode prog
const unsigned long ANTI_REBOND            = 50UL;    // ms
const unsigned long PAS_LEDS               = 200UL;   // ms, cycle / clignotement

// Badges à UID long (7 ou 10 octets : NTAG, Ultralight...) :
//   false = on garde les 4 PREMIERS octets (compatible avec la base existante)
//   true  = on garde les 4 DERNIERS octets (plus discriminant, car le 1er octet
//           est le code fabricant, identique pour beaucoup de badges).
//           Les badges longs déjà enregistrés (admin compris) devront alors
//           être réenregistrés (RAZ de la base conseillée).
const bool UID_LONG_4_DERNIERS = true;

// ------------------------------- EEPROM -------------------------------------
const int  ADR_NB_BADGES     = 0;
const int  ADR_FLAG_ADMIN    = 1;
const int  ADR_UID_ADMIN     = 2;
const int  ADR_PREMIER_BADGE = 6;
const byte CODE_ADMIN        = 128;
const byte TAILLE_UID        = 4;

// ------------------------------- Objets -------------------------------------
MFRC522 mfrc522(PIN_RC522_SS, PIN_RC522_RST);
hd44780_I2Cexp lcd;   // adresse I2C détectée automatiquement

// ------------------------------- Variables ----------------------------------
byte readCard[TAILLE_UID];    // UID du badge qui vient d'être lu
byte storedCard[TAILLE_UID];  // UID lu dans l'EEPROM
byte masterCard[TAILLE_UID];  // UID du badge administrateur

// États du système
enum Etat { ATTENTE, MESSAGE, OUVERT_BADGE, OUVERT_BP };
Etat etat = ATTENTE;
unsigned long debutEtat = 0;

bool modeProg = false;
unsigned long derniereActionProg = 0;

bool bpDoitEtreRelache = false;  // empêche une réouverture si le BP est resté coincé

// Clignotement non bloquant d'une LED
byte ledClignote = 0;
byte nbBasculements = 0;
bool etatLedClignote = false;
unsigned long dernierBasculement = 0;


// ============================================================================
//                                   LEDs
// ============================================================================
void ledsOff() {
  digitalWrite(PIN_LED_ROUGE, LOW);
  digitalWrite(PIN_LED_VERTE, LOW);
  digitalWrite(PIN_LED_BLEUE, LOW);
}

// Allume une seule LED (et arrête un éventuel clignotement)
void allumerSeule(byte pin) {
  ledClignote = 0;
  nbBasculements = 0;
  ledsOff();
  digitalWrite(pin, HIGH);
}

// Fait clignoter une LED nbFois (géré dans gererLeds, sans delay)
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
      // Mode programmation : cycle vert -> bleu -> rouge
      byte phase = (maintenant / PAS_LEDS) % 3;
      digitalWrite(PIN_LED_VERTE, phase == 0);
      digitalWrite(PIN_LED_BLEUE, phase == 1);
      digitalWrite(PIN_LED_ROUGE, phase == 2);
    } else {
      // Mode normal : LED bleue seule
      allumerSeule(PIN_LED_BLEUE);
    }
  }
  // OUVERT_BADGE / OUVERT_BP : LED verte, réglée à l'ouverture
}


// ============================================================================
//                                   LCD
// ============================================================================
// Efface l'écran et affiche jusqu'à 4 lignes (nullptr = ligne vide).
// Les textes sont en mémoire Flash (F("...")) pour économiser la RAM.
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

// Passe en état MESSAGE : le message reste affiché DUREE_MESSAGE,
// puis on revient automatiquement à l'écran d'attente.
void entrerMessage() {
  etat = MESSAGE;
  debutEtat = millis();
}


// ============================================================================
//                                  Buzzer
// ============================================================================
// Sons courts (moins de 250 ms) : seule petite partie encore bloquante
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
  tone(PIN_BUZZ, 370, 300);   // non bloquant
}


// ============================================================================
//                           Gestion des badges (EEPROM)
// ============================================================================
// Nombre maximum de badges (limité à 254 car le compteur tient sur 1 octet)
int nbBadgesMax() {
  int places = (EEPROM.length() - ADR_PREMIER_BADGE) / TAILLE_UID;
  return (places > 254) ? 254 : places;
}

// Adresse EEPROM du badge n° slot (slot de 1 à nombre de badges)
int adresseBadge(int slot) {
  return ADR_PREMIER_BADGE + (slot - 1) * TAILLE_UID;
}

void readID(int slot) {
  int adr = adresseBadge(slot);
  for (byte i = 0; i < TAILLE_UID; i++) {
    storedCard[i] = EEPROM.read(adr + i);
  }
}

// Compare deux UID (plus de variable globale "match")
bool checkTwo(const byte a[], const byte b[]) {
  for (byte k = 0; k < TAILLE_UID; k++) {
    if (a[k] != b[k]) return false;
  }
  return true;
}

// Renvoie le n° d'emplacement du badge (1..n), ou 0 s'il est absent
int findIDSLOT(const byte find[]) {
  int count = EEPROM.read(ADR_NB_BADGES);
  for (int i = 1; i <= count; i++) {
    readID(i);
    if (checkTwo(find, storedCard)) return i;
  }
  return 0;
}

bool findID(const byte find[]) {
  return findIDSLOT(find) != 0;
}

bool isMaster(const byte test[]) {
  return checkTwo(test, masterCard);
}

// Ajoute un badge. Renvoie false s'il existe déjà ou si la base est pleine.
bool writeID(const byte a[]) {
  int num = EEPROM.read(ADR_NB_BADGES);
  if (findID(a) || num >= nbBadgesMax()) return false;

  int adr = adresseBadge(num + 1);
  for (byte j = 0; j < TAILLE_UID; j++) {
    EEPROM.update(adr + j, a[j]);          // update : écrit seulement si différent
  }
  EEPROM.update(ADR_NB_BADGES, num + 1);   // compteur en dernier (coupure secteur)
  return true;
}

// Retire un badge en décalant les suivants. Renvoie false s'il est absent.
bool deleteID(const byte a[]) {
  int slot = findIDSLOT(a);
  if (slot == 0) return false;

  int num = EEPROM.read(ADR_NB_BADGES);
  for (int s = slot; s < num; s++) {
    int dest = adresseBadge(s);
    for (byte j = 0; j < TAILLE_UID; j++) {
      EEPROM.update(dest + j, EEPROM.read(dest + TAILLE_UID + j));
    }
  }
  int dernier = adresseBadge(num);          // emplacement libéré
  for (byte j = 0; j < TAILLE_UID; j++) {
    EEPROM.update(dernier + j, 0);
  }
  EEPROM.update(ADR_NB_BADGES, num - 1);    // compteur en dernier
  return true;
}


// ============================================================================
//                              Lecteur RFID
// ============================================================================
void afficherUID(const byte uid[], byte taille) {
  for (byte i = 0; i < taille; i++) {
    if (uid[i] < 0x10) Serial.print('0');
    Serial.print(uid[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
}

// Renvoie true si un nouveau badge a été lu (UID dans readCard)
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
  mfrc522.PICC_HaltA();   // le badge ne sera relu qu'après avoir été retiré
  return true;
}

void ShowReaderDetails() {
  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  Serial.print(F("MFRC522 Version: 0x"));
  Serial.print(v, HEX);
  if      (v == 0x91) Serial.print(F(" = v1.0"));
  else if (v == 0x92) Serial.print(F(" = v2.0"));
  else if (v == 0x11) Serial.print(F(" = BlueCore Tech. RFID Acces v2.0"));
  else                Serial.print(F(" (Inconnu)"));
  Serial.println();

  // 0x00 ou 0xFF : lecteur absent ou défaillant
  if (v == 0x00 || v == 0xFF) {
    Serial.println(F("ATTENTION: Erreur de Communication"));
    Serial.println(F("Verifier que le RFID-MFRC522 soit correctement connecte"));
    afficher(F("ATTENTION"), F("Erreur Communication"), F("Lecteur RFID"), nullptr);
    allumerSeule(PIN_LED_ROUGE);
    while (true);   // arrêt : à corriger côté câblage
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
  digitalWrite(PIN_SERRURE, HIGH);   // déverrouillage
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
  Serial.println(F("Acces Refuse"));
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
  derniereActionProg = millis();
  Serial.println(F("Entree dans le mode Programmation"));
  Serial.print(F("Il y a "));
  Serial.print(EEPROM.read(ADR_NB_BADGES));
  Serial.println(F(" badge(s) dans la base"));
  Serial.println(F("Scanner un Badge pour l'inscrire ou le retirer"));
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

// Traitement d'un badge qui vient d'être lu
void traiterBadge() {
  if (modeProg) {
    derniereActionProg = millis();

    if (isMaster(readCard)) {
      sortirProgrammation(false);
    }
    else if (deleteID(readCard)) {           // badge connu -> retiré
      Serial.println(F("Badge supprime de la base"));
      afficher(F("Badge SUPPRIME"), F("de la BASE..."), nullptr, nullptr);
      tone(PIN_BUZZ, 1800, 80);
      lancerClignotement(PIN_LED_BLEUE, 3);
      entrerMessage();
    }
    else if (writeID(readCard)) {            // badge inconnu -> ajouté
      Serial.println(F("Badge enregistre dans la base"));
      afficher(F("Badge ENREGISTRE"), F("dans la BASE..."), nullptr, nullptr);
      tone(PIN_BUZZ, 2200, 80);
      lancerClignotement(PIN_LED_VERTE, 3);
      entrerMessage();
    }
    else {                                   // inconnu mais base pleine
      Serial.println(F("ERREUR : base pleine"));
      afficher(F("....ERREUR!....."), F("  Base pleine"), F("Badge non enregistre"), nullptr);
      lancerClignotement(PIN_LED_ROUGE, 3);
      entrerMessage();
    }
    return;
  }

  // Mode normal
  if (isMaster(readCard))     entrerProgrammation();
  else if (findID(readCard))  ouvrirParBadge();
  else                        refuser();
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

// Corrige un compteur incohérent (EEPROM neuve = 0xFF, ou ancien bug)
void verifierCompteur() {
  int nb = EEPROM.read(ADR_NB_BADGES);
  if (nb > nbBadgesMax()) {
    Serial.println(F("Compteur de badges invalide -> remis a 0"));
    EEPROM.update(ADR_NB_BADGES, 0);
  }
}

void enregistrerAdmin() {
  Serial.println(F("Pas de Badge Administrateur"));
  Serial.println(F("Placer une carte pour la definir comme badge administrateur"));
  afficher(F("    Pas de Badge"),
           F("   Administrateur"),
           F("     Enregistre"),
           F("  Placer une Carte"));

  while (!getID()) {                 // LED bleue clignotante en attente
    digitalWrite(PIN_LED_BLEUE, HIGH);
    delay(200);
    digitalWrite(PIN_LED_BLEUE, LOW);
    delay(200);
  }

  EEPROM.update(ADR_NB_BADGES, 0);   // base vide (corrige le bug de l'EEPROM neuve)
  for (byte j = 0; j < TAILLE_UID; j++) {
    EEPROM.update(ADR_UID_ADMIN + j, readCard[j]);
  }
  EEPROM.update(ADR_FLAG_ADMIN, CODE_ADMIN);   // validé en dernier
  Serial.println(F("Badge Administrateur Enregistre"));
  afficher(F("Badge Administrateur"), F("    Enregistre"), nullptr, nullptr);
  delay(1500);
}


// ============================================================================
//                                  SETUP
// ============================================================================
void setup() {
  // Sorties : serrure fermée et LEDs éteintes dès le départ
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

  Serial.println(F("Lecteur de Badges"));
  ShowReaderDetails();

  if (digitalRead(PIN_BP_RAZ) == LOW) {
    demandeEffacement();
  }

  verifierCompteur();

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
  Serial.println(EEPROM.read(ADR_NB_BADGES));
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

      // Fin d'un message temporaire -> retour à l'écran d'attente
      if (etat == MESSAGE && maintenant - debutEtat >= DUREE_MESSAGE) {
        etat = ATTENTE;
        afficherEcranAttente();
      }

      // Ouverture forcée par BP (possible à tout moment hors ouverture)
      if (bp && !bpDoitEtreRelache) {
        ouvrirParBP();
        break;
      }

      if (etat == ATTENTE) {
        // Sortie automatique du mode programmation
        if (modeProg && maintenant - derniereActionProg >= TIMEOUT_PROGRAMMATION) {
          sortirProgrammation(true);
          break;
        }
        if (getID()) {
          traiterBadge();
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
          bpDoitEtreRelache = true;   // BP coincé : pas de réouverture avant relâchement
          Serial.println(F("BP maintenu trop longtemps : fermeture de securite"));
        }
        fermerPorte();
      }
      break;
    }
  }

  gererLeds();
}
