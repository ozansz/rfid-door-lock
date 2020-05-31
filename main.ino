#include <SPI.h> 
#include <Wire.h>
#include <MFRC522.h>

/* Arduino Pin Connections
 * 
 * Pin 2 - Wipe Cards Button
 * Pin 3 - Add Card Button
 * Pin 4 - Card Read Led
 * Pin 6 - Card Waiting Led
 * Pin 7 - Access Granted Led
 * Pin 9 - RC522 Reset Pin
 * Pin 10 - RC522 SDA Pin
 * Pin 11 - RC522 MOSI Pin
 * Pin 12 - RC522 MISO Pin
 * Pin 13 - RC522 SCK Pin
 * 3.3V - RC522 VCC Pin
 * Pin A4 - 24C16 Pin 5
 * Pin A5 - 24C16 Pin 6
 * GND - 24C16 Pin 1, 2, 3, 4, 7
 * 5V - 24C16 Pin 8, AND Gate Pin VCC, RC522 VCC
 */

/* EEPROM Storage Memory Mapping
 * 
 * Page 1 -> Card UIDs (4 bytes each)
 * Page 1 [0xFF] -> logging_last_index
 * 
 * Page2 -> Granted access logs (4 bytes timestamp & 4 bytes card UID)
 */

#define GRANTED_CARDS_COUNT 10

#define EEPROM_START_PAGE 80
#define EEPROM_LAST_PAGE  87
#define EEPROM_PAGE_DWORDS  32

#define RFID_CARD_UID_SIZE  0x04
#define LOGGING_INDEX_ADDR  0xFF

#define ST_READ 0
#define ST_NEW  1
#define ST_WIPE 2

#define USE_DEBUG
//#undef USE_DEBUG

MFRC522 rfid(10, 9);

const int cardReadLed = 4;
const int accessGrantedLed = 7;
const int newCardLed = 6;
const int newCardButtonInterrupt = 3;
const int wipeCardsButtonInterrupt = 2;

byte grantedCards[GRANTED_CARDS_COUNT][4];
const byte MASTER_CARD[4] = {110, 95, 111, 96};

const int eepromCardStoragePage = EEPROM_START_PAGE;
const int eepromLogStoragePage = EEPROM_START_PAGE + 1;

int logging_last_index;

int __system_state = ST_READ;

void _changeSystemStateNew() {
    __system_state = ST_NEW;
}

void _changeSystemStateWipe() {
    __system_state = ST_WIPE;
}

void checkEEPROM() {
  byte code;
  
  Wire.beginTransmission(EEPROM_START_PAGE);
  code = Wire.endTransmission();

  if (code == 0)
    Serial.println("[+] EEPROM OK.");
  else {
    Serial.print("[!] EEPROM is NOT OK: code ");
    Serial.println(code, DEC);

    while (1);
  }
}

void getLastLoggingAddress(int *save) {
    Wire.beginTransmission(eepromCardStoragePage); 
    Wire.write(LOGGING_INDEX_ADDR);               
    Wire.endTransmission();       

    Wire.requestFrom(eepromCardStoragePage, 1);
  
    for (int i = 0; Wire.available() && (i < 1); i++)
      *save = ((int) Wire.read());
}

void saveLastLoggingAddress(int save) {
    Wire.beginTransmission(eepromCardStoragePage); 
    Wire.write(LOGGING_INDEX_ADDR);               
    Wire.write(save);            
    Wire.endTransmission();       
    delay(10);
}

void logAccessToEEPROM(byte cardUID[4], unsigned long timestamp) {
    int index = (logging_last_index + 1) % EEPROM_PAGE_DWORDS;
    logging_last_index = index;

    char cbuf[] = {cardUID[0], cardUID[1], cardUID[2], cardUID[3]};

    for (int i = 0; i < 4; i++) {
      Wire.beginTransmission(eepromLogStoragePage); 
      Wire.write(logging_last_index * 0x08 + i);               
      Wire.write((timestamp >> (4 * (3 - i))) & 0xFF);            
      Wire.endTransmission();       
      delay(10); 
    }

    Wire.beginTransmission(eepromLogStoragePage); 
    Wire.write(logging_last_index * 0x08 + 0x04);               
    Wire.write(cbuf);            
    Wire.endTransmission();       
    delay(10);

    saveLastLoggingAddress(logging_last_index);
}

void getGrantedCardUIDs(byte cardArray[GRANTED_CARDS_COUNT][4]) {
  for (int pos = 0; pos < GRANTED_CARDS_COUNT; pos++) {
    unsigned int address = pos * RFID_CARD_UID_SIZE;

    Wire.beginTransmission(eepromCardStoragePage); 
    Wire.write(address);               
    Wire.endTransmission();       

    Wire.requestFrom(eepromCardStoragePage, 4);
  
    for (int index = 0; Wire.available() && (index < 4); index++)
      cardArray[pos][index] = Wire.read();
  }  
}

