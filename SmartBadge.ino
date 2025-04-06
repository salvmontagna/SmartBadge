// Salvatore Montagna - 1000080223

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>

// --- LCD ---
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- RFID ---
#define RFID_SS_PIN 10
#define RFID_RST_PIN 9
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);

// --- LED RGB ---
#define LED_R_PIN 3
#define LED_G_PIN 5
#define LED_B_PIN 6

// --- PIR (analogico A0) ---
#define PIR_PIN A0
bool accessoConsentito = false;

// --- LED Allarme (analogico A1) ---
#define LED_ALLARME_PIN A1
bool allarmeAttivo = false;

// --- Servo (A3) ---
Servo servoPorta;
#define SERVO_PIN A3

// --- Buzzer (analogico A2) ---
#define BUZZER_PIN A2

// --- Encoder ---
#define ENCODER_CLK 4
#define ENCODER_DT 7
#define ENCODER_SW 8
int ultimoStatoEncoder = HIGH;
bool encoderPremuto = false;
int indiceMenu = 0;
int indiceUtenteVisualizzato = 0;
bool visualizzandoLog = false;
unsigned long ultimoSpostamentoMenu = 0;
const unsigned long debounceMenu = 300; // Sensibilità regolata

// --- Timer Allarme ---
unsigned long ultimoCambioAllarme = 0;
bool statoBuzzer = false;
const unsigned long intervalloAllarme = 500;

// --- Tentativi di accesso ---
int tentativiFalliti = 0;
const int MAX_TENTATIVI = 5;

// --- Struttura Utente ---
struct Utente {
  byte uid[4];
  String nome;
  String ruolo;
  int accessi;
};

Utente utenti[] = {
  {{0x8D, 0x1B, 0xF3, 0x2C}, "Salvatore", "Dipendente", 0},
  {{0xEC, 0x1C, 0x7B, 0xEC}, "Antonio", "Amministratore", 0}
};

const int NUM_UTENTI = sizeof(utenti) / sizeof(utenti[0]);
int indiceUtenteCorrente = -1;

void setup() {
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();
  lcd.init();
  lcd.backlight();

  servoPorta.attach(SERVO_PIN);
  servoPorta.write(0);

  pinMode(PIR_PIN, INPUT);
  pinMode(LED_ALLARME_PIN, OUTPUT);
  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(ENCODER_CLK, INPUT);
  pinMode(ENCODER_DT, INPUT);
  pinMode(ENCODER_SW, INPUT_PULLUP);

  lcd.setCursor(0, 0);
  lcd.print("Avvicinare badge");
  lcd.setCursor(0, 1);
  lcd.print("per accedere...");
}

void loop() {
  if (!accessoConsentito) {
    //checkPIR();
  }

  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    indiceUtenteCorrente = trovaUtente(rfid.uid.uidByte);
    if (indiceUtenteCorrente >= 0) {
      Utente &u = utenti[indiceUtenteCorrente];
      Serial.print("Accesso consentito a ");
      Serial.println(u.nome);
      u.accessi++;
      gestisciAccesso(u.nome, u.ruolo);
    } else {
      gestisciAccessoNegato();
    }
    rfid.PICC_HaltA();
  }

  if (allarmeAttivo) {
    if (millis() - ultimoCambioAllarme >= intervalloAllarme) {
      ultimoCambioAllarme = millis();
      statoBuzzer = !statoBuzzer;
      if (statoBuzzer) {
        tone(BUZZER_PIN, 800);
      } else {
        noTone(BUZZER_PIN);
      }
    }
  }

  if (accessoConsentito) {
    gestisciMenu();
  }
}

void gestisciAccesso(String nome, String ruolo) {
  lcd.clear();
  lcd.print("Benvenuto");
  lcd.setCursor(0, 1);
  lcd.print(nome);

  if (ruolo == "Amministratore") {
    impostaRGB(0, 0, 255);
  } else if (ruolo == "Dipendente") {
    impostaRGB(0, 255, 0);
  }

  tone(BUZZER_PIN, 1000, 200);
  servoPorta.write(90);
  delay(3000);
  servoPorta.write(0);
  lcd.clear();

  accessoConsentito = true;
  tentativiFalliti = 0;
  indiceMenu = 0;

  mostraMenu();
}

void gestisciAccessoNegato() {
  lcd.clear();
  lcd.print("Accesso negato");
  impostaRGB(255, 0, 0);
  tone(BUZZER_PIN, 400, 500);
  tentativiFalliti++;
  delay(1500);
  lcd.clear();

  if (tentativiFalliti >= MAX_TENTATIVI) {
    attivaAllarme();
  }
}

