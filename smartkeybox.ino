// IoT-based Smart Keybox with NFC Authentication - Main Application
// Required Libraries (Install via Library Manager):
// - PubSubClient (by Nick O'Leary)
// - Adafruit PN532
// - LiquidCrystal I2C

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_PN532.h>
#include "secrets.h"

// --- PIN DEFINITIONS ---
const int keyPins[8]  = {32, 16, 17, 18, 19, 23, 25, 26};
const int relayPin    = 4;
const int buzzerPin   = 27;

// PN532 I2C requires IRQ and RESET pins defined for the library,
// even if we poll it non-blocking. (Using arbitrary unused pins)
#define PN532_IRQ     (5)
#define PN532_RESET   (33)

// --- GLOBAL OBJECTS ---
WiFiClient espClient;
PubSubClient mqtt(espClient);
LiquidCrystal_I2C lcd(0x27, 16, 2); // Change 0x27 to 0x3F if LCD doesn't show text
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// --- DYNAMIC TOPICS ---
String macAddr = "";
String topicKeyStatus = "";
String topicRFID = "";
String topicRelay = "";

// --- STATE TRACKING VARIABLES (Non-Blocking) ---
String lastKeyStatus = "";

bool relayActive = false;
unsigned long relayTriggerTime = 0;
const unsigned long RELAY_UNLOCK_DURATION = 5000; // Increased to 5 seconds!

unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastRfidScanTime = 0;
String lastRfidUid = "";
unsigned long rfidCooldownTime = 0;

// --- LCD STATE TRACKING ---
unsigned long lcdMessageTime = 0;
bool isLcdMessageActive = false;
const unsigned long LCD_MESSAGE_DURATION = 3000; // 3 seconds to show temporary messages

// --- FUNCTION DECLARATIONS ---
void setupWiFi();
void reconnectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void checkSensors();
void checkRFID();
void beep(int freq, int duration);
void displayTempMessage(String msg);

void setup() {
  Serial.begin(115200);

  // 1. Initialize Hardware Pins
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW); // Locked by default (assume LOW = locked)
  pinMode(buzzerPin, OUTPUT);

  for (int i = 0; i < 8; i++) {
    pinMode(keyPins[i], INPUT);
  }

  // 2. Initialize LCD
  Wire.begin(21, 22); // SDA = 21, SCL = 22
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Smart Key Box");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  // 3. Initialize PN532
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Didn't find PN53x board. Check wiring!");
    lcd.setCursor(0, 1);
    lcd.print("RFID Error!    ");
  } else {
    // Configure PN532 to read RFID tags (Non-blocking mode setup)
    nfc.SAMConfig();
    Serial.println("Found PN532 RFID reader.");
  }

  beep(1000, 150); beep(1500, 150); // Startup tune

  // 4. Setup Network & Topics
  setupWiFi();

  macAddr = WiFi.macAddress();
  macAddr.replace(":", ""); // Remove colons to make a clean topic (e.g. AABBCCDDEEFF)

  topicKeyStatus = "smartkeybox/keystatus/" + macAddr;
  topicRFID      = "smartkeybox/rfid/" + macAddr;
  topicRelay     = "smartkeybox/relay/" + macAddr;

  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqttCallback);

  lcd.setCursor(0, 1);
  lcd.print("System Ready!   ");
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. Maintain WiFi & MQTT Connection (Non-Blocking)
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected()) {
      if (currentMillis - lastMqttReconnectAttempt > 5000) {
        lastMqttReconnectAttempt = currentMillis;
        reconnectMQTT();
      }
    } else {
      mqtt.loop(); // Process incoming MQTT messages
    }
  }

  // Refresh currentMillis because mqtt.loop() might have executed delays inside the callback!
  currentMillis = millis();

  // 2. Handle Non-Blocking Relay Timer
  if (relayActive) {
    if (currentMillis - relayTriggerTime >= RELAY_UNLOCK_DURATION) {
      digitalWrite(relayPin, LOW); // Lock back
      relayActive = false;
      Serial.println("Relay locked.");
      displayTempMessage("System Locked");
    }
  }

  // 3. Check Sensor Status
  checkSensors();

  // 4. Check RFID Reader
  checkRFID();

  // 5. Handle LCD Idle State
  if (isLcdMessageActive && (currentMillis - lcdMessageTime >= LCD_MESSAGE_DURATION)) {
    lcd.setCursor(0, 1);
    lcd.print("System Ready!   ");
    isLcdMessageActive = false;
  }
}