void dumpGrantedCards(byte cards[GRANTED_CARDS_COUNT][4]) {
  Serial.println();

#ifdef USE_DEBUG
  for (int i = 0; i < GRANTED_CARDS_COUNT; i++) {
    Serial.print("==> Card #");
    Serial.print(i, DEC);
    Serial.print(": ");

    for (int j = 0; j < 4; j++) {
      Serial.print(cards[i][j]);

      if (j < 3)
        Serial.print(" - ");
    }

    Serial.println();
  }

  Serial.println();
#endif
}

void saveCardToEEPROM(byte uidArray[4], int slot) {
  Serial.print("[+] Saving card ");
  Serial.print(uidArray[0]);
  Serial.print(".");
  Serial.print(uidArray[1]);
  Serial.print(".");
  Serial.print(uidArray[2]);
  Serial.print(".");
  Serial.print(uidArray[3]);
  Serial.print(" to EEPROM slot #");
  Serial.print(slot);
  Serial.print("... ");

  char buf[4] = {uidArray[0], uidArray[1], uidArray[2], uidArray[3]};

  Wire.beginTransmission(eepromCardStoragePage); 
  Wire.write(slot * RFID_CARD_UID_SIZE);               
  Wire.write(buf);            
  Wire.endTransmission();       
  delay(10);

  Serial.println("DONE.");
}

void wipeCards() {
Serial.println();
  Serial.println("[+] Please use the master card to approve the action.");

  digitalWrite(newCardLed, HIGH);

  bool _done = false;

  while (!_done) {
    if (rfid.PICC_IsNewCardPresent()) {    
      if (rfid.PICC_ReadCardSerial()) {
        digitalWrite(cardReadLed, HIGH);

        if ((MASTER_CARD[0] == rfid.uid.uidByte[0]) && (MASTER_CARD[1] == rfid.uid.uidByte[1]) && (MASTER_CARD[2] == rfid.uid.uidByte[2]) && (MASTER_CARD[3] == rfid.uid.uidByte[3])) {
          Serial.println("[+] Wiping all the card data... ");

          digitalWrite(accessGrantedLed, HIGH);

          saveCardToEEPROM(MASTER_CARD, 0);

          for (int i = RFID_CARD_UID_SIZE; i < GRANTED_CARDS_COUNT * RFID_CARD_UID_SIZE; i++) {
              Wire.beginTransmission(eepromCardStoragePage); 
              Wire.write(i);               
              Wire.write(0);            
              Wire.endTransmission();       
              delay(15);
            }
            
          getGrantedCardUIDs(grantedCards);

          Serial.println("[+] Wiping OK.");
        } else
          Serial.println("[!] Access denied.");

        _done = true;
        delay(1000);
      } 

      rfid.PICC_HaltA(); 
    }   
  }

  dumpGrantedCards(grantedCards);
  
  digitalWrite(newCardLed, LOW);
  digitalWrite(cardReadLed, LOW);
  digitalWrite(accessGrantedLed, LOW);
}

void defineNewCard() {
  Serial.println();
  Serial.println("[+] Waiting for the master card to approve the action.");

  digitalWrite(newCardLed, HIGH);

  bool _done = false;

  while (!_done) {
    if (rfid.PICC_IsNewCardPresent()) {    
      if (rfid.PICC_ReadCardSerial()) {
        digitalWrite(cardReadLed, HIGH);

        if ((MASTER_CARD[0] == rfid.uid.uidByte[0]) && (MASTER_CARD[1] == rfid.uid.uidByte[1]) && (MASTER_CARD[2] == rfid.uid.uidByte[2]) && (MASTER_CARD[3] == rfid.uid.uidByte[3])) {
          Serial.println("[+] Access granted.");
          digitalWrite(accessGrantedLed, HIGH);
        } else {
          Serial.println("[!] Access denied.");
          
          digitalWrite(accessGrantedLed, LOW);
          digitalWrite(cardReadLed, LOW);
          digitalWrite(newCardLed, LOW);
          
          return;
        }

        _done = true;
        delay(1000);
      } 

      rfid.PICC_HaltA(); 
    }   
  }

  digitalWrite(accessGrantedLed, LOW);
  _done = false;
  Serial.println("[+] Waiting for the new card.");

  while (!_done) {
    if (rfid.PICC_IsNewCardPresent()) {    
      if (rfid.PICC_ReadCardSerial()) {
        digitalWrite(cardReadLed, HIGH);
      
        Serial.print("+++ New Card ID: ");
        Serial.print(rfid.uid.uidByte[0]); 
        Serial.print(".");
        Serial.print(rfid.uid.uidByte[1]); 
        Serial.print("."); 
        Serial.print(rfid.uid.uidByte[2]); 
        Serial.print("."); 
        Serial.println(rfid.uid.uidByte[3]);

        for (int i = 0; i < GRANTED_CARDS_COUNT; i++) {
          if ((grantedCards[i][0] == rfid.uid.uidByte[0]) && (grantedCards[i][1] == rfid.uid.uidByte[1]) && (grantedCards[i][2] == rfid.uid.uidByte[2]) && (grantedCards[i][3] == rfid.uid.uidByte[3])) {
            Serial.println("[!] Card already granted.");

            _done = true;
            break;
          }
          
          if ((grantedCards[i][0] == 0) && (grantedCards[i][1] == 0) && (grantedCards[i][2] == 0) && (grantedCards[i][3] == 0)) {
            Serial.print("[i] Writing card to slot #");
            Serial.println(i);
            
            digitalWrite(accessGrantedLed, HIGH);
            delay(500);

            grantedCards[i][0] = rfid.uid.uidByte[0];
            grantedCards[i][1] = rfid.uid.uidByte[1];
            grantedCards[i][2] = rfid.uid.uidByte[2];
            grantedCards[i][3] = rfid.uid.uidByte[3];

            saveCardToEEPROM(rfid.uid.uidByte, i);
            
            _done = true;
            break;
          }
        }

        if (_done)
          break;

        Serial.println("[~] All slots are full. Will re-write the last slot.");

        grantedCards[GRANTED_CARDS_COUNT-1][0] = rfid.uid.uidByte[0];
        grantedCards[GRANTED_CARDS_COUNT-1][1] = rfid.uid.uidByte[1];
        grantedCards[GRANTED_CARDS_COUNT-1][2] = rfid.uid.uidByte[2];
        grantedCards[GRANTED_CARDS_COUNT-1][3] = rfid.uid.uidByte[3];

        saveCardToEEPROM(rfid.uid.uidByte, GRANTED_CARDS_COUNT-1);

        _done = true;

        delay(500);
        
        digitalWrite(cardReadLed, LOW);
        digitalWrite(accessGrantedLed, LOW);
      } 

      rfid.PICC_HaltA(); 
    }   
  }

  dumpGrantedCards(grantedCards);
  
  digitalWrite(newCardLed, LOW);
  digitalWrite(cardReadLed, LOW);
  digitalWrite(accessGrantedLed, LOW);
}

