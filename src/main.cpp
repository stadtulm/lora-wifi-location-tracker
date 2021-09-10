// we use our own config.h
#define ARDUINO_LMIC_PROJECT_CONFIG_H_SUPPRESS 1
#include "config.h"

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <ESP8266WiFi.h>

void os_getArtEui (u1_t* buf) { memcpy_P(buf, APPEUI, 8); }
void os_getDevEui (u1_t* buf) { memcpy_P(buf, DEVEUI, 8); }
void os_getDevKey (u1_t* buf) { memcpy_P(buf, APPKEY, 16); }

u1_t NWKSKEY[16] = {};
u1_t APPSKEY[16] = {};
u4_t DEVADDR = 0;

static osjob_t sendjob;

const uint8 PAYLOAD_VERSION = 2;

// Pin mapping
const lmic_pinmap lmic_pins = {
    .nss = D8,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = LMIC_UNUSED_PIN,
    .dio = { D1, D2, LMIC_UNUSED_PIN },
};

#define DATAVALID 0xACF2AFC2                     // Pattern for data valid in EEPROM/RTC memory
                                                 // Change if you want OTAA to renew keys.

struct savdata_t                                 // Structure of data to be saved over reset
{
  uint32_t dataValid;                            // DATAVALID if valid data (joined)
  uint8_t  devaddr[4];                           // Device address after join
  uint8_t  nwkKey[16];                           // Network session key after join
  uint8_t  artKey[16];                           // Aplication session key after join
  uint32_t seqnoUp;                              // Sequence number                      
};
savdata_t savdata;

void do_send(osjob_t* );
void saveLoraToRTCMemory();
void loadLoraFromRTCMemory();

void onEvent (ev_t ev) {
    Serial.print(os_getTime());
    Serial.print(": ");
    switch(ev) {
        case EV_SCAN_TIMEOUT:
            Serial.println(F("EV_SCAN_TIMEOUT"));
            break;
        case EV_BEACON_FOUND:
            Serial.println(F("EV_BEACON_FOUND"));
            break;
        case EV_BEACON_MISSED:
            Serial.println(F("EV_BEACON_MISSED"));
            break;
        case EV_BEACON_TRACKED:
            Serial.println(F("EV_BEACON_TRACKED"));
            break;
        case EV_JOINING:
            Serial.println(F("EV_JOINING"));
            break;
        case EV_JOINED:
            Serial.println(F("EV_JOINED"));
            {
                u4_t netid = 0;
                devaddr_t devaddr = 0;
                u1_t nwkKey[16];
                u1_t artKey[16];
                LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);
            }
            LMIC_setLinkCheckMode(0);
            break;
        case EV_RFU1:
            Serial.println(F("EV_RFU1"));
            break;
        case EV_JOIN_FAILED:
            Serial.println(F("EV_JOIN_FAILED"));
            break;
        case EV_REJOIN_FAILED:
            Serial.println(F("EV_REJOIN_FAILED"));
            break;
        case EV_TXCOMPLETE:
            Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
            if (LMIC.txrxFlags & TXRX_ACK)
              Serial.println(F("Received ack"));
            if (LMIC.dataLen) {
              Serial.println(F("Received "));
              Serial.println(LMIC.dataLen);
              Serial.println(F(" bytes of payload"));
            }
            // Schedule next transmission
            //os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
            
            //LMIC_shutdown();
            saveLoraToRTCMemory();
            
            //Einschlafen
            ESP.deepSleep(SLEEP_DURATION);
            break;
        case EV_LOST_TSYNC:
            Serial.println(F("EV_LOST_TSYNC"));
            break;
        case EV_RESET:
            Serial.println(F("EV_RESET"));
            break;
        case EV_RXCOMPLETE:
            // data received in ping slot
            Serial.println(F("EV_RXCOMPLETE"));
            break;
        case EV_LINK_DEAD:
            Serial.println(F("EV_LINK_DEAD"));
            break;
        case EV_LINK_ALIVE:
            Serial.println(F("EV_LINK_ALIVE"));
            break;
         default:
            Serial.println(F("Unknown event"));
            break;
    }
}
int scanWifi(byte* data);
uint8 getVoltage();
void do_send(osjob_t* j){
    // Check if there is not a current TX/RX job running
    if (LMIC.opmode & OP_TXRXPEND) {
        Serial.println(F("OP_TXRXPEND, not sending"));
    } else {
        // Prepare upstream data transmission at the next possible time.
        
        byte data[53];
        int len = scanWifi(data+2)+2;
        data[0] = PAYLOAD_VERSION;
        data[1] = getVoltage();
        LMIC_setTxData2(1, data, len, 0);
        Serial.println(F("Packet queued"));
    }
    // Next TX is scheduled after TX_COMPLETE event.
}

uint8 getVoltage() {
    int voltageRawValue = analogRead(A0);
    /*Serial.printf("voltageRawValue: %i\n", voltageRawValue);
    Serial.printf("A0 voltage: %f\n", ((double)voltageRawValue)/(double)1024);
    double correctedVoltage = ((double)voltageRawValue)/(double)1024*7.8;
    Serial.printf("corrected voltage: %f\n", correctedVoltage);*/
    uint8 oneByteValue = voltageRawValue/4;
    //Serial.printf("oneByteValue: %f\n", oneByteValue);
    return oneByteValue;
}

struct minWifi {
  int id;
  int rssi;
};

