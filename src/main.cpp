#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <RTClib.h>
#include <SD.h>


// Structure to send data
// Must match the receiver structure
typedef struct struct_message {
    int actionID;
    float value;
} struct_message;

struct_message TXdata; // Create a struct_message called data

uint8_t servantAdress[][6] ={{0x24, 0x0A, 0xC4, 0x0A, 0x0B, 0x24},
                             {0x24, 0x0A, 0xC4, 0x0A, 0x0B, 0x24},
                             {0x24, 0x0A, 0xC4, 0x0A, 0x0B, 0x24},
                             {0x24, 0x0A, 0xC4, 0x0A, 0x0B, 0x24}}; //made up address MARK: MAC ADRESS

//MARK: USER VARIABLES
int numSens =        9;  //number of DS18B20 sensors, connected to the oneWireBus
int numServ =       sizeof(servantAdress) / sizeof(servantAdress[0]);  //number of servents
int logFreq =      30;  //interval between measurements in seconds

//MARK: PIN DEFINITIONS
#define SD_CS       5

//MARK: SYSTEM VARIABLES
//Do not touch these!!!
char filename[25] = "";
unsigned long lastTime = 0;  // will store the last time the function was called
unsigned long interval = interval*1000;  // interval at which to call function (milliseconds)


RTC_DS3231 rtc; // Create a RTC object

void checkActionID(int actionID, float value) { //MARK: CHECK ACTION IDs
    switch (actionID) {
       
        case 1234:
            Serial.println("ActionID: 1234"); //later replace with actual action
            break;
        
        default:
            Serial.print("ERROR: Invalid actionID: ");
            Serial.println(actionID);
            break;
    }
}

// Callback when data is sent MARK:CALLBACKS
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("\r\nLast Packet Send Status:\t");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

// Callback when data is received
void OnDataReceived(const uint8_t *mac_addr, const uint8_t *RXdata_param, int RXdata_len) {
    struct_message local_RXdata;
    memcpy(&local_RXdata, RXdata_param, RXdata_len);

    // Process the received data
    Serial.print("Received actionID: ");
    Serial.println(local_RXdata.actionID);
    Serial.print("Received value: ");
    Serial.println(local_RXdata.value);

    checkActionID(local_RXdata.actionID, local_RXdata.value);
}

const char* updateTimeStamp() {
    DateTime now = rtc.now();
    char timestamp[20];
    sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
    return(timestamp);
}

void sendAction(int actionID, float value) { //MARK: SEND ACTION ID
    struct_message TXdata;
    TXdata.actionID = actionID;
    TXdata.value = value;

    for (int i = 0; i < numServ; i++) {
        esp_err_t result = esp_now_send(servantAdress[i], (uint8_t *) &TXdata, sizeof(TXdata));

        if (result == ESP_OK) {
            Serial.println("SUCCESS: Action ID: " + String(actionID) + " with value: " + String(value));
        }
        else {
            Serial.println("Error sending the action");
        }
    }
}

void setup() { // MARK: SETUP
    Serial.begin(115200);

    WiFi.mode(WIFI_STA);  // Set device as a Wi-Fi Station

    // Begin RTC Init -----------------------
    Wire.begin();
    if (! rtc.begin()) {
        Serial.println("Couldn't find RTC");
        while (1);
    }
    //rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); //uncomment to set the RTC to the compile time
    // End RTC Init -----------------------


    // Begin SD Card Init -----------------------
    if (!SD.begin(SD_CS)) {  // Change this to the correct CS pin!
        Serial.println("SD-Card Initialization failed!");
        return;
    }
    Serial.println("SD-Card Initialization done.");
    // End SD Card Init -----------------------


    // Begin Init ESP-NOW -----------------------
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataReceived); // Register callbacks

    // Peer info
    esp_now_peer_info_t peerInfo[numServ]; // Adjust the size of the array based on the number of receivers
    for (int i = 0; i < numServ; i++) {    // Initialize each object in the peerInfo array
        memcpy(peerInfo[i].peer_addr, servantAdress[i], 6);
        peerInfo[i].channel = 0;
        peerInfo[i].encrypt = false;

        // Add each peer individually
        if (esp_now_add_peer(&peerInfo[i]) != ESP_OK){
            Serial.println("Failed to add peer");
            return;
        }
    }
    // End Init ESP-NOW -----------------------

    sprintf(filename, "%s.txt", updateTimeStamp()); //create filename with timestamp of boot
    Serial.print("Filename: "); Serial.println(filename);
}


void loop() { // MARK: LOOP
    unsigned long currentTime = millis();
    if(currentTime - lastTime > interval) {
        lastTime = currentTime;   
        // put reoccuring code here
        sendAction(3001,1);
    }
}