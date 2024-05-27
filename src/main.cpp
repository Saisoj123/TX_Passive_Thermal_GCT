#include <esp_now.h>
#include <WiFi.h>
#include <RTClib.h>

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
char timestamp[20];


uint8_t broadcastAddresses[][6] = {
    {0x48, 0xE7, 0x29, 0x8C, 0x78, 0x30},
    {0x48, 0xE7, 0x29, 0x8C, 0x6B, 0x5C},
    {0x48, 0xE7, 0x29, 0x8C, 0x72, 0x50},
    {0x48, 0xE7, 0x29, 0x29, 0x79, 0x68}
};
esp_now_peer_info_t peerInfo[4];

RTC_DS3231 rtc;


void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("\r\nLast Packet Send Status:\t");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
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
         
        if (result == ESP_OK) {
            Serial.println("Sending confirmed");
        }
        else {
            Serial.println("Sending error");
        }
    }

    //delay(2000); // Add a delay to prevent flooding the network
}

void waitForActionID(int actionID) {
    while (!messageReceived || receivedActionID != actionID) {
        // Wait for message with correct actionID
    }
    messageReceived = false; // Reset for next message
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

void writeToSD(String dataString) {
    Serial.print (dataString);
}


const char* get_timestamp() {
    DateTime now = rtc.now();
    sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
    return timestamp;
}


void getAllTemps() {//MARK: Get temperatures
    TXdata.actionID = 3001; //Action ID for getting all temperatures from a servent

    for (int i = 0; i < 4; i++) {
        esp_err_t result = esp_now_send(broadcastAddresses[i], (uint8_t *) &TXdata, sizeof(TXdata));
        waitForActionID(2001);
        writeToSD(tempToString(receivedData,get_timestamp(), i+1));
    }
}


void setup() {
    Serial.begin(115200);

    //------------------ ESP-NNOW -INIT - BEGIN ------------------
    WiFi.mode(WIFI_STA);

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
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
            return;
        }
    }
    //------------------ ESP-NNOW -INIT - END ------------------

    if (! rtc.begin()) {
        Serial.println("Could not find RTC!");
        while (1);
    }
    //rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); //uncomment to set the RTC to the compile time
}


void loop() {
    //SerialUserInput();
    getAllTemps();
    delay(10000);
}