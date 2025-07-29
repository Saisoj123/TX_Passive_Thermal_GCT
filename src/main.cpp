/*
 * TX Passive Thermal GCT - Master ESP32
 * 
 * Original code base: https://github.com/MoritzNelle/TX_Passive_Thermal_GCT
 * Enhanced by Josias Kern using GitHub Copilot (GPT-4)
 * 
 * Enhancements include:
 * - WiFi connectivity and NTP time synchronization
 * - Watchdog timer for system reliability
 * - Improved error handling and recovery
 * - Robust SD card and RTC operations
 */

// Bugs to fix
// TODO: occasionally the loging timer goes to 42947XXX seconds
// TODO: button press is not always detected
// X TODO: temperature is not displayed for all servents at once
// X TODO: temperature values from offline-targets are replaced with the value of the last online-target on the display
// X TODO: temperature values from offline-targets are replaced with the value of the last online-target in the log file
// X TODO: display does not reset propperly after a fatal error-display
// TODO: when connection is lost, the display does still show the last temperature values
// TODO: temperture values are not compleatly over written on the display (could be a problem, when the number of digits reduces)
// TODO: display and LED freezes during the temperature request (display "Updating Temperature")
// TODO: status LED and ERROR message accasionally (and only very briefly) display "No connection" even if the connection is established



#include <esp_now.h>
#include <WiFi.h>
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

//User variables
int sendTimeout         = 1000;     //Timeout for waiting for a servent response data in ms
int logIntervall        = 10000;    //Log intervall in ms (>= 10000 ms = 10s)
int pingCheckIntervall  = 1000;     //Ping check intervall in ms
int tempUpdateIntervall = 10000;    //Temperature update intervall in ms

