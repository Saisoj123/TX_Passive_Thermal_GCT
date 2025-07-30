/*
 * TX Passive Thermal GCT - Master ESP32
 * 
 * Original code base: https://github.com/MoritzNelle/TX_Passive_Thermal_GCT
 * Enhanced by Josias Kern using GitHub Copilot (GPT-4)
 * 
 * Purpose: Ground Control Target for thermal-infrared drone calibration
 * Provides synchronized temperature reference data for aerial thermal imaging validation
 * 
 * Enhancements include:
 * - WiFi connectivity and NTP time synchronization (temporarily disabled for stability)
 * - Watchdog timer for system reliability
 * - Improved error handling and recovery
 * - Robust SD card and RTC operations
 * - Button control system with proper debounce logic
 * - Flexible logging with partial servant connectivity
 * - Enhanced debugging and status reporting
 * - Data integrity protection preventing servant cross-contamination
 */

// System Status - Recent Updates (2025-07-30)
// ✅ FIXED: Button press detection and debounce logic working correctly
// ✅ FIXED: Temperature values from offline-targets replaced with online-target values (data contamination)
// ✅ FIXED: Temperature values in log file corrupted by offline-target data
// ✅ FIXED: Display reset properly after fatal errors
// ✅ FIXED: Fake temperature data display for disconnected servants
// ✅ FIXED: Flexible logging system works with 1-4 servants (partial connectivity)
// ✅ FIXED: Enhanced debug output for connection status and data retrieval
// ✅ FIXED: Timeout handling and proper NAN logging for disconnected servants

// Known Issues - Pending Resolution
// TODO: Occasionally logging timer shows incorrect values (42947XXX seconds) - rare occurrence
// TODO: Display temperature values not completely overwritten when digit count changes
// TODO: Display and LED briefly freeze during temperature request ("Updating Temperature")
// TODO: Status LED occasionally shows brief "No connection" even when connected
// TODO: Display retains last temperature values when connection is lost (should show "--" or similar)

// System Architecture Notes
// - WiFi/NTP temporarily disabled to prevent watchdog timeouts during deployment
// - ESP-NOW communication on WiFi Channel 1 for servant coordination
// - Master-servant topology supports up to 4 GCT units with 9 sensors each
// - Real-time status monitoring via LCD display and LED indicators
// - Button-controlled logging for synchronized data collection during drone flights



#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <RTClib.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_NeoPixel.h>
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <WiFiUdp.h>

//User variables
int sendTimeout         = 1000;     //Timeout for waiting for a servent response data in ms
int logIntervall        = 10000;    //Log intervall in ms (>= 10000 ms = 10s)
int pingCheckIntervall  = 2000;     //Ping check intervall in ms (increased from 1000 to reduce interference)
int tempUpdateIntervall = 10000;    //Temperature update intervall in ms

// WiFi and NTP configuration
const char* ssid = "VodafoneMobileWiFi-A8E1";        // Replace with your WiFi network name
const char* password = "I5IJ4ij4"; // Replace with your WiFi password
const char* ntpServer = "time.google.com";  // Changed to Google's NTP (more reliable than pool.ntp.org)
const long gmtOffset_sec = 3600;            // GMT+1 for Amsterdam (CET)
const int daylightOffset_sec = 3600;        // +1 hour for summer time (CEST)

// Time management configuration
const unsigned long NTP_RETRY_INTERVAL = 3600000;    // Retry NTP sync every hour (3600000ms)
const unsigned long RTC_VALIDITY_CHECK = 86400000;   // Check RTC validity every 24 hours
unsigned long lastNTPSync = 0;                       // Timestamp of last successful NTP sync
unsigned long lastRTCCheck = 0;                      // Timestamp of last RTC validity check
bool ntpSyncSuccessful = false;                      // Flag to track if NTP ever succeeded

// structure to send data
typedef struct struct_message {
    int actionID;
    float value;
} struct_message;
struct_message TXdata;


typedef struct temp {
    int actionID;
    float sens1;
    float sens2;
    float sens3;
    float sens4;
    float sens5;
    float sens6;
    float sens7;
    float sens8;
    float sens9;
} temp;
temp RXData;

// system variables
volatile bool messageReceived   = false;
volatile int receivedActionID   = 0;
int numConnections              = 0;
int timeLeft                    = 0;
char timestamp[19];
char fileName[24];
bool connectionStatus           = false;
bool logState                   = false;
esp_err_t lastSendStatus        = ESP_FAIL;
temp receivedData;
File file;

// Connection state tracking
unsigned long lastConnectionCheck[4] = {0, 0, 0, 0};
bool deviceOnline[4] = {false, false, false, false};
const unsigned long CONNECTION_TIMEOUT = 5000; // 5 seconds

// Pin definitions
#define CS_PIN 5
#define LED_PIN 4
#define BUTTON_PIN 0


Adafruit_NeoPixel strip(1, LED_PIN, NEO_GRB + NEO_KHZ800);  // Create an instance of the Adafruit_NeoPixel class

uint8_t broadcastAddresses[][6] = {
    {0x48, 0xE7, 0x29, 0x8C, 0x79, 0x68},  // Servant 1 (COM13) - GCT1
    {0x48, 0xE7, 0x29, 0x8C, 0x73, 0x18},  // Servant 2 (COM15) - GCT2
    {0x4C, 0x11, 0xAE, 0x65, 0xBD, 0x54},  // Servant 3 (COM16) - GCT3
    {0x48, 0xE7, 0x29, 0x8C, 0x72, 0x50}   // Servant 4 (unknown) - GCT4
};
esp_now_peer_info_t peerInfo[4];

RTC_DS3231 rtc;

LiquidCrystal_I2C lcd(0x27, 20, 4); // set the LCD address to 0x27 for a 20 chars and 4 line display