int wifiComparer(const void * w1, const void * w2) {
  const struct minWifi *elem1 = (minWifi*)w1;    
  const struct minWifi *elem2 = (minWifi*)w2;
  return ( elem2->rssi - elem1->rssi );
}

int scanWifi(byte* data) {
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks(false, false);
  //we dont need the wifi anymore
  WiFi.forceSleepBegin();
  String ssid;
  uint8_t encryptionType;
  int32_t RSSI;
  uint8_t* BSSID;
  int32_t channel;
  bool isHidden;


  minWifi wifilist[n];
  for (int i = 0; i < n; i++) {
    WiFi.getNetworkInfo(i, ssid, encryptionType, RSSI, BSSID, channel, isHidden);
    wifilist[i] = {i, RSSI};
  }
  qsort(wifilist, n, sizeof(minWifi), wifiComparer);

  int datalen = 0;
  for (int i = 0; i < n; i++)
  {
    WiFi.getNetworkInfo(wifilist[i].id, ssid, encryptionType, RSSI, BSSID, channel, isHidden);
    Serial.print("WIFI: ");
    Serial.println(ssid);
    memcpy(data+i*6, BSSID, 6);
    datalen = i*6+6;
    if (i==7) {
      break;
    }
  }

  return datalen;
}

void setup() {
    Serial.begin(115200);
    Serial.println(F("Starting"));
    Serial.printf("DEVADDR: 0x%8X\n", DEVADDR);

    #ifdef VCC_ENABLE
    // For Pinoccio Scout boards
    pinMode(VCC_ENABLE, OUTPUT);
    digitalWrite(VCC_ENABLE, HIGH);
    delay(1000);
    #endif

    // LMIC init
    os_init();
    // Reset the MAC state. Session and pending data transfers will be discarded.
    LMIC_reset();

    // Set static session parameters. Instead of dynamically establishing a session
    // by joining the network, precomputed session parameters are be provided.
    #ifdef PROGMEM
    // On AVR, these values are stored in flash and only copied to RAM
    // once. Copy them to a temporary buffer here, LMIC_setSession will
    // copy them into a buffer of its own again.
    //uint8_t appskey[sizeof(APPSKEY)];
    //uint8_t nwkskey[sizeof(NWKSKEY)];
    //memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
    //memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));
    //LMIC_setSession (0x13, DEVADDR, nwkskey, appskey);
    #else
    // If not running an AVR with PROGMEM, just use the arrays directly
    //LMIC_setSession (0x13, DEVADDR, NWKSKEY, APPSKEY);
    #endif
    loadLoraFromRTCMemory();

    #if defined(CFG_eu868)
    // Set up the channels used by the Things Network, which corresponds
    // to the defaults of most gateways. Without this, only three base
    // channels from the LoRaWAN specification are used, which certainly
    // works, so it is good for debugging, but can overload those
    // frequencies, so be sure to configure the full frequency range of
    // your network here (unless your network autoconfigures them).
    // Setting up channels should happen after LMIC_setSession, as that
    // configures the minimal channel set.
    // NA-US channels 0-71 are configured automatically
    LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(DR_SF12, DR_SF7B), BAND_CENTI);      // g-band
    LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(3, 867100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(4, 867300000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(5, 867500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(6, 867700000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(7, 867900000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(8, 868800000, DR_RANGE_MAP(DR_FSK,  DR_FSK),  BAND_MILLI);      // g2-band
    // TTN defines an additional channel at 869.525Mhz using SF9 for class B
    // devices' ping slots. LMIC does not have an easy way to define set this
    // frequency and support for class B is spotty and untested, so this
    // frequency is not configured here.
    #elif defined(CFG_us915)
    // NA-US channels 0-71 are configured automatically
    // but only one group of 8 should (a subband) should be active
    // TTN recommends the second sub band, 1 in a zero based count.
    // https://github.com/TheThingsNetwork/gateway-conf/blob/master/US-global_conf.json
    LMIC_selectSubBand(1);
    #endif

    // Disable link check validation
    LMIC_setLinkCheckMode(0);

    // TTN uses SF9 for its RX2 window.
    LMIC.dn2Dr = DR_SF9;

    // Set data rate and transmit power for uplink (note: txpow seems to be ignored by the library)
    LMIC_setDrTxpow(DR_SF7,14);

    // Start job
    do_send(&sendjob);
}

void saveLoraToRTCMemory() {
    memcpy(savdata.devaddr, &LMIC.devaddr, 4);
    memcpy(savdata.nwkKey, LMIC.nwkKey, 16);
    memcpy(savdata.artKey, LMIC.artKey, 16);
    savdata.seqnoUp = LMIC.seqnoUp;
    savdata.dataValid = DATAVALID;
    ESP.rtcUserMemoryWrite(0, (uint32_t*) &savdata, sizeof(savdata));
}

void loadLoraFromRTCMemory() {
    ESP.rtcUserMemoryRead(0, (uint32_t*) &savdata, sizeof(savdata));
    if (savdata.dataValid == DATAVALID) {
        memcpy((uint8_t*)&DEVADDR, savdata.devaddr, sizeof(DEVADDR));
        memcpy(NWKSKEY, savdata.nwkKey, sizeof(NWKSKEY));          // LoRaWAN NwkSKey, network session key.
        memcpy(APPSKEY, savdata.artKey, sizeof(APPSKEY));
        LMIC_setSession(0x1, DEVADDR, NWKSKEY, APPSKEY);
        LMIC.seqnoUp = savdata.seqnoUp;
    }
}

void loop() {
    os_runloop_once();
}