// WiFi and NTP configuration
const char* ssid = "VodafoneMobileWiFi-A8E1";        // Replace with your WiFi network name
const char* password = "I5IJ4ij4"; // Replace with your WiFi password
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;            // GMT+1 for Amsterdam (CET)
const int daylightOffset_sec = 3600;        // +1 hour for summer time (CEST)

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
    {0x48, 0xE7, 0x29, 0x8C, 0x79, 0x68},
    {0x48, 0xE7, 0x29, 0x8C, 0x73, 0x18},
    {0x4C, 0x11, 0xAE, 0x65, 0xBD, 0x54},
    {0x48, 0xE7, 0x29, 0x8C, 0x72, 0x50}    
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
    const unsigned long debounceDelay = 50;
    
    bool currentButtonState = digitalRead(BUTTON_PIN);
    
    // Check if button state changed
    if (currentButtonState != lastButtonState) {
        lastButtonPress = millis();
    }
    
    // If enough time has passed since last state change
    if ((millis() - lastButtonPress) > debounceDelay) {
        // If button is pressed (LOW) and wasn't pressed before
        if (currentButtonState == LOW && lastButtonState == HIGH) {
            logState = !logState;
            sendLogState(logState);
            if (logState) {
                timeLeft = 0;
            }
            Serial.printf("Button pressed - Log state: %s\n", logState ? "ON" : "OFF");
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
    memcpy(&receivedData, incomingData, sizeof(receivedData));
    receivedActionID = receivedData.actionID;
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
    esp_err_t result;
    
    // structure to Action Code as a connection test
    typedef struct struct_message {
        int actionID;
    } struct_message;
    struct_message testData;

    testData.actionID = 1001;

    // Don't re-register callback - it's already registered in setup()
    result = esp_now_send(broadcastAddresses[locTargetID-1], (uint8_t *) &testData, sizeof(testData));

    if (result == ESP_OK) {     // Check if the message was queued for sending successfully
        delay(40);              // NEEDED: Delay to allow the send callback to be called
        if (lastSendStatus == ESP_OK) {        // Check the send status
            return true;
        } else {
            return false;
        }
    } else {
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
        return (true);
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


void displayTemp(int targetID, temp t) { //MARK: Display temperature

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

    if (checkConnection(targetID)) {
        float avgTemp = (t.sens1 + t.sens2 + t.sens3 + t.sens4 + t.sens5 + t.sens6 + t.sens7 + t.sens8 + t.sens9) / 9;

        lcd.printf("%.1f", avgTemp);
    }else{
        lcd.print("  -  ");
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
        Serial.printf("Successfully wrote %d bytes to SD card\n", bytesWritten);
    }
}


void getAllTemps(bool save = true) {//MARK: Get temperatures

    updateStatusLED(0);
    lcd.setCursor(0, 3);
    lcd.print("Updating Temperature");

    TXdata.actionID = 3001; //Action ID for getting all temperatures from a servent

    for (int i = 0; i < 4; i++) {
        esp_err_t result = esp_now_send(broadcastAddresses[i], (uint8_t *) &TXdata, sizeof(TXdata));

        if (waitForActionID(2001,i+1/*Target ID*/) == true){
                
            if (save == true)
            {
                if (checkConnection(i+1)) {
                    writeToSD(tempToString(receivedData,get_timestamp(), i+1));
                }else{
                    writeToSD(String(get_timestamp()) + "," + String(i+1) + ",123456789,NAN\n");
                }
            }
            displayTemp(i+1, receivedData);
        }
    }
}


void logLoop() {    //MARK: Log loop
    static unsigned long previousExecution = logIntervall;
    unsigned long currentTime = millis();

    if (timeLeft == 0) {   // Update the connection status only every second, to avoid callback issues
        previousExecution = currentTime;
        Serial.println("Retrieving Data... ");
        lcd.setCursor(0, 3);
        lcd.print("Retrieving Data...  ");
        getAllTemps();
    }else{
        lcd.setCursor(0, 3);
        lcd.print("Logging:");
        lcd.setCursor(8, 3);
        lcd.printf(" %.d s        ",timeLeft);
    }

    timeLeft = ((logIntervall)-(currentTime - (previousExecution + 1000)))/1000;
    
    // Serial.println(timeLeft);
    // Serial.println(currentTime);
    // Serial.println(previousExecution);
}


void displayConnectionStatus() { //MARK: Display connection status
    numConnections = 0;

    lcd.setCursor(0, 1);
    lcd.print("S1:");
    lcd.setCursor(5, 1);
    lcd.print("S2:");
    lcd.setCursor(10, 1);
    lcd.print("S3:");
    lcd.setCursor(15, 1);
    lcd.print("S4:");
    
    lcd.setCursor(3, 1);
    if (checkConnection(1)) {
        lcd.write(byte(0)); // Tick mark
        numConnections++;
    } else {
        lcd.print("x");
    }

    lcd.setCursor(8, 1);
    if (checkConnection(2)) {
        lcd.write(byte(0)); // Tick mark
        numConnections++;
    } else {
        lcd.print("x");
    }

    lcd.setCursor(13, 1);
    if (checkConnection(3)) {
        lcd.write(byte(0)); // Tick mark
        numConnections++;
    } else {
        lcd.print("x");
    }

    lcd.setCursor(18, 1);
    if (checkConnection(4)) {
        lcd.write(byte(0)); // Tick mark
        numConnections++;
    } else {
        lcd.print("x");
    }
}


bool connectToWiFi() {
    Serial.println("WiFi Connection:\t\t\tAttempting...");
    lcd.setCursor(0, 0);
    lcd.print("Connecting to WiFi...");
    
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
    
    // Configure time with NTP server
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    // Wait for time to be set
    struct tm timeinfo;
    int attempts = 0;
    const int maxAttempts = 10; // 5 seconds timeout
    
    while (!getLocalTime(&timeinfo) && attempts < maxAttempts) {
        delay(500);
        attempts++;
        
        // Show progress
        if (attempts % 2 == 0) {
            lcd.setCursor(18, 1);
            lcd.print(attempts / 2);
        }
    }
    
    if (getLocalTime(&timeinfo)) {
        // Validate the received time (should be reasonable)
        if (timeinfo.tm_year > (2020 - 1900) && timeinfo.tm_year < (2050 - 1900)) {
            // Update RTC with NTP time
            DateTime ntpTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            rtc.adjust(ntpTime);
            
            Serial.println("NTP Time Sync:\t\t\t\tSuccess");
            Serial.printf("Time updated to: %04d-%02d-%02d %02d:%02d:%02d\n",
                         timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                         timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            
            lcd.setCursor(0, 1);
            lcd.print("Time synced         ");
            return true;
        } else {
            Serial.println("NTP Time Sync:\t\t\t\tFailed (Invalid time received)");
            lcd.setCursor(0, 1);
            lcd.print("Time invalid        ");
            return false;
        }
    } else {
        Serial.println("NTP Time Sync:\t\t\t\tFailed (Timeout)");
        lcd.setCursor(0, 1);
        lcd.print("Time sync failed    ");
        return false;
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
    pinMode(BUTTON_PIN, INPUT);
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
    // Attempt WiFi connection and NTP time sync
    if (connectToWiFi()) {
        if (syncTimeWithNTP()) {
            Serial.println("WiFi and NTP setup completed successfully");
        } else {
            Serial.println("WiFi connected but NTP sync failed");
        }
        // Disconnect WiFi to free resources for ESP-NOW
        WiFi.disconnect();
        delay(100);
    } else {
        Serial.println("WiFi connection failed, continuing without NTP sync");
    }
    
    // Reset WiFi mode for ESP-NOW compatibility
    WiFi.mode(WIFI_STA);
    delay(100);
    
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

    while(numConnections == 0){
        lcd.setCursor(0, 3);
        lcd.print("ERROR: No connection");
        displayConnectionStatus();
        updateStatusLED(5);
        displayTimeStamp();
        logState = false;
    }

    buttonState();

    if(logState){
        if (numConnections == 4){
            updateStatusLED(3); // Constant green
            logLoop();

        }else{
            updateStatusLED(1); // Constant yellow
            logLoop();
        }

    }else{
        if (numConnections == 4){
            updateStatusLED(2); // Blink green
        }else{
            updateStatusLED(6); // Blink yellow
        }

        lcd.setCursor(0, 3);
        lcd.print("Idle (ready to log) ");
    }
}