void sendLogState(bool logState){
        if(logState){
        TXdata.actionID = 1002;
        for (int i = 0; i < 4; i++) {
            esp_err_t result = esp_now_send(broadcastAddresses[i], (uint8_t *) &TXdata, sizeof(TXdata));
        }
    }else{
        TXdata.actionID = 1003;
        for (int i = 0; i < 4; i++) {
            esp_err_t result = esp_now_send(broadcastAddresses[i], (uint8_t *) &TXdata, sizeof(TXdata));
        }
    }
}


void buttonState(){ //MARK: Button state
    static unsigned long lastButtonPress = 0;
    static bool lastButtonState = HIGH;
    static unsigned long lastDebugPrint = 0;
    const unsigned long debounceDelay = 50;
    
    bool currentButtonState = digitalRead(BUTTON_PIN);
    
    // Debug: Print button state every 5 seconds
    if (millis() - lastDebugPrint > 5000) {
        lastDebugPrint = millis();
        Serial.printf("Button Debug: Pin %d = %s, logState = %s\n", 
                     BUTTON_PIN, currentButtonState ? "HIGH" : "LOW", logState ? "ON" : "OFF");
    }
    
    // Check if button state changed
    if (currentButtonState != lastButtonState) {
        Serial.printf("Button state changed: %s -> %s at %lu ms\n", 
                     lastButtonState ? "HIGH" : "LOW", currentButtonState ? "HIGH" : "LOW", millis());
        
        // If button was just pressed (HIGH to LOW transition)
        if (currentButtonState == LOW && lastButtonState == HIGH) {
            lastButtonPress = millis();  // Record press time only when button is pressed
            Serial.println("Button PRESSED - will toggle after debounce");
        }
        // If button was just released (LOW to HIGH transition) 
        else if (currentButtonState == HIGH && lastButtonState == LOW) {
            Serial.println("Button RELEASED - checking debounce");
            
            // Check if the press duration was long enough (debounce)
            if ((millis() - lastButtonPress) > debounceDelay) {
                // Toggle logging state
                logState = !logState;
                Serial.printf("DEBOUNCE OK - Toggling logState to %s\n", logState ? "ON" : "OFF");
                
                sendLogState(logState);
                if (logState) {
                    timeLeft = 0; // Start logging immediately
                    // Immediately update display to show logging status
                    lcd.setCursor(0, 3);
                    if (numConnections > 0) {
                        lcd.print("Logging: Starting...");
                    } else {
                        lcd.print("Logging: No connect  ");
                    }
                    Serial.println("=== LOGGING ACTIVATED ===");
                } else {
                    Serial.println("=== LOGGING DEACTIVATED ===");
                    lcd.setCursor(0, 3);
                    lcd.print("Idle (ready to log) ");
                }
                Serial.printf("Button pressed - Log state: %s, numConnections: %d\n", logState ? "ON" : "OFF", numConnections);
            } else {
                Serial.printf("DEBOUNCE FAILED - Duration: %lu ms (need > %lu ms)\n", 
                             (millis() - lastButtonPress), debounceDelay);
            }
        }
    }
    
    lastButtonState = currentButtonState;
}


void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {

    Serial.print(mac_addr[0], HEX); Serial.print(":");
    Serial.print(mac_addr[1], HEX); Serial.print(":");
    Serial.print(mac_addr[2], HEX); Serial.print(":");
    Serial.print(mac_addr[3], HEX); Serial.print(":");
    Serial.print(mac_addr[4], HEX); Serial.print(":");
    Serial.print(mac_addr[5], HEX); Serial.print(" --> ");

    if (status == ESP_NOW_SEND_SUCCESS) {
        Serial.println("Delivery Success");
        connectionStatus = true;
    } else {
        Serial.println("Delivery Fail");
        connectionStatus = false;
    }
    lastSendStatus = status == ESP_NOW_SEND_SUCCESS ? ESP_OK : ESP_FAIL;
}

void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
    // First, copy the actionID to determine the message type
    int incomingActionID;
    memcpy(&incomingActionID, incomingData, sizeof(int));
    
    if (incomingActionID == 1001) {
        // Connection test response - copy to struct_message
        struct_message connectionResponse;
        memcpy(&connectionResponse, incomingData, sizeof(connectionResponse));
        receivedActionID = connectionResponse.actionID;
    } else if (incomingActionID == 2001) {
        // Temperature data - copy to temp structure
        memcpy(&receivedData, incomingData, sizeof(receivedData));
        receivedActionID = receivedData.actionID;
    } else {
        // Unknown message type, try to copy as temp structure (default)
        memcpy(&receivedData, incomingData, sizeof(receivedData));
        receivedActionID = receivedData.actionID;
    }
    
    messageReceived = true;
}

void SerialUserInput() {
    while (!Serial.available()) {
        // Wait for user input
    }
    int userActionID = Serial.parseInt();
    TXdata.actionID = userActionID != 0 ? userActionID : 1; // Use user input if available, otherwise use default value

    TXdata.value = 2.0; // Replace with your actual default value

    for (int i = 0; i < 4; i++) {
        esp_err_t result = esp_now_send(broadcastAddresses[i], (uint8_t *) &TXdata, sizeof(TXdata));
    }
}

void updateConnectionStatus(bool status, int targetID) { //MARK: Update connection status
    lcd.setCursor(0, 1);
    lcd.print("S1:");
    lcd.setCursor(5, 1);
    lcd.print("S2:");
    lcd.setCursor(10, 1);
    lcd.print("S3:");
    lcd.setCursor(15, 1);
    lcd.print("S4:");

    
    switch (targetID)
    {
        case 1:
            lcd.setCursor(3, 1);
            break;

        case 2:
            lcd.setCursor(8, 1);
            break;

        case 3:
            lcd.setCursor(13, 1);
            break;

        case 4:
            lcd.setCursor(18, 1);
            break;
    }

    if (status) {
        lcd.write(byte(0)); // Tick mark
    } else {
        lcd.print("x");
    }
}


