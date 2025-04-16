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

// --- LED Allarme (digitale D2) ---
#define LED_ALLARME_PIN 2
bool allarmeAttivo = false;

// --- Servo (A3) ---
Servo servoPorta;
#define SERVO_PIN A3

// --- Buzzer (analogico A2) ---
#define BUZZER_PIN A2

// --- Fotoresistenza (analogico A1) ---
#define FOTORES_PIN A1

// --- Encoder ---
#define ENCODER_CLK 4
#define ENCODER_DT 7
#define ENCODER_SW 8
int ultimoStatoEncoder = HIGH;
bool encoderPremuto = false;
int indiceMenu = 0;
int indiceUtenteVisualizzato = 0;
bool visualizzandoLog = false;
bool visualizzandoLuminosita = false;
unsigned long ultimoSpostamentoMenu = 0;
const unsigned long debounceMenu = 300;

// --- PID ---
float setpoint = 700.0; // valore target di luminosità
float kp = 0.5, ki = 0.05, kd = 0.1;
float errorePrecedente = 0, integrale = 0;

// --- Timer Allarme ---
unsigned long ultimoCambioAllarme = 0;
bool statoBuzzer = false;
const unsigned long intervalloAllarme = 500;

// --- Tentativi di accesso ---
int tentativiFalliti = 0;
const int MAX_TENTATIVI = 5;

// Limiti per l'integrale (anti-windup)
float integraleMin = -500;
float integraleMax = 500;

// Variabile per smoothing
float uscitaSmoothed = 50;

float luminositaAttuale = 0;  // inizializza in alto

// --- Struttura Utente ---
struct Utente {
  byte uid[4];
  String nome;
  String ruolo;
  int accessi;
};

Utente utenti[] = {
  {{0x3B, 0x26, 0xA1, 0x52}, "Salvatore", "Dipendente", 0},
  {{0x4B, 0x1F, 0xB3, 0x52}, "Antonio", "Dipendente", 0},
  {{0x4B, 0x73, 0xD3, 0x52}, "Gianluca", "Amministratore", 0},
  {{0xDB, 0x72, 0xB6, 0x52}, "Carlo", "Amministratore", 0}
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
  servoPorta.write(6);

  pinMode(PIR_PIN, INPUT);
  pinMode(LED_ALLARME_PIN, OUTPUT);
  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(ENCODER_CLK, INPUT);
  pinMode(ENCODER_DT, INPUT);
  pinMode(ENCODER_SW, INPUT_PULLUP);

  digitalWrite(LED_ALLARME_PIN, HIGH);

  lcd.setCursor(0, 0);
  lcd.print("Avvicinare badge");
  lcd.setCursor(0, 1);
  lcd.print("per accedere...");
}

void loop() {
  
  gestisciPID();

  if (!accessoConsentito) {
    checkPIR();
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
  servoPorta.write(6);
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
  digitalWrite(LED_ALLARME_PIN, LOW);
  allarmeAttivo = true;
  delay(2000);
  lcd.clear();
}

void gestisciPID() {
  int valoreLuce = analogRead(FOTORES_PIN);
  float errore = setpoint - valoreLuce;
  float derivata = errore - errorePrecedente;

  float nuovaIntegrale = integrale + errore;
  float outputTest = kp * errore + ki * nuovaIntegrale + kd * derivata;

  if (outputTest >= 0 && outputTest <= 255) {
    integrale = constrain(nuovaIntegrale, -500, 500);
  }

  //Serial.println("Valore luce");
  //Serial.println(valoreLuce);

  float output = kp * errore + ki * integrale + kd * derivata;
  errorePrecedente = errore;

  // Smoothing dinamico basato sulla distanza
  float distanza = abs(output - luminositaAttuale);
  float smoothingFactor = constrain(distanza / 100.0, 0.05, 0.4);
  luminositaAttuale += smoothingFactor * (output - luminositaAttuale);

  int intensita = constrain((int)luminositaAttuale, 0, 255);
  impostaRGB(intensita, intensita, intensita);

  if (visualizzandoLuminosita) {
    lcd.clear();
    lcd.print("Luce ambiente:");
    lcd.setCursor(0, 1);
    lcd.print(valoreLuce);
    delay(300);
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
        indiceMenu = constrain(indiceMenu, 0, 3);
      } else {
        indiceMenu = constrain(indiceMenu, 0, 1);
      }
      mostraMenu();
    }
    ultimoSpostamentoMenu = millis();
  }
  ultimoStatoEncoder = statoClk;

  if (digitalRead(ENCODER_SW) == LOW && !encoderPremuto) {
    if (visualizzandoLog || visualizzandoLuminosita) {
      visualizzandoLog = false;
      visualizzandoLuminosita = false;
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
    if (indiceMenu == 0) lcd.print("1.Vedi Log");
    else if (indiceMenu == 1) lcd.print("2.Attiva Allarme");
    else if (indiceMenu == 2) lcd.print("3.Reset Allarme");
    else if (indiceMenu == 3) lcd.print("4.Luce attuale");
  } else {
    if (indiceMenu == 0) lcd.print("1.I miei accessi");
    else lcd.print("2.Attiva Allarme");
  }
}

void selezionaOpzioneMenu() {
  lcd.clear();
  if (utenti[indiceUtenteCorrente].ruolo == "Amministratore") {
    if (indiceMenu == 0) {
      visualizzandoLog = true;
      indiceUtenteVisualizzato = -1;
      ultimoStatoEncoder = digitalRead(ENCODER_CLK);
    } else if (indiceMenu == 1) {
      attivaAllarme();
    } else if (indiceMenu == 2){
      allarmeAttivo = false;
      digitalWrite(LED_ALLARME_PIN, HIGH);
      noTone(BUZZER_PIN);
      lcd.print("Allarme disattivo");
      delay(1500);
    } else if (indiceMenu == 3) {
      visualizzandoLuminosita = true;
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
  if (!visualizzandoLog && !visualizzandoLuminosita) {
    mostraMenu();
  }
}

void checkPIR() {
  int valorePIR = analogRead(PIR_PIN);

  Serial.print("Valore PIR: ");
  Serial.println(valorePIR);

  if (valorePIR >= 650) {
    Serial.println("Movimento rilevato nel blocco!");
    if (!accessoConsentito) {
      attivaAllarme();
    }
  }

  delay(300); // <-- Aggiunto per rallentare la stampa
}


