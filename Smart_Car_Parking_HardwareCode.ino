#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Servo.h>
#include <ESP8266WiFi.h>      // For ESP8266 WiFi connectivity
#include <FirebaseESP8266.h> // For Firebase Realtime Database operations

// Firebase Configuration 
#define WIFI_SSID "Akshay"  
#define WIFI_PASSWORD "123456788"  


// Use the .firebasedatabase.app format for regional databases
#define FIREBASE_HOST "smart-parking-system-be293-default-rtdb.asia-southeast1.firebasedatabase.app" 
#define FIREBASE_AUTH "PvDHepMyc25ntpUJn3lP3CEqDyb9q9OySK6oJEem" 

// Define the paths in your Firebase Realtime Database for each slot's status
#define SLOT1_FIREBASE_PATH "/parking_slots/slot1/status"
#define SLOT2_FIREBASE_PATH "/parking_slots/slot2/status"

// ------ OLED configuration
#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 64  
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1); // -1 for no RESET pin

// --- IR Sensor Pins (NodeMCU D-pin labels) 

// D3 -> GPIO0, D4 -> GPIO2, D7 -> GPIO13, D8 -> GPIO15
#define ENTRY_IR D3  // IR sensor at entry point
#define EXIT_IR  D4  // IR sensor at exit point
#define SLOT1_IR D7  // IR sensor for parking Slot 1
#define SLOT2_IR D8  // IR sensor for parking Slot 2

// -------Servo Pins 
#define ENTRY_SERVO_PIN D5 // Servo for entry gate
#define EXIT_SERVO_PIN  D6 // Servo for exit gate

Servo entryGateServo;  
Servo exitGateServo;   
//  Gate Control & Timing -

unsigned long entryGateOpenedAt = 0; // Timestamp when entry gate was opened
unsigned long exitGateOpenedAt = 0;  // Timestamp when exit gate was opened
const unsigned long gateOpenDuration = 3000; // Gate stays open for 3 seconds (3000 milliseconds)
bool entryGateOpen = false; // Flag to track entry gate state
bool exitGateOpen = false;  // Flag to track exit gate state

// -------------------- Firebase Objects --------------------
FirebaseData firebaseData;      
FirebaseConfig firebaseConfig;  
FirebaseAuth firebaseAuth;      

// -------------------- Function Prototypes --------------------
void setupWiFi();
void firebaseReconnect();
void updateFirebaseSlotStatus(const char* path, int status);
bool isCarDetected(int pin); // Checks if a car is detected by an IR sensor
int getSlotOccupancyStatus(int sensorPin); // Returns 0 (Available) or 1 (Occupied)

void setup() {
  Serial.begin(115200); // Initialize serial communication for debugging

  // Configure IR sensor pins as INPUT
  pinMode(ENTRY_IR, INPUT);
  pinMode(EXIT_IR, INPUT);
  pinMode(SLOT1_IR, INPUT);
  pinMode(SLOT2_IR, INPUT);

  // Attach servo motors to their respective pins
  entryGateServo.attach(ENTRY_SERVO_PIN);
  exitGateServo.attach(EXIT_SERVO_PIN);
  
  // Set initial gate positions (0 degrees = closed)
  entryGateServo.write(0);
  exitGateServo.write(0);

  // Initialize OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x64 OLED
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Don't proceed, loop forever
  }

  // Initial OLED display messages
  display.clearDisplay();
  display.setTextSize(1);      // Smallest text size
  display.setTextColor(SSD1306_WHITE); // White text
  display.setCursor(0, 0);     // Start at top-left
  display.println("Smart Parking System");
  display.println("Initializing WiFi...");
  display.display();
  delay(1000);

  // -------------------- WiFi & Firebase Setup --------------------
  setupWiFi(); // Connect to WiFi

  // Configure Firebase (Host and Authentication)
  firebaseConfig.host = FIREBASE_HOST;
  // FIX: Use firebaseConfig.signer.tokens.legacy_token for database secret authentication
  firebaseConfig.signer.tokens.legacy_token = FIREBASE_AUTH; 
  
  // Begin Firebase connection using the configured objects
  Firebase.begin(&firebaseConfig, &firebaseAuth); 
  Firebase.reconnectWiFi(true); // Enable automatic WiFi reconnection

  Serial.println("Firebase setup complete.");
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Firebase Connected!");
  display.display();
  delay(1000);
}