// --- CORE FUNCTIONS ---

void setupWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // We wait here initially, but if it drops later, loop() handles it non-blocking
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
}

void reconnectMQTT() {
  Serial.print("Attempting MQTT connection...");
  // Create a random client ID
  String clientId = "SmartKeyBox-" + String(random(0, 0xffff), HEX);

  if (mqtt.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
    Serial.println("connected");
    // Subscribe to the relay topic once connected
    mqtt.subscribe(topicRelay.c_str());

    // Force a publish of the current state upon fresh connection
    lastKeyStatus = "";
  } else {
    Serial.print("failed, rc=");
    Serial.print(mqtt.state());
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String incomingTopic = String(topic);
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.println("MQTT Received [" + incomingTopic + "]: " + message);

  // Check if we received a command on the relay topic
  if (incomingTopic == topicRelay) {
    if (message == "TRIGGER") {
      Serial.println("Access Granted! Unlocking...");

      digitalWrite(relayPin, HIGH); // Unlock (Change to LOW if your relay is active-low)
      relayActive = true;
      relayTriggerTime = millis();  // Record the exact time we opened it

      // Happy success tune (ascending)
      beep(1500, 100);
      delay(150);
      beep(2500, 150);

      displayTempMessage("Unlocked!");
    }
    else if (message == "NOACCESS") {
      Serial.println("Access Denied!");

      // Angry/Error tune (descending & low pitch)
      beep(500, 200);
      delay(250);
      beep(300, 300);

      displayTempMessage("Access Denied!");
    }
  }
}

void checkSensors() {
  String currentStatus = "";

  for (int i = 0; i < 8; i++) {
    int state = digitalRead(keyPins[i]);
    currentStatus += String(state);
    if (i < 7) currentStatus += ","; // Add comma separator
  }

  // Only publish if the state has changed (prevents spamming the broker)
  if (currentStatus != lastKeyStatus) {
    Serial.println("Key status changed: " + currentStatus);

    if (mqtt.connected()) {
      // true argument sets the RETAINED flag on the MQTT broker
      mqtt.publish(topicKeyStatus.c_str(), currentStatus.c_str(), true);
    }
    lastKeyStatus = currentStatus;
  }
}

void checkRFID() {
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
  uint8_t uidLength;                        // Length of the UID (4 or 7 bytes depending on ISO14443A card type)

  // Read target with a 50ms timeout to ensure it doesn't block the loop!
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 50);

  if (success) {
    unsigned long currentMillis = millis();
    String newUid = "";
    for (uint8_t i = 0; i < uidLength; i++) {
      newUid += String(uid[i], HEX);
    }
    newUid.toUpperCase();

    // Prevent spamming the same card multiple times a second (3-second cooldown)
    if (newUid != lastRfidUid || (currentMillis - rfidCooldownTime > 3000)) {
      Serial.println("RFID Detected: " + newUid);

      displayTempMessage("Card: " + newUid);

      beep(3000, 200);

      if (mqtt.connected()) {
        mqtt.publish(topicRFID.c_str(), newUid.c_str());
      }

      lastRfidUid = newUid;
      rfidCooldownTime = currentMillis;
    }
  }
}

void beep(int freq, int duration) {
  tone(buzzerPin, freq, duration);
  // tone() is natively non-blocking for duration on ESP32!
}

void displayTempMessage(String msg) {
  lcd.setCursor(0, 1);
  // Pad the message with spaces to 16 characters to overwrite any old text completely
  while (msg.length() < 16) {
    msg += " ";
  }
  lcd.print(msg);

  // Record the time we showed the message and mark it active
  lcdMessageTime = millis();
  isLcdMessageActive = true;
}