bool checkConnection(int locTargetID) { //MARK: Check connection
    static unsigned long lastCheckTime[4] = {0, 0, 0, 0};
    static bool lastCheckResult[4] = {false, false, false, false};
    
    // Implement cooldown: only check each servant once every 3 seconds to reduce interference
    unsigned long currentTime = millis();
    if (currentTime - lastCheckTime[locTargetID-1] < 3000) {
        return lastCheckResult[locTargetID-1]; // Return cached result
    }
    
    esp_err_t result;
    
    // structure to Action Code as a connection test
    typedef struct struct_message {
        int actionID;
    } struct_message;
    struct_message testData;

    testData.actionID = 1001;

    // Store current state to restore later
    bool previousMessageReceived = messageReceived;
    int previousReceivedActionID = receivedActionID;
    
    // Clear any previous message state
    messageReceived = false;
    receivedActionID = 0;

    // Send connection test message
    result = esp_now_send(broadcastAddresses[locTargetID-1], (uint8_t *) &testData, sizeof(testData));

    lastCheckTime[locTargetID-1] = currentTime;
    
    if (result == ESP_OK) {     // Check if the message was queued for sending successfully
        // Wait for response from servant with timeout
        unsigned long startTime = millis();
        const unsigned long responseTimeout = 800; // Increased timeout to 800ms for more reliable connection tests
        
        while (!messageReceived && (millis() - startTime) < responseTimeout) {
            delay(10); // Small delay to allow response processing
        }
        
        // Check if we received a valid response
        if (messageReceived && receivedActionID == 1001) {
            lastCheckResult[locTargetID-1] = true;
            // Don't reset messageReceived here to avoid clearing valid temperature data
            return true;
        } else {
            lastCheckResult[locTargetID-1] = false;
            // Restore previous state if no connection response was received
            messageReceived = previousMessageReceived;
            receivedActionID = previousReceivedActionID;
            return false;
        }
    } else {
        Serial.printf("ESP-NOW send failed for target %d: %d\n", locTargetID, result);
        lastCheckResult[locTargetID-1] = false;
        // Restore previous state
        messageReceived = previousMessageReceived;
        receivedActionID = previousReceivedActionID;
        return false;
    }
}


bool waitForActionID(int actionID, int targetID) { //MARK: Wait for action ID
    
    // if(checkConnection(targetID)){  //Only request data if the connection is established
        unsigned long startTime = millis();
        bool connectionStatus = false;

        while (!messageReceived || receivedActionID != actionID) {
            if (millis() - startTime > sendTimeout) {
                Serial.println("Timeout waiting for action ID on target: " + String(targetID));
                connectionStatus = false;
                break;
            }else{
                connectionStatus = true;
            }
        }
        messageReceived = false; // Reset for next message

        //updateConnectionStatus(connectionStatus, targetID);
        return connectionStatus; // FIXED: Return actual connection status instead of always true
    }
    // return (false);
// }


String tempToString(temp t, String timestamp, int serventID) {//MARK: To String
    String data = "";
    data += timestamp + "," + String(serventID) + ",1," + String(t.sens1) + "\n";
    data += timestamp + "," + String(serventID) + ",2," + String(t.sens2) + "\n";
    data += timestamp + "," + String(serventID) + ",3," + String(t.sens3) + "\n";
    data += timestamp + "," + String(serventID) + ",4," + String(t.sens4) + "\n";
    data += timestamp + "," + String(serventID) + ",5," + String(t.sens5) + "\n";
    data += timestamp + "," + String(serventID) + ",6," + String(t.sens6) + "\n";
    data += timestamp + "," + String(serventID) + ",7," + String(t.sens7) + "\n";
    data += timestamp + "," + String(serventID) + ",8," + String(t.sens8) + "\n";
    data += timestamp + "," + String(serventID) + ",9," + String(t.sens9) + "\n";
    return data;
}


const char* get_timestamp() {
    DateTime now = rtc.now();
    sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
    return timestamp;
}


void displayTemp(int targetID, temp t, bool isConnected = true) { //MARK: Display temperature

    switch (targetID)
    {
        case 1:
            lcd.setCursor( 0, 2);
            break;

        case 2:
            lcd.setCursor( 5, 2);
            break;

        case 3:
            lcd.setCursor(10, 2);
            break;

        case 4:
            lcd.setCursor(15, 2);
            break;
    }

    if (isConnected) {
        // Validate temperature readings and count valid sensors
        float tempSum = 0;
        int validSensorCount = 0;
        float temps[9] = {t.sens1, t.sens2, t.sens3, t.sens4, t.sens5, t.sens6, t.sens7, t.sens8, t.sens9};
        
        for (int i = 0; i < 9; i++) {
            // Check if temperature is reasonable (between -50°C and 100°C)
            if (temps[i] >= -50.0 && temps[i] <= 100.0) {
                tempSum += temps[i];
                validSensorCount++;
            }
        }
        
        if (validSensorCount > 0) {
            float avgTemp = tempSum / validSensorCount;
            lcd.print("     "); // Clear the area first (5 spaces)
            lcd.setCursor((targetID-1)*5, 2); // Reset cursor position
            lcd.printf("%.1f", avgTemp);
        } else {
            lcd.print(" --- "); // Show error indicator for invalid readings
        }
    } else {
        lcd.print("  -  "); // Connection lost indicator
    }
}
    

void blinkLED(int red, int green, int blue, int blinkIntervall) {
    static unsigned long previousMillis = 0;
    static bool ledState = false;
    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= blinkIntervall) {
        previousMillis = currentMillis;
        ledState = !ledState;

        if (ledState) {
            strip.setPixelColor(0, strip.Color(red, green, blue));
        } else {
            strip.setPixelColor(0, strip.Color(0, 0, 0));
        }

        strip.show();
    }
}


