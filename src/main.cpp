#include <FS.h> //this needs to be first, or it all crashes and burns...
#include <SPIFFS.h>

#include <Arduino.h>

#include <WiFi.h>

//needed for library
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFiManager.h>

#include "esp_system.h"
#include "BLEDevice.h"
//#include "BLEScan.h"

#include <ArduinoJson.h>

extern "C" {
	#include "freertos/FreeRTOS.h"
	#include "freertos/timers.h"
}

#include <PubSubClient.h>

#define MQTT_SECURE true

#define UPDATE_TIME  60000
#define UPDATE_LIMIT 180000

static BLEUUID advertisedUUID("0000180f-0000-1000-8000-00805f9b34fb");
//static BLEUUID advertisedUUID("226c0000-6476-4566-7562-66734470666d");
// The remote service we wish to connect to.
static BLEUUID serviceUUID("226c0000-6476-4566-7562-66734470666d");
//static BLEUUID serviceUUID("180f-0000-1000-8000-00805f9b34fb");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("226caa55-6476-4566-7562-66734470666d");

static BLEAddress* pServerAddresses[20];

static BLEAddress *pServerAddress;
static BLEClient*  pClient;
static boolean doConnect = false;
static boolean connected = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static int count=0;
static int current=0;
static bool scanning = false;

static double temperature;
static double humidity;
static unsigned long lastUpdate = 0;

static WiFiManager wifiManager;

static WebServer server(80);

//flag for saving data
bool shouldSaveConfig = false;

void IRAM_ATTR resetModule() {
  ets_printf("REBOOT\n");
  esp_restart();
}

// ####################################################
// ############### BLE FUNCTIONS ######################
// ####################################################
static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    Serial.print("Notify callback for characteristic ");
    Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
    Serial.print(" of data length ");
    Serial.println(length);
    String data = String((char *)pData);
    temperature = data.substring(2, 6).toDouble();
    humidity = data.substring(9, 13).toDouble();

    Serial.println(pBLERemoteCharacteristic->toString().c_str());
    Serial.printf("%.1f / %.1f\n", temperature, humidity);

    lastUpdate = millis();
    // Disconnect from BT
    pRemoteCharacteristic->registerForNotify(NULL, false);
    pClient->disconnect();
    connected = false;
    scanning = false;

    if (current < count) {
      doConnect = true;
    }
}

bool connectToServer(BLEAddress *pAddress) {

    if (pAddress->toString() == "4c:65:a8:d4:c3:5d") {
      current++;
      return false;
    }
    Serial.print("Forming a connection to ");
    Serial.println(pAddress->toString().c_str());

    pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");
    delay(200);

    // Connect to the remove BLE Server.
    pClient->connect(*pAddress);
    Serial.println(" - Connected to server");
    delay(200);

    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    delay(200);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      return false;
    }
    Serial.println(" - Found our service");


    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(charUUID.toString().c_str());
      return false;
    }
    Serial.println(" - Found our characteristic");
    if(pRemoteCharacteristic->canNotify()) {
      Serial.println(" - Register for notifications");
      pRemoteCharacteristic->registerForNotify(notifyCallback);
    }
    return true;
}
/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.println("BLE Advertised Device found");
    
    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.getName() == "MJ_HT_V1") {

      Serial.println("!!! Found our device !!!");
      //advertisedDevice.getScan()->stop();

      //pServerAddress = new BLEAddress(advertisedDevice.getAddress());
      pServerAddresses[count++] = new BLEAddress(advertisedDevice.getAddress());
      //doConnect = true;
      Serial.println(advertisedDevice.toString().c_str());
    } // Found our server
  } // onResult
}; // MyAdvertisedDeviceCallbacks

static void scanCompleteCB(BLEScanResults scanResults) {
  Serial.println("Scan complete");
  for (int i=0; i<count; i++) {
    Serial.println(pServerAddresses[i]->toString().c_str());
  }
  doConnect = true;
}

static void scanDevices() {
  scanning = true;
  count = 0;
  current = 0;
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->start(30, scanCompleteCB);
}
// ####################################################
// ########### END BLE FUNCTIONS ######################
// ####################################################

// ####################################################
// ############## MQTT FUNCTIONS ######################
// ####################################################
void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);
}

// ####################################################
// ########## END MQTT FUNCTIONS ######################
// ####################################################

// ####################################################
// ############## WIFI FUNCTIONS ######################
// ####################################################
//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

// ####################################################
// ########## END WIFI FUNCTIONS ######################
// ####################################################


void setup() {
  Serial.begin(115200);

  lastUpdate = millis();

  //wifiManager.resetSettings();
  if (!wifiManager.autoConnect("MijiaBleBridge")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    resetModule();
  }
  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init("");
  

  // Connect to MQTT Broker


  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 30 seconds.
  scanDevices();
} // End of setup.


// This is the Arduino main loop function.
void loop() {
  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
  // connected we set the connected flag to be true.
  if (doConnect) {
    doConnect = false;
    for (int i = 0; i < 3; i++) {
      Serial.print("Connection try : ");
      Serial.println(i);
      if (connectToServer(pServerAddresses[current])) {
        Serial.println("We are now connected to the BLE Server.");
        current++;
        connected = true;
        break;
      }
    } 
    if(!connected) {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    }
  }

  delay(1000); // Delay a second between loops.
  
  if (millis() - lastUpdate  > UPDATE_TIME && !scanning) {
    Serial.println("Scan");
    scanDevices();
  }
  if (millis() - lastUpdate > UPDATE_LIMIT) {
    resetModule();
  }
} // End of loop
