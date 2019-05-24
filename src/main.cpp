#include <Arduino.h>
/**
 * A BLE client example that is rich in capabilities.
 */
#include <WiFi.h>

//needed for library
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFiManager.h>

#include "esp_system.h"
#include "BLEDevice.h"
//#include "BLEScan.h"

#define UPDATE_TIME  30000
#define UPDATE_LIMIT 60000

static BLEUUID advertisedUUID("0000180f-0000-1000-8000-00805f9b34fb");
// The remote service we wish to connect to.
static BLEUUID serviceUUID("226c0000-6476-4566-7562-66734470666d");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("226caa55-6476-4566-7562-66734470666d");

static BLEAddress *pServerAddress;
static BLEClient*  pClient;
static boolean doConnect = false;
static boolean connected = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;

static double temperature;
static double humidity;
static unsigned long lastUpdate = 0;
static int i = 0;

void IRAM_ATTR resetModule() {
  ets_printf("REBOOT\n");
  esp_restart();
}

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

    Serial.printf("%.1f / %.1f\n", temperature, humidity);

    lastUpdate = millis();
    // Disconnect from BT
    pClient->disconnect();
    connected = false;
    doConnect = false;
}

bool connectToServer(BLEAddress pAddress) {
    Serial.print("Forming a connection to ");
    Serial.println(pAddress.toString().c_str());

    pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");

    // Connect to the remove BLE Server.
    pClient->connect(pAddress);
    Serial.println(" - Connected to server");

    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
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
}
/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.getServiceUUID().equals(advertisedUUID)) {

      //
      Serial.print("Found our device!  address: ");
      advertisedDevice.getScan()->stop();

      pServerAddress = new BLEAddress(advertisedDevice.getAddress());
      doConnect = true;

    } // Found our server
  } // onResult
}; // MyAdvertisedDeviceCallbacks


void setup() {
  Serial.begin(115200);

  lastUpdate = millis();

  WiFiManager wifiManager;
  wifiManager.autoConnect("MijiaBleBridge");

  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init("");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 30 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->start(30);
} // End of setup.


// This is the Arduino main loop function.
void loop() {
  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
  // connected we set the connected flag to be true.
  if (doConnect) {
    if (connectToServer(*pServerAddress)) {
      Serial.println("We are now connected to the BLE Server.");
      connected = true;
    } else {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    }
    doConnect = false;

  }

  delay(1000); // Delay a second between loops.
  if (millis() - lastUpdate  > UPDATE_TIME) {
    doConnect = true;
  }
  if (millis() - lastUpdate > UPDATE_LIMIT) {
    resetModule();
  }
} // End of loop
