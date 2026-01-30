#include <Arduino.h>
#include "NimBLEDevice.h"
#include "NimBLEUtils.h"
#include "ArduinoJson.h"
#include "BTHomeDecoder.h"

#ifndef Scan_duration
    #define Scan_duration 1000*5 //define the duration for a scan; in milliseconds
#endif
#ifndef BLEScanInterval
    #define BLEScanInterval 52 // How often the scan occurs / switches channels; in milliseconds,
#endif
#ifndef BLEScanWindow
    #define BLEScanWindow 30 // How long to scan during the interval; in milliseconds.
#endif

static BTHomeDecoder bthDecoder;

// We'll store the latest reading in a global struct
// or you can store them in a queue if you expect many devices
struct BTHomeReading {
    bool valid = false;
    String mac;
    String devName;
    int rssi;
    JsonDocument doc;
};

static BTHomeReading latestReading;
NimBLEAddress esp("48:ca:43:39:32:a5", BLE_ADDR_PUBLIC);

// Create a global flag to indicate new reading is ready
static volatile bool newReadingReady = false;

BLEScan *scan;
const char *key = ""; // "431d39c1d7cc1ac1aef224cd096db934"

class scanCallbacks : public NimBLEScanCallbacks {

    void onResult(const BLEAdvertisedDevice* advertisedDevice)  {

        // if (advertisedDevice->getAddress() != esp) return;

        // If no service data, skip
        if (!advertisedDevice->haveServiceData()) return;

        int count = advertisedDevice->getServiceDataCount();
        for (int j=0; j<count; j++) {
            // Check if FCD2 => BTHome
            std::string uuid = advertisedDevice->getServiceDataUUID(j).toString();
            if (uuid.find("fcd2") == std::string::npos) {
                continue;
            }

            // Convert to vector
            std::string rawData = advertisedDevice->getServiceData(j);
            // std::vector<uint8_t> dataVec(rawData.begin(), rawData.end());
            // log_v("--- dataVec=%s",  NimBLEUtils::dataToHexString(dataVec.data(), dataVec.size()).c_str());

            // Decode
            BTHomeDecodeResult bthRes = bthDecoder.parseBTHomeV2(
                advertisedDevice->getServiceData(j),
                                            advertisedDevice->getAddress().toString().c_str(),
                                            key
                                        );

            if (bthRes.isBTHome && bthRes.decryptionSucceeded) {
                // -----------------------------------------------------------
                // 1) Copy everything into our global BTHomeReading struct
                //    so we are not depending on NimBLE's memory after return
                // -----------------------------------------------------------
                latestReading.valid = true;

                // Copy the MAC address into a String
                String mac = advertisedDevice->getAddress().toString().c_str();
                mac.toUpperCase();
                latestReading.mac = mac;

                // Copy the name
                // WARNING: getName() memory might go away after callback,
                // so store it in a local String now
                String devName;

                // for some reason this crashes
                // if (advertisedDevice->haveName()) {
                //     devName = advertisedDevice->getName().c_str();
                // } else {
                //     devName = "NoName";
                // }
                latestReading.devName = devName;

                // Copy other fields
                latestReading.rssi = advertisedDevice->getRSSI();

                // 2) Build the JSON doc in the global struct
                //    (No more printing here, do it in loop())
                latestReading.doc.clear();
                JsonObject root = latestReading.doc.to<JsonObject>();

                root["id"] = latestReading.mac;
                root["name"] = latestReading.devName;
                root["bthome_version"] = bthRes.bthomeVersion;
                root["bthome_encrypted"] = bthRes.isEncrypted;
                root["rssi"] = latestReading.rssi;

                // JsonArray measArr = root.createNestedArray("measurements");
                JsonArray measArr = root["measurements"].to<JsonArray>();

                for (auto &m : bthRes.measurements) {
                    JsonObject obj = measArr.add<JsonObject>(); // createNestedObject();
                    obj["object_id"] = m.objectID;
                    obj["name"]      = m.name;
                    obj["value"]     = m.value;
                    obj["unit"]      = m.unit;
                }

                // Set the flag
                newReadingReady = true;
            }
        }
    }

    void onScanEnd(const NimBLEScanResults &results, int reason) override {
        log_v("Scan ended reason = %d; restarting scan", reason);
        NimBLEDevice::getScan()->start(Scan_duration, false, true);
    }
} scanCallbacks;

void setup() {
    Serial.begin(115200);
    NimBLEDevice::init("");
    scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&scanCallbacks, false);
    scan->setActiveScan(false);
    scan->setInterval(BLEScanInterval);
    scan->setWindow(BLEScanWindow);
    scan->setDuplicateFilter(false);

    scan->setMaxResults(0);
    scan->start(Scan_duration, false, true);
    log_i("BLE scan started");
}

void loop() {
    // Check if new reading is ready
    if(newReadingReady) {
        // Turn off scanning while we handle print, to avoid interruption
        scan->stop();

        Serial.println("===== BTHome Advertisement Decoded =====");
        serializeJsonPretty(latestReading.doc, Serial);
        Serial.println("\n========================================\n");

        // Clear the flag
        newReadingReady = false;
        latestReading.valid = false;

        // Resume scanning
        scan->start(Scan_duration, false, true);
    }

    delay(500);
}