void waitForCardInput() {
  if (rfid.PICC_IsNewCardPresent()) {    
    if (rfid.PICC_ReadCardSerial()) {
      digitalWrite(cardReadLed, HIGH);
      
      Serial.print("++ Card ID: ");
      Serial.print(rfid.uid.uidByte[0]); 
      Serial.print(".");
      Serial.print(rfid.uid.uidByte[1]); 
      Serial.print("."); 
      Serial.print(rfid.uid.uidByte[2]); 
      Serial.print("."); 
      Serial.println(rfid.uid.uidByte[3]);

      for (int i = 0; i < GRANTED_CARDS_COUNT; i++)
        if ((grantedCards[i][0] == rfid.uid.uidByte[0]) && (grantedCards[i][1] == rfid.uid.uidByte[1]) && (grantedCards[i][2] == rfid.uid.uidByte[2]) && (grantedCards[i][3] == rfid.uid.uidByte[3])) {
          Serial.println("  ==> ACCESS GRANTED.");
          digitalWrite(accessGrantedLed, HIGH);

          logAccessToEEPROM(rfid.uid.uidByte, millis());
          
          delay(500);
          break;
        }

      delay(500);
      digitalWrite(cardReadLed, LOW);
      digitalWrite(accessGrantedLed, LOW);
    } 

    rfid.PICC_HaltA(); 
  } 
}

void setup() {
  pinMode(cardReadLed, OUTPUT);
  pinMode(accessGrantedLed, OUTPUT);
  pinMode(newCardLed, OUTPUT);

  pinMode(newCardButtonInterrupt, INPUT);
  attachInterrupt(digitalPinToInterrupt(newCardButtonInterrupt), _changeSystemStateNew, RISING);

  pinMode(wipeCardsButtonInterrupt, INPUT);
  attachInterrupt(digitalPinToInterrupt(wipeCardsButtonInterrupt), _changeSystemStateWipe, RISING);

  Wire.begin();
  Serial.begin(9600);

  while (!Serial);

  Serial.println("[+] Serial OK.");
  
  SPI.begin();
  rfid.PCD_Init(); 

  digitalWrite(cardReadLed, HIGH);
  digitalWrite(accessGrantedLed, HIGH);
  digitalWrite(newCardLed, HIGH);

  checkEEPROM();
  getLastLoggingAddress(&logging_last_index);
  getGrantedCardUIDs(grantedCards);
  dumpGrantedCards(grantedCards);

  delay(2000);

  digitalWrite(cardReadLed, LOW);
  digitalWrite(accessGrantedLed, LOW);
  digitalWrite(newCardLed, LOW);

  Serial.println("[+] Ready.");
} 

void loop() { 
  if (__system_state == ST_NEW) {
    defineNewCard();
    __system_state = ST_READ;
    delay(1000);
    Serial.println("[+] Ready.");
  } else if (__system_state == ST_WIPE) {
    wipeCards();
    __system_state = ST_READ;
    delay(1000);
    Serial.println("[+] Ready.");
  } else
    waitForCardInput();
}