void updateStatusLED(int status, int blinkIntervall = 1000){ //MARK: Update status LED
    switch (status)
    {

    case 0:
        strip.setPixelColor(0, strip.Color(0, 0, 0)); // Turn off the LED
        break;

    case 1:
        strip.setPixelColor(0, strip.Color(255, 100, 0));   // Constant yellow
        break;
    
    case 2:
        blinkLED(0, 255, 0, blinkIntervall);    // Blink the LED in green
        break;
    
    case 3:
        strip.setPixelColor(0, strip.Color(0, 255, 0)); // Constant green
        break;

    case 4:
        strip.setPixelColor(0, strip.Color(255, 0, 0)); // Constant red
        break;

    case 5:
        blinkLED(255, 0, 0, blinkIntervall);    // Blink the LED in red
        break;

    case 6:
        blinkLED(255, 100, 0, blinkIntervall);  // Blink the LED in yellow
        break;
    
    default:
        break;
    }

    strip.show();
}


void displayTimeStamp() {
    lcd.setCursor(0, 0);
    lcd.print(get_timestamp());
}


void displayError(String errorMessage = "", int errorNr = 0){ //MARK: Display error
    lcd.clear();
    lcd.setCursor(0, 1);

    if (errorMessage != "" && errorNr != 0){
        lcd.printf("FATAL ERROR: Nr. %d", errorNr);
        lcd.setCursor(0, 2);
        lcd.print(errorMessage);
    }else if (errorMessage != "" && errorNr == 0){
        lcd.print("FATAL ERROR:");
        lcd.setCursor(0, 2);
        lcd.print(errorMessage);
    }else{
        lcd.print("FATAL ERROR (undef.)");
    }
}


void writeToSD(String dataString) { //MARK: Write to SD
    Serial.println("=== ATTEMPTING TO WRITE TO SD CARD ===");
    Serial.printf("Data to write: %s\n", dataString.c_str());
    
    // Check if SD card is still available
    if (!SD.begin(CS_PIN)) {
        Serial.println("SD Card not available for writing");
        displayError("SD Card unavailable", 2);
        updateStatusLED(5);
        return;
    }
    
    file = SD.open(fileName, FILE_APPEND); // Open the file in append mode

    if (!file){
        Serial.println("Failed to open file for writing");
        displayError("Failed to open file", 2);
        updateStatusLED(5);
        
        // Try to remount SD card
        delay(1000);
        if (SD.begin(CS_PIN)) {
            Serial.println("SD Card remounted successfully");
            file = SD.open(fileName, FILE_APPEND);
            if (!file) {
                Serial.println("Still failed to open file after remount");
                return;
            }
        } else {
            Serial.println("Failed to remount SD card");
            return;
        }
    }

    size_t bytesWritten = file.print(dataString);
    file.close();
    
    // Verify write was successful
    if (bytesWritten == 0) {
        Serial.println("Warning: No bytes written to SD card");
    } else {
        Serial.printf("=== SUCCESS: Wrote %d bytes to SD card ===\n", bytesWritten);
        Serial.printf("File: %s\n", fileName);
    }
}


void getAllTemps(bool save = true) {//MARK: Get temperatures

    updateStatusLED(0);
    lcd.setCursor(0, 3);
    lcd.print("Updating Temperature");

    TXdata.actionID = 3001; //Action ID for getting all temperatures from a servent

    for (int i = 0; i < 4; i++) {
        // Only try to get temperatures from connected servants
        bool servantConnected = checkConnection(i+1);
        if (servantConnected) {
            // Clear previous data to prevent contamination
            memset(&receivedData, 0, sizeof(receivedData));
            
            esp_err_t result = esp_now_send(broadcastAddresses[i], (uint8_t *) &TXdata, sizeof(TXdata));

            if (waitForActionID(2001,i+1/*Target ID*/) == true){
                Serial.printf("Successfully received data from servant %d\n", i+1);
                if (save == true)
                {
                    writeToSD(tempToString(receivedData,get_timestamp(), i+1));
                }
                displayTemp(i+1, receivedData, true);
            } else {
                Serial.printf("Failed to receive data from servant %d - logging NAN\n", i+1);
                // For failed data retrieval, log NAN if saving
                if (save == true) {
                    writeToSD(String(get_timestamp()) + "," + String(i+1) + ",123456789,NAN\n");
                }
                // Display shows "-" for failed servants
                temp emptyData = {0};
                displayTemp(i+1, emptyData, false);
            }
        } else {
            Serial.printf("Servant %d not connected - logging NAN\n", i+1);
            // For disconnected servants, log NAN if saving
            if (save == true) {
                writeToSD(String(get_timestamp()) + "," + String(i+1) + ",123456789,NAN\n");
            }
            // Display shows "-" for disconnected servants
            temp emptyData = {0};
            displayTemp(i+1, emptyData, false);
        }
    }
}


void logLoop() {    //MARK: Log loop
    static unsigned long previousExecution = 0;
    static unsigned long lastDisplayUpdate = 0;
    unsigned long currentTime = millis();

    // If timeLeft is 0, it means we should retrieve data
    if (timeLeft <= 0) {
        previousExecution = currentTime;
        timeLeft = logIntervall / 1000; // Reset to full interval in seconds
        
        // Only try to get temperatures if we have connected servants
        if (numConnections > 0) {
            Serial.println("=== RETRIEVING DATA FOR LOGGING ===");
            lcd.setCursor(0, 3);
            lcd.print("Retrieving Data...  ");
            getAllTemps();
            Serial.println("=== DATA RETRIEVAL COMPLETE ===");
        } else {
            Serial.println("Logging: No servants connected, skipping data collection");
            lcd.setCursor(0, 3);
            lcd.print("Logging: No connect  ");
        }
        lastDisplayUpdate = currentTime;
    } else {
        // Update the countdown display every second
        if (currentTime - lastDisplayUpdate >= 1000) {
            lastDisplayUpdate = currentTime;
            timeLeft = logIntervall / 1000 - (currentTime - previousExecution) / 1000;
            
            // Ensure timeLeft doesn't go negative
            if (timeLeft < 0) {
                timeLeft = 0;
            }
            
            lcd.setCursor(0, 3);
            if (numConnections > 0) {
                lcd.print("Logging:");
                lcd.setCursor(8, 3);
                lcd.printf(" %d s        ", timeLeft);
                Serial.printf("Logging countdown: %d seconds\n", timeLeft);
            } else {
                lcd.print("Logging: No connect  ");
                Serial.println("Logging: No connections available");
            }
        }
    }
    
    // Debug output (commented out)
    // Serial.println("timeLeft: " + String(timeLeft));
    // Serial.println("currentTime: " + String(currentTime));
    // Serial.println("previousExecution: " + String(previousExecution));
}


