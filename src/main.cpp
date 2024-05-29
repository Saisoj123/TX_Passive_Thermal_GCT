#include <esp_now.h>
#include <WiFi.h>
#include <RTClib.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_NeoPixel.h>

//User variables
int sendTimeout     = 1000;     //Timeout for waiting for a servent response data in ms
int logIntervall    = 10000;    //Log intervall in ms

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
temp tempData;

// system variables
volatile bool messageReceived = false;
volatile int receivedActionID = 0;
temp receivedData;
char timestamp[19];
#define CS_PIN 5
#define LED_PIN 2
File file;
char fileName[24];
bool conectionStatus = false;
#define BUTTON_PIN 0
bool logState = false;
esp_err_t lastSendStatus = ESP_FAIL;
int numConnections = 0;

Adafruit_NeoPixel strip(1, LED_PIN, NEO_GRB + NEO_KHZ800);  // Create an instance of the Adafruit_NeoPixel class

uint8_t broadcastAddresses[][6] = {
    {0x48, 0xE7, 0x29, 0x8C, 0x78, 0x30},
    {0x48, 0xE7, 0x29, 0x29, 0x79, 0x68},
    {0x48, 0xE7, 0x29, 0x8C, 0x6B, 0x5C},
    {0x48, 0xE7, 0x29, 0x8C, 0x72, 0x50}
};
esp_now_peer_info_t peerInfo[4];

RTC_DS3231 rtc;

LiquidCrystal_I2C lcd(0x27, 20, 4); // set the LCD address to 0x27 for a 20 chars and 4 line display