int trovaUtente(byte *uid) {
  for (int i = 0; i < NUM_UTENTI; i++) {
    bool corrisponde = true;
    for (int j = 0; j < 4; j++) {
      if (uid[j] != utenti[i].uid[j]) {
        corrisponde = false;
        break;
      }
    }
    if (corrisponde) return i;
  }
  return -1;
}

void impostaRGB(int r, int g, int b) {
  analogWrite(LED_R_PIN, r);
  analogWrite(LED_G_PIN, g);
  analogWrite(LED_B_PIN, b);
}

void attivaAllarme() {
  lcd.clear();
  lcd.print("ALLARME ATTIVO");
  digitalWrite(LED_ALLARME_PIN, HIGH);
  allarmeAttivo = true;
  delay(2000);
  lcd.clear();
}

void checkPIR() {
  if (digitalRead(PIR_PIN) == HIGH) {
    lcd.clear();
    lcd.print("Movimento rilevato");
    lcd.setCursor(0, 1);
    lcd.print("Senza accesso");
    attivaAllarme();
  }
}

void gestisciMenu() {
  int statoClk = digitalRead(ENCODER_CLK);
  if ((statoClk != ultimoStatoEncoder && millis() - ultimoSpostamentoMenu > debounceMenu) || indiceUtenteVisualizzato == -1) {
    if (visualizzandoLog) {
      if (digitalRead(ENCODER_DT) != statoClk) {
        indiceUtenteVisualizzato++;
      } else {
        indiceUtenteVisualizzato--;
      }
      indiceUtenteVisualizzato = constrain(indiceUtenteVisualizzato, 0, NUM_UTENTI - 1);
      lcd.clear();
      lcd.print(utenti[indiceUtenteVisualizzato].nome);
      lcd.setCursor(0, 1);
      lcd.print("Accessi: ");
      lcd.print(utenti[indiceUtenteVisualizzato].accessi);
    } else {
      if (digitalRead(ENCODER_DT) != statoClk) {
        indiceMenu++;
      } else {
        indiceMenu--;
      }
      if (utenti[indiceUtenteCorrente].ruolo == "Amministratore") {
        indiceMenu = constrain(indiceMenu, 0, 2);
      } else {
        indiceMenu = constrain(indiceMenu, 0, 1);
      }
      mostraMenu();
    }
    ultimoSpostamentoMenu = millis();
  }
  ultimoStatoEncoder = statoClk;

  if (digitalRead(ENCODER_SW) == LOW && !encoderPremuto) {
    if (visualizzandoLog) {
      visualizzandoLog = false;
      mostraMenu();
    } else {
      selezionaOpzioneMenu();
    }
    encoderPremuto = true;
  } else if (digitalRead(ENCODER_SW) == HIGH) {
    encoderPremuto = false;
  }
}

void mostraMenu() {
  lcd.clear();
  if (utenti[indiceUtenteCorrente].ruolo == "Amministratore") {
    if (indiceMenu == 0) lcd.print("1. Vedi Log");
    else if (indiceMenu == 1) lcd.print("2. Attiva Allarme");
    else if (indiceMenu == 2) lcd.print("3. Reset Allarme");
  } else {
    if (indiceMenu == 0) lcd.print("1. I miei accessi");
    else lcd.print("2. Attiva Allarme");
  }
}

void selezionaOpzioneMenu() {
  lcd.clear();
  if (utenti[indiceUtenteCorrente].ruolo == "Amministratore") {
    if (indiceMenu == 0) {
      visualizzandoLog = true;
      indiceUtenteVisualizzato = -1;
      ultimoStatoEncoder = digitalRead(ENCODER_CLK); // 🔁 reset stato encoder per non bloccare il primo input
    } else if (indiceMenu == 1) {
      attivaAllarme();
    } else if (indiceMenu == 2){
      allarmeAttivo = false;
      digitalWrite(LED_ALLARME_PIN, LOW);
      noTone(BUZZER_PIN);
      lcd.print("Allarme disattivo");
      delay(1500);
    }
  } else {
    if (indiceMenu == 0) {
      lcd.print("Accessi: ");
      lcd.setCursor(0, 1);
      lcd.print(utenti[indiceUtenteCorrente].accessi);
      delay(2000);
    } else {
      attivaAllarme();
    }
  }
 lcd.clear();
  if (!visualizzandoLog) {
    mostraMenu();
  }
}