void loop() {
  // Read status from IR sensors
  bool entryDetected = isCarDetected(ENTRY_IR);
  bool exitDetected = isCarDetected(EXIT_IR);
  
  // Get parking slot occupancy status (0 for Available, 1 for Occupied)
  int slot1Occupancy = getSlotOccupancyStatus(SLOT1_IR);
  int slot2Occupancy = getSlotOccupancyStatus(SLOT2_IR);

  // Calculate available slots
  int availableSlots = (slot1Occupancy == 0) + (slot2Occupancy == 0); // Sum of 1s for available slots

  // -------------------- Gate Control Logic --------------------
  // Entry Gate Logic
  if (entryDetected && !entryGateOpen) {
    if (availableSlots > 0) { // Only open entry gate if there are available slots
      entryGateServo.write(90); // Open gate
      entryGateOpenedAt = millis();
      entryGateOpen = true;
      Serial.println("Entry Gate: Opening (Car detected & slots available)");
    } else {
      Serial.println("Entry Gate: Blocked (No slots available)");
      // Optionally, display a message on OLED: "Parking Full"
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("PARKING FULL!");
      display.display();
      delay(1000); // Display message for a second
    }
  }

  // Close entry gate after duration
  if (entryGateOpen && millis() - entryGateOpenedAt >= gateOpenDuration) {
    entryGateServo.write(0); // Close gate
    entryGateOpen = false;
    Serial.println("Entry Gate: Closing");
  }

  // Exit Gate Logic
  if (exitDetected && !exitGateOpen) {
    exitGateServo.write(90); // Open gate
    exitGateOpenedAt = millis();
    exitGateOpen = true;
    Serial.println("Exit Gate: Opening (Car detected)");
  }

  // Close exit gate after duration
  if (exitGateOpen && millis() - exitGateOpenedAt >= gateOpenDuration) {
    exitGateServo.write(0); // Close gate
    exitGateOpen = false;
    Serial.println("Exit Gate: Closing");
  }

  // -------------------- Update Firebase Realtime Database --------------------
  if (Firebase.ready()) {
    updateFirebaseSlotStatus(SLOT1_FIREBASE_PATH, slot1Occupancy);
    updateFirebaseSlotStatus(SLOT2_FIREBASE_PATH, slot2Occupancy);
  } else {
    Serial.println("Firebase not ready for update. Attempting reconnect...");
    firebaseReconnect();
  }

  // -------------------- Update OLED Display --------------------
  display.clearDisplay();
  display.setCursor(0, 0);

  display.println("Smart Parking System");
  display.println("--------------------");
  display.print("Entry: "); display.println(entryDetected ? "Car Detected" : "Clear");
  display.print("Entry Gate: "); display.println(entryGateOpen ? "OPEN" : "CLOSED");
  display.println(" "); // Spacer

  display.print("Slot 1: "); display.println(slot1Occupancy == 1 ? "OCCUPIED" : "AVAILABLE");
  display.print("Slot 2: "); display.println(slot2Occupancy == 1 ? "OCCUPIED" : "AVAILABLE");
  display.println(" "); // Spacer

  display.print("Exit: "); display.println(exitDetected ? "Car Detected" : "Clear");
  display.print("Exit Gate: "); display.println(exitGateOpen ? "OPEN" : "CLOSED");
  display.println(" "); // Spacer

  display.print("Slots Free: "); display.println(availableSlots);

  display.display(); // Update the OLED display

  delay(200); // Small delay for loop stability and display refresh
}

// -------------------- Helper Functions Implementation --------------------

// Connects ESP8266 to WiFi network
void setupWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

// Handles Firebase and WiFi reconnection attempts
void firebaseReconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    setupWiFi(); // Reconnect WiFi if disconnected
  }
  if (!Firebase.ready()) {
    Serial.println("Re-initializing Firebase connection...");
    Firebase.begin(&firebaseConfig, &firebaseAuth); // Re-initialize Firebase
  }
}

// Updates a specific slot's status in Firebase Realtime Database
void updateFirebaseSlotStatus(const char* path, int status) {
  if (Firebase.setInt(firebaseData, path, status)) {
    // Serial.print("Firebase updated "); Serial.print(path); Serial.print(": "); Serial.println(status);
  } else {
    Serial.print("Firebase write failed for "); Serial.print(path); Serial.print(": "); Serial.println(firebaseData.errorReason());
  }
}

 
bool isCarDetected(int pin) {
  // Debounce logic: read twice with a small delay
  // IR sensors typically output LOW when an object is detected and HIGH when clear.
  // Adjust logic if your sensors behave differently.
  if (digitalRead(pin) == LOW) {
    delay(50); // Small debounce delay
    return digitalRead(pin) == LOW; // Confirm reading after debounce
  }
  return false;
}
 
int getSlotOccupancyStatus(int sensorPin) {
  // For IR sensors, LOW typically means object detected (occupied)
  // and HIGH means no object (available).
  if (digitalRead(sensorPin) == LOW) {
    return 1; // Occupied
  } else {
    return 0; // Available
  }
}