bool buttonState(){ //MARK: Button state
    if (digitalRead(BUTTON_PIN) == LOW){
        logState = !logState;
        while(digitalRead(BUTTON_PIN) == LOW){
            // Wait for button release
        }
    }
    return logState;
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("\r\nLast Packet Send Status:\t");
    if (status == ESP_NOW_SEND_SUCCESS) {
        Serial.println("Delivery Success");
        conectionStatus = true;
    } else {
        Serial.println("Delivery Fail");
        conectionStatus = false;
    }
    lastSendStatus = status == ESP_NOW_SEND_SUCCESS ? ESP_OK : ESP_FAIL;

    //Serial.print(conectionStatus);
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

bool waitForActionID(int actionID, int targetID) { //MARK: Wait for action ID
    unsigned long startTime = millis();
    bool conectionStatus = false;

    while (!messageReceived || receivedActionID != actionID) {
        if (millis() - startTime > sendTimeout) {
            Serial.println("Timeout waiting for action ID");
            conectionStatus = false;
            break;
        }else{
            conectionStatus = true;
        }
    }
    messageReceived = false; // Reset for next message

    //updateConnectionStatus(conectionStatus, targetID);
    return (conectionStatus);

}


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

void writeToSD(String dataString) { //MARK: Write to SD
    //Serial.print (dataString);
    file = SD.open(fileName, FILE_APPEND); // Open the file in append mode
    file.print(dataString);
    file.close();
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
    
    float avgTemp = (t.sens1 + t.sens2 + t.sens3 + t.sens4 + t.sens5 + t.sens6 + t.sens7 + t.sens8 + t.sens9) / 9;
    //Serial.println("Taget ID: " + String(targetID) + "\tAvg Temp: " + String(avgTemp) + "°C");
    lcd.printf("%.1f", avgTemp);
}
    

void getAllTemps() {//MARK: Get temperatures
    TXdata.actionID = 3001; //Action ID for getting all temperatures from a servent

    for (int i = 0; i < 4; i++) {
        esp_err_t result = esp_now_send(broadcastAddresses[i], (uint8_t *) &TXdata, sizeof(TXdata));
        
        if (waitForActionID(2001,i+1/*Target ID*/) == true){
            writeToSD(tempToString(receivedData,get_timestamp(), i+1));
            displayTemp(i+1, receivedData);
        }
    }
}


void blinkLED(int red, int green, int blue) {
    static unsigned long previousMillis = 0;
    static bool ledState = false;
    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= 1000) {
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


void updateStatusLED(int status){ //MARK: Update status LED
    switch (status)
    {

    case 0:
        strip.setPixelColor(0, strip.Color(0, 0, 0)); // Turn off the LED
        break;
    case 1:
        strip.setPixelColor(0, strip.Color(255, 100, 0));// Set the LED to yellow
        break;
    
    case 2:
        blinkLED(0, 255, 0); // Blink the LED in green
        break;
    
    case 3:
        strip.setPixelColor(0, strip.Color(0, 255, 0)); // Set the LED to green
        break;

    case 4:
        strip.setPixelColor(0, strip.Color(255, 0, 0)); // Set the LED to red
        break;

    case 5:
        blinkLED(255, 0, 0); // Blink the LED in red
        break;

    case 6:
        blinkLED(255, 100, 0); // Blink the LED in yellow
        break;
    
    default:
        break;
    }

    strip.show();
}


void logLoop() {    //MARK: Log loop
    static unsigned long previousExecution = 10000;
    unsigned long currentTime = millis();
    int timeLeft = ((logIntervall)-(currentTime - previousExecution))/1000;

    if (currentTime - previousExecution >= logIntervall) {   // Update the connection status only every second, to avoid callback issues
        previousExecution = currentTime;
        Serial.println("Logging...");
        lcd.setCursor(0, 3);
        lcd.print("Retrieving Data...  ");
        getAllTemps();
    }else{
        lcd.setCursor(0, 3);
        lcd.print("Logging:");
        lcd.setCursor(8, 3);
        lcd.printf(" %.d s        ",timeLeft+1);
    }
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


bool checkConnection(int locTargetID) { //MARK: Check connection
    esp_err_t result;
    uint8_t testData[1] = {0}; // Test data to send

    esp_now_register_send_cb(OnDataSent);    // Register send callback
    result = esp_now_send(broadcastAddresses[locTargetID-1], testData, sizeof(testData));

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

void displayConectionStatus() { //MARK: Display connection status
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


void setup() {  //MARK: Setup
    Serial.begin(115200);

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

    lcd.setCursor(5, 1);            // set cursor to first column, first row
    lcd.print("Booting...");        // print message
    //------------------ LCD - INIT - END ------------------

    //------------------ ESP-NNOW -INIT - BEGIN ------------------
    WiFi.mode(WIFI_STA);

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        displayError("Error init ESP-NOW", 6);
        updateStatusLED(4);
        return;
    }

    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    for (int i = 0; i < 4; i++) {
        memcpy(peerInfo[i].peer_addr, broadcastAddresses[i], 6);
        peerInfo[i].channel = 0;  
        peerInfo[i].encrypt = false;
        
        if (esp_now_add_peer(&peerInfo[i]) != ESP_OK){
            Serial.println("Failed to add peer");
            displayError("Failed to add peer", 5);
            updateStatusLED(4);
            return;
        }
    }
    //------------------ ESP-NNOW -INIT - END ------------------

    //------------------ SD CARD - INIT - BEGIN ------------------
    if(!SD.begin(CS_PIN)){
        Serial.println("Card Mount Failed");
        displayError("Card Mount Failed", 4);
        updateStatusLED(4);
        return;
    }
    uint8_t cardType = SD.cardType();
    if(cardType == CARD_NONE){
        Serial.println("No SD card attached");
        displayError("No SD card attached", 3);
        updateStatusLED(4);
        return;
    }

    strncpy(fileName, "/data_master.csv", sizeof(fileName));
    File file = SD.open(fileName, FILE_WRITE);
    
    if(!file){
        Serial.println("Failed to open file for writing");
        displayError("Failed to open file", 2);
        updateStatusLED(4);
        return;
    }

    //------------------ SD CARD - INIT - END ------------------

    //------------------ RTC - INIT - BEGIN ------------------
    if (! rtc.begin()) {
        updateStatusLED(4);
        displayError("Couldn't find RTC", 1);
        Serial.println("Couldn't find RTC");
        while (1);
    }
    //rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); //uncomment to set the RTC to the compile time
    //------------------ RTC - INIT - END ------------------
    lcd.clear();
    updateStatusLED(0);
    Serial.println("Setup completed successfully!");
}


void loop() {
    displayTimeStamp();

    static unsigned long previousMillis = 0;
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= 1000) {   // Update the connection status only every second, to avoid callback issues
        previousMillis = currentMillis;
        displayConectionStatus();
    }

    while(numConnections == 0){
        lcd.setCursor(0, 3);
        lcd.print("ERROR: No connection");
        displayConectionStatus();
        updateStatusLED(5);
        displayTimeStamp();
        logState = false;
    }

    if(buttonState()){
        if (numConnections == 4){
            updateStatusLED(3);

        }else{
            updateStatusLED(1);
            logLoop();
        }

    }else{
        if (numConnections == 4){
            updateStatusLED(2);
        }else{
            updateStatusLED(6);
        }

        lcd.setCursor(0, 3);
        lcd.print("Idle (ready to log) ");
    }
    Serial.println("!!! Main loop running !!!");
}