void displayConnectionStatus() { //MARK: Display connection status
    static unsigned long lastDebugPrint = 0;
    numConnections = 0;

    // Clear the entire connection status line first
    lcd.setCursor(0, 1);
    lcd.print("                    "); // Clear 20 characters
    
    // Redraw the connection status labels and indicators
    lcd.setCursor(0, 1);
    lcd.print("S1:");
    lcd.setCursor(5, 1);
    lcd.print("S2:");
    lcd.setCursor(10, 1);
    lcd.print("S3:");
    lcd.setCursor(15, 1);
    lcd.print("S4:");
    
    bool connections[4];
    
    lcd.setCursor(3, 1);
    connections[0] = checkConnection(1);
    if (connections[0]) {
        lcd.write(byte(0)); // Tick mark
        numConnections++;
    } else {
        lcd.print("x");
    }

    lcd.setCursor(8, 1);
    connections[1] = checkConnection(2);
    if (connections[1]) {
        lcd.write(byte(0)); // Tick mark
        numConnections++;
    } else {
        lcd.print("x");
    }

    lcd.setCursor(13, 1);
    connections[2] = checkConnection(3);
    if (connections[2]) {
        lcd.write(byte(0)); // Tick mark
        numConnections++;
    } else {
        lcd.print("x");
    }

    lcd.setCursor(18, 1);
    connections[3] = checkConnection(4);
    if (connections[3]) {
        lcd.write(byte(0)); // Tick mark
        numConnections++;
    } else {
        lcd.print("x");
    }
    
    // Debug: Print connection status every 10 seconds
    if (millis() - lastDebugPrint > 10000) {
        lastDebugPrint = millis();
        Serial.printf("Connection Status: S1=%s S2=%s S3=%s S4=%s (Total: %d)\n",
                     connections[0] ? "OK" : "X", connections[1] ? "OK" : "X",
                     connections[2] ? "OK" : "X", connections[3] ? "OK" : "X", numConnections);
    }
}


bool connectToWiFi() {
    Serial.println("WiFi Connection:\t\t\tAttempting...");
    lcd.setCursor(0, 0);
    lcd.print("Connecting to WiFi...");
    
    // Feed watchdog before starting WiFi connection
    esp_task_wdt_reset();
    
    // Ensure WiFi is disconnected first
    WiFi.disconnect(true);
    delay(100);
    
    WiFi.mode(WIFI_AP_STA); // Set to both AP and STA mode for ESP-NOW compatibility
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    const int maxAttempts = 20; // 10 seconds timeout
    
    while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
        delay(500);
        Serial.print(".");
        attempts++;
        
        // Feed watchdog during WiFi connection to prevent timeout
        esp_task_wdt_reset();
        
        // Show progress on LCD
        if (attempts % 4 == 0) {
            lcd.setCursor(19, 0);
            lcd.print(attempts / 4);
        }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print("WiFi Connection:\t\t\tSuccess (");
        Serial.print(WiFi.localIP());
        Serial.println(")");
        lcd.setCursor(0, 0);
        lcd.print("WiFi Connected      ");
        return true;
    } else {
        Serial.println();
        Serial.printf("WiFi Connection:\t\t\tFailed (Status: %d)\n", WiFi.status());
        lcd.setCursor(0, 0);
        lcd.print("WiFi Failed         ");
        return false;
    }
}


bool syncTimeWithNTP() {
    Serial.println("NTP Time Sync:\t\t\t\tAttempting...");
    lcd.setCursor(0, 1);
    lcd.print("Syncing time...");
    
    // Feed watchdog before starting NTP sync
    esp_task_wdt_reset();
    
    // Detailed network diagnostics
    Serial.printf("WiFi Status: %d (Connected=%d)\n", WiFi.status(), WL_CONNECTED);
    Serial.printf("WiFi IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("WiFi Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("WiFi DNS: %s\n", WiFi.dnsIP().toString().c_str());
    
    // Test basic connectivity first
    Serial.println("Testing basic connectivity...");
    WiFiClient client;
    if (client.connect("8.8.8.8", 53)) {  // Google DNS on port 53
        Serial.println("Basic TCP connectivity: OK");
        client.stop();
    } else {
        Serial.println("Basic TCP connectivity: FAILED");
        Serial.println("Network issue - cannot reach external servers");
        return false;
    }
    
    // Test DNS resolution
    Serial.println("Testing DNS resolution...");
    IPAddress ntpIP;
    if (WiFi.hostByName(ntpServer, ntpIP)) {
        Serial.printf("DNS Resolution: OK (%s -> %s)\n", ntpServer, ntpIP.toString().c_str());
    } else {
        Serial.printf("DNS Resolution: FAILED (cannot resolve %s)\n", ntpServer);
        Serial.println("Trying alternate NTP servers...");
        
        // Try backup NTP servers
        const char* backupServers[] = {"pool.ntp.org", "time.nist.gov", "time.cloudflare.com"};
        bool dnsWorking = false;
        
        for (int i = 0; i < 3; i++) {
            if (WiFi.hostByName(backupServers[i], ntpIP)) {
                Serial.printf("Backup DNS OK: %s -> %s\n", backupServers[i], ntpIP.toString().c_str());
                dnsWorking = true;
                break;
            }
        }
        
        if (!dnsWorking) {
            Serial.println("All DNS lookups failed - DNS issue detected");
            return false;
        }
    }
    
    // Configure time with NTP server
    Serial.printf("Configuring NTP with server: %s\n", ntpServer);
    Serial.printf("Timezone: GMT%+ld, DST: %d hours\n", gmtOffset_sec/3600, daylightOffset_sec/3600);
    
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    // Wait for time to be set with detailed progress
    struct tm timeinfo;
    int attempts = 0;
    const int maxAttempts = 8; // 4 seconds timeout
    
    Serial.println("Waiting for NTP response...");
    while (!getLocalTime(&timeinfo) && attempts < maxAttempts) {
        delay(500);
        attempts++;
        
        // Feed watchdog during NTP sync to prevent timeout
        esp_task_wdt_reset();
        
        // Show detailed progress
        if (attempts % 2 == 0) {
            lcd.setCursor(18, 1);
            lcd.print(attempts / 2);
            Serial.printf("NTP attempt %d/%d (%.1fs elapsed)...\n", attempts, maxAttempts, attempts * 0.5);
        }
        
        // Check if we're getting partial time data
        time_t now;
        time(&now);
        if (now > 0) {
            Serial.printf("Partial time received: %ld (but getLocalTime still fails)\n", now);
        }
    }
    
    Serial.printf("NTP sync completed after %d attempts (%.1fs)\n", attempts, attempts * 0.5);
    
    if (getLocalTime(&timeinfo)) {
        Serial.printf("Raw time received - Year: %d, Month: %d, Day: %d\n", 
                     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
        Serial.printf("Time: %02d:%02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        
        // Validate the received time (should be reasonable)
        if (timeinfo.tm_year > (2020 - 1900) && timeinfo.tm_year < (2050 - 1900)) {
            // Update RTC with NTP time
            DateTime ntpTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            rtc.adjust(ntpTime);
            
            // Update tracking variables
            lastNTPSync = millis();
            ntpSyncSuccessful = true;
            
            Serial.println("NTP Time Sync:\t\t\t\tSuccess");
            Serial.printf("Time updated to: %04d-%02d-%02d %02d:%02d:%02d\n",
                         timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                         timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            
            lcd.setCursor(0, 1);
            lcd.print("Time synced         ");
            // Clear any leftover NTP progress numbers from the display
            lcd.setCursor(18, 1);
            lcd.print(" ");
            return true;
        } else {
            Serial.printf("NTP Time Sync:\t\t\t\tFailed (Invalid time - year %d)\n", timeinfo.tm_year + 1900);
            lcd.setCursor(0, 1);
            lcd.print("Time invalid        ");
            // Clear any leftover NTP progress numbers from the display
            lcd.setCursor(18, 1);
            lcd.print(" ");
            return false;
        }
    } else {
        Serial.println("NTP Time Sync:\t\t\t\tFailed (No response from NTP server)");
        Serial.println("Possible causes:");
        Serial.println("1. NTP port 123 blocked by firewall/router");
        Serial.println("2. ISP blocking NTP traffic");
        Serial.println("3. Network congestion");
        Serial.println("4. NTP server overloaded");
        
        // Try one more time with a different approach
        Serial.println("Trying manual NTP query...");
        // You could implement manual NTP packet query here if needed
        
        lcd.setCursor(0, 1);
        lcd.print("NTP blocked/timeout ");
        // Clear any leftover NTP progress numbers from the display
        lcd.setCursor(18, 1);
        lcd.print(" ");
        return false;
    }
}


bool isRTCTimeValid() {
    DateTime now = rtc.now();
    
    // Check if RTC time is reasonable (not reset to default values)
    if (now.year() < 2020 || now.year() > 2050) {
        Serial.printf("RTC Invalid: Year %d out of range\n", now.year());
        return false;
    }
    
    // Check if RTC has lost time (typical sign of dead battery)
    // If it's been more than 48 hours since last NTP sync and RTC shows a time
    // that's way off from when we expect it to be, RTC might be dead
    if (ntpSyncSuccessful && lastNTPSync > 0) {
        unsigned long expectedElapsed = (millis() - lastNTPSync) / 1000; // seconds
        // Allow for some drift, but if RTC is more than 10 minutes off, it's likely dead
        if (expectedElapsed > 600) { // More than 10 minutes
            Serial.println("RTC potentially lost time - battery may be dead");
            return false;
        }
    }
    
    return true;
}


bool attemptNTPSync() {
    // Don't attempt if we just tried recently (unless it's startup)
    if (lastNTPSync > 0 && (millis() - lastNTPSync) < NTP_RETRY_INTERVAL) {
        return false; // Too soon to retry
    }
    
    Serial.println("Attempting background NTP sync...");
    
    // Set WiFi mode and connect
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    const int maxWiFiAttempts = 10; // 5 seconds max for WiFi
    
    while (WiFi.status() != WL_CONNECTED && attempts < maxWiFiAttempts) {
        delay(500);
        attempts++;
        esp_task_wdt_reset();
    }
    
    bool syncSuccess = false;
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi connected for NTP sync");
        syncSuccess = syncTimeWithNTP();
        WiFi.disconnect();
    } else {
        Serial.println("WiFi connection failed for NTP sync");
    }
    
    // Reset WiFi mode for ESP-NOW compatibility
    WiFi.mode(WIFI_STA);
    delay(100);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    
    return syncSuccess;
}


void manageTimeSync() {
    unsigned long currentTime = millis();
    
    // Check RTC validity periodically
    if (currentTime - lastRTCCheck > RTC_VALIDITY_CHECK) {
        lastRTCCheck = currentTime;
        
        if (!isRTCTimeValid()) {
            Serial.println("RTC time invalid - attempting emergency NTP sync");
            // Force immediate NTP sync attempt
            lastNTPSync = 0; // Reset to force sync
            attemptNTPSync();
        }
    }
    
    // Periodic NTP sync attempt (every hour)
    if (currentTime - lastNTPSync > NTP_RETRY_INTERVAL) {
        Serial.println("Scheduled NTP sync attempt");
        attemptNTPSync();
    }
}


void updateSystemTimeFromRTC() {
    Serial.println("System Time Update:\t\t\tAttempting...");
    
    DateTime now = rtc.now();
    
    // Validate RTC time before using it
    if (now.year() < 2020 || now.year() > 2050) {
        Serial.println("System Time Update:\t\t\tFailed (Invalid RTC time)");
        Serial.printf("RTC shows invalid year: %d\n", now.year());
        return;
    }
    
    struct tm timeinfo;
    timeinfo.tm_year = now.year() - 1900;  // tm_year is years since 1900
    timeinfo.tm_mon = now.month() - 1;     // tm_mon is 0-11
    timeinfo.tm_mday = now.day();
    timeinfo.tm_hour = now.hour();
    timeinfo.tm_min = now.minute();
    timeinfo.tm_sec = now.second();
    timeinfo.tm_isdst = -1; // Let the system determine DST
    
    time_t rtcTime = mktime(&timeinfo);
    if (rtcTime == -1) {
        Serial.println("System Time Update:\t\t\tFailed (Invalid time structure)");
        return;
    }
    
    struct timeval tv = { .tv_sec = rtcTime, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) == 0) {
        Serial.println("System Time Update:\t\t\tSuccess");
        Serial.printf("System time set to: %04d-%02d-%02d %02d:%02d:%02d\n",
                     now.year(), now.month(), now.day(),
                     now.hour(), now.minute(), now.second());
    } else {
        Serial.println("System Time Update:\t\t\tFailed (settimeofday error)");
    }
}


void setup() {  //MARK: Setup
    Serial.begin(115200);

    // Initialize watchdog timer (30 seconds timeout)
    esp_task_wdt_init(30, true);
    esp_task_wdt_add(NULL);

    Serial.println("\n\n\nSELF CHECK:\n");


    //------------------ BUTTON - INIT - BEGIN ------------------
    pinMode(BUTTON_PIN, INPUT_PULLUP); // Enable internal pull-up resistor
    Serial.printf("Button initialized on pin %d with pull-up\n", BUTTON_PIN);
    //------------------ BUTTON - INIT - END ------------------

    //------------------ NEOPIXEL - INIT - BEGIN ------------------
    strip.begin(); // Initialize the NeoPixel library
    strip.show();  // Initialize all pixels to 'off'
    //------------------ NEOPIXEL - INIT - END ------------------
    updateStatusLED(1);

    //------------------ LCD - INIT - BEGIN ------------------
    lcd.init();                     // initialize the lcd
    lcd.backlight();                // Turn on the backlight

    byte tickMark[8] = {            // Custom character for tick mark
        B00000,
        B00000,
        B00001,
        B00011,
        B10110,
        B11100,
        B01000,
        B00000
        };
    lcd.createChar(0, tickMark);    // Create a new custom character

    lcd.setCursor(8, 0);            // set cursor to first column, first row
    lcd.print("Boot...");        // print message
    //------------------ LCD - INIT - END ------------------

    //------------------ ESP-NNOW -INIT - BEGIN ------------------
    // Set WiFi mode for ESP-NOW (will be temporarily changed for WiFi connection later)
    WiFi.mode(WIFI_STA);

    while (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW Initialization:\t\t\tFailed");
        displayError("Error init ESP-NOW", 6);
        updateStatusLED(4);
        delay(3000);
    }
    Serial.println("ESP-NOW Initialization:\t\t\tSuccess");

    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    for (int i = 0; i < 4; i++) {
        memcpy(peerInfo[i].peer_addr, broadcastAddresses[i], 6);
        peerInfo[i].channel = 0;  
        peerInfo[i].encrypt = false;
        peerInfo[i].ifidx = WIFI_IF_STA;  // Set the interface to STA
        
        if (esp_now_add_peer(&peerInfo[i]) != ESP_OK){
            Serial.println("ESP-NOW Peer Addition (Target " + String(i+1) + "):\tFailed");
            displayError("Failed to add peer", 5);
            updateStatusLED(4);
            return;
        }else{
            Serial.print("ESP-NOW Peer Addition (Target " + String(i+1) + "):\tSuccess\n");
        }
    }
    //------------------ ESP-NNOW -INIT - END ------------------

    //------------------ SD CARD - INIT - BEGIN ------------------
    while(!SD.begin(CS_PIN)){
        Serial.println("SD Card Mount Failed");
        displayError("SD Card Mount Failed", 4);
        updateStatusLED(5);
    }

    uint8_t cardType = SD.cardType();

    while(cardType == CARD_NONE){
        updateStatusLED(5);
        Serial.println("SD Card Mount:\t\t\t\tFailed");
        displayError("SD Card Mount Failed", 2);
    }
    Serial.println("SD Card Mount:\t\t\t\tSuccess");

    strncpy(fileName, "/data_master.csv", sizeof(fileName));
    File file = SD.open(fileName, FILE_APPEND);

    if (!file) {
        Serial.println("Writing to file:\t\t\tFailed");
        updateStatusLED(5);
        displayError("Failed to open file", 2);
    } else {
        // Add header if file is empty
        if (file.size() == 0) {
            file.println("timestamp,target_no,sensor_no,temperature");
        }
        Serial.println("Writing to file:\t\t\tSuccess");
        file.close();
    }

    //------------------ SD CARD - INIT - END ------------------

    //------------------ RTC - INIT - BEGIN ------------------
  if (! rtc.begin()) {
    Serial.println("Init RTC:\t\t\t\tFailed");
    updateStatusLED(4);
    while (true){}
    } else {
      Serial.print("Init RTC:\t\t\t\tSuccess (");
      Serial.print(get_timestamp());
      Serial.println(")"); 
  }
    //------------------ RTC - INIT - END ------------------

    //------------------ WIFI & NTP TIME SYNC - BEGIN ------------------
    // Feed watchdog before starting WiFi/NTP operations
    esp_task_wdt_reset();
    Serial.println("Starting WiFi/NTP initialization...");
    
    // Initialize time management tracking
    lastNTPSync = 0;
    lastRTCCheck = millis();
    ntpSyncSuccessful = false;
    
    // Attempt WiFi connection and NTP time sync
    Serial.println("Attempting WiFi/NTP time synchronization...");
    if (connectToWiFi()) {
        if (syncTimeWithNTP()) {
            Serial.println("WiFi and NTP setup completed successfully");
        } else {
            Serial.println("WiFi connected but NTP sync failed - continuing with RTC time");
        }
        // Disconnect WiFi to free resources for ESP-NOW
        WiFi.disconnect();
        delay(100);
    } else {
        Serial.println("WiFi connection failed, continuing without NTP sync - using RTC time only");
    }
    
    // Reset WiFi mode for ESP-NOW compatibility
    WiFi.mode(WIFI_STA);
    delay(100);
    
    // Set WiFi channel to 1 for ESP-NOW consistency
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    
    // Reinitialize ESP-NOW after WiFi operations
    esp_now_deinit();
    delay(100);
    while (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW Reinitialization:\t\t\tFailed");
        delay(1000);
    }
    Serial.println("ESP-NOW Reinitialization:\t\t\tSuccess");
    
    // Re-register the callback and peers
    esp_now_register_recv_cb(OnDataRecv);
    for (int i = 0; i < 4; i++) {
        esp_now_peer_info_t peerInfo;
        memcpy(peerInfo.peer_addr, broadcastAddresses[i], 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        peerInfo.ifidx = WIFI_IF_STA;  // Set the interface to STA
        
        if (esp_now_add_peer(&peerInfo) != ESP_OK) {
            Serial.printf("ESP-NOW Peer Re-addition (Target %d):\t\tFailed\n", i + 1);
        } else {
            Serial.printf("ESP-NOW Peer Re-addition (Target %d):\t\tSuccess\n", i + 1);
        }
    }
    
    // Update system time from RTC regardless of WiFi/NTP success
    updateSystemTimeFromRTC();
    //------------------ WIFI & NTP TIME SYNC - END ------------------

    lcd.clear();
    lcd.setCursor(4, 0);            // set cursor to first column, first row
    lcd.print("Connecting...");     // print message

    Serial.println("\nSELF-CHECK COMPLET\n\n\n");
    
    updateStatusLED(0);
}


void loop() {
    // Feed the watchdog timer
    esp_task_wdt_reset();

    // Background time management (check for RTC validity and periodic NTP sync)
    manageTimeSync();

    static unsigned long previousTempUpdate = tempUpdateIntervall;
    unsigned long currentTempUpdate = millis();
    if (currentTempUpdate - previousTempUpdate >= tempUpdateIntervall && logState == false) {   // Update displayed temperature every 10 seconds
        previousTempUpdate = currentTempUpdate;
        getAllTemps(false);
    }
    
    displayTimeStamp();

    static unsigned long previousConnectStat = pingCheckIntervall;
    unsigned long currentConnectStat = millis();
    if (currentConnectStat - previousConnectStat >= pingCheckIntervall) {   // Update the connection status only every second, to avoid callback issues
        previousConnectStat = currentConnectStat;
        displayConnectionStatus();
        sendLogState(logState);
    }

    // Only enter error loop if we have no connections AND we're not actively logging
    // This prevents logging mode from being interrupted by temporary connection issues
    while(numConnections == 0 && !logState){
        // Feed the watchdog timer in the error loop
        esp_task_wdt_reset();
        
        lcd.setCursor(0, 3);
        lcd.print("ERROR: No connection");
        displayConnectionStatus();
        updateStatusLED(5);
        displayTimeStamp();
        
        // Small delay to prevent tight loop
        delay(100);
        
        // Check connections again to see if we can exit the error loop
        displayConnectionStatus();
        if (numConnections > 0) {
            break; // Exit the error loop when any connection is established
        }
    }

    buttonState();

    if(logState){
        // Allow logging with any number of connected servants (even just 1)
        static unsigned long lastLEDDebug = 0;
        if (millis() - lastLEDDebug > 5000) { // Debug every 5 seconds
            lastLEDDebug = millis();
            if (numConnections > 0) {
                if (numConnections >= 3) {
                    Serial.println("LED: Constant Green (3+ servants logging)");
                } else {
                    Serial.println("LED: Constant Yellow (1-2 servants logging)");
                }
            } else {
                Serial.println("LED: Blink Yellow (no connections but logging active)");
            }
        }
        
        if (numConnections > 0) {
            if (numConnections >= 3) {
                updateStatusLED(3); // Constant green - 3 or more servants logging
            } else {
                updateStatusLED(1); // Constant yellow - fewer than 3 servants but still logging
            }
            logLoop(); // logLoop handles its own display messages
        } else {
            // No connections but logging is active - keep trying
            updateStatusLED(6); // Blink yellow - no connections but logging active
            // Still call logLoop to maintain timing, but it will skip data collection
            logLoop(); // logLoop will display "Logging: No connection"
        }

    }else{
        // Not logging - show idle status
        if (numConnections >= 3){
            updateStatusLED(2); // Blink green - ready with 3+ servants
        } else if (numConnections > 0) {
            updateStatusLED(6); // Blink yellow - ready with fewer servants
        } else {
            updateStatusLED(5); // Blink red - no connections
        }

        lcd.setCursor(0, 3);
        lcd.print("Idle (ready to log) ");
    }
}
