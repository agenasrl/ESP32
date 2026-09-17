/*
 * ESP32 <-> ATmega16M1 tuning module BLE gateway.
 *
 * Ported from the original Arduino (.ino) sketch to plain ESP-IDF + the
 * h2zero/esp-nimble-cpp component. The BLE service/characteristic layout,
 * the serial protocol with the ATmega16M1 and the persisted settings are
 * unchanged; only the framework glue (BLE library, serial port, non-volatile
 * storage, delay/logging calls) was translated to their ESP-IDF equivalents:
 *
 *   Arduino BLE library (BLEDevice/BLEServer/BLECharacteristic/BLE2902)
 *       -> h2zero/esp-nimble-cpp (NimBLEDevice/NimBLEServer/NimBLECharacteristic)
 *          CCCD (0x2902) descriptors are created automatically for
 *          NOTIFY/INDICATE characteristics, so no BLE2902 equivalent is needed.
 *   HardwareSerial SerTED(2)  -> driver/uart.h on UART_NUM_2 (same TX=17, RX=16 pins)
 *   EEPROM.read/write/commit  -> NVS (nvs_get_u8/nvs_set_u8/nvs_commit)
 *   setup()/loop()            -> app_main()
 *   delay()                   -> vTaskDelay(pdMS_TO_TICKS())
 *   Serial.print*             -> ESP_LOGI
 */

#include <cstdint>
#include <cstring>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "NimBLEDevice.h"

static const char *TAG = "tune_gateway";

#define RECEIVE_INTERVAL   10u   /* ms, polling interval while reading from the ATmega */
#define DEFAULT_WARM_UP    1

/* UART link to the ATmega16M1 (was HardwareSerial SerTED(2) on the Arduino side) */
#define ATMEGA_UART_PORT     UART_NUM_2
#define ATMEGA_UART_TXD_PIN  17
#define ATMEGA_UART_RXD_PIN  16
#define ATMEGA_UART_BAUD     9600
#define ATMEGA_UART_BUF_SIZE 256

/* NVS keys replacing the raw EEPROM byte addresses of the original sketch */
static const char *NVS_NAMESPACE  = "app_values";
static const char *NVS_KEY_WARMUP = "warm_up";
static const char *NVS_KEY_STATE  = "state";
static const char *NVS_KEY_BOOST  = "boost";
static const char *NVS_KEY_ONOFF  = "on_off";
static const char *NVS_KEY_S1     = "s1";
static const char *NVS_KEY_S2     = "s2";

static nvs_handle_t s_nvs_handle;

static NimBLEServer         *pServer;
static NimBLEService         *pService_1;
static NimBLECharacteristic *pCharacteristic_11;
static NimBLECharacteristic *pCharacteristic_12;
static NimBLECharacteristic *pCharacteristic_13;
static NimBLECharacteristic *pCharacteristic_14;
static NimBLECharacteristic *pCharacteristic_15;
static NimBLECharacteristic *pCharacteristic_16;
static NimBLECharacteristic *pCharacteristic_17;

static uint8_t payloadReceived[256];
static uint8_t c[8];
static uint8_t error_response = 0;

typedef enum {
    ECO = 0,
    SPORT,
    RACE,
} STATE;

typedef enum {
    BOOST_OFF,
    BOOST_1,
    BOOST_2,
} BOOST;

typedef struct {
    uint8_t time_warm_up;
    uint8_t States;
    uint8_t on_off;
    uint8_t boost_situation;
    uint8_t Selettore1;
    uint8_t Selettore2;
    uint8_t warm_up_on_off;
} values;

static values App_value;

/* See the following for generating UUIDs: https://www.uuidgenerator.net/ */
#define SERVICE_UUID_1         "f858e8ea-4dbf-11ea-b77f-2e728ce88125"

#define CHARACTERISTIC_UUID_11 "a601bcbe-4dc1-11ea-b77f-2e728ce88125" /* time_warm_up */
#define CHARACTERISTIC_UUID_12 "ba846344-4dc1-11ea-b77f-2e728ce88125" /* STATE: ECO,SPORT,RACE */
#define CHARACTERISTIC_UUID_13 "c6ba9778-4dc1-11ea-b77f-2e728ce88125" /* On/OFF */
#define CHARACTERISTIC_UUID_14 "d451d842-4dc1-11ea-b77f-2e728ce88125" /* BOOST off 1 2 */
#define CHARACTERISTIC_UUID_15 "e19d3668-4dc1-11ea-b77f-2e728ce88125" /* selettore1 */
#define CHARACTERISTIC_UUID_16 "f0756e62-4dc1-11ea-b77f-2e728ce88125" /* selettore2 */
#define CHARACTERISTIC_UUID_17 "1e01ba98-58ad-4fbe-86f5-9bb87fe424bd" /* warm_up_on_off */

/*** prototypes ***/
static bool bk_writecomm(uint8_t command0, uint8_t command1, uint8_t command2);
/***            ***/

static uint8_t nvs_read_u8(const char *key, uint8_t def) {
    uint8_t value = def;
    if (nvs_get_u8(s_nvs_handle, key, &value) != ESP_OK) {
        value = def;
    }
    return value;
}

static void nvs_write_u8(const char *key, uint8_t value) {
    nvs_set_u8(s_nvs_handle, key, value);
}

static void nvs_commit_all() {
    nvs_commit(s_nvs_handle);
}

class AppCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override {
        std::string value = pCharacteristic->getValue();
        memcpy(c, value.data(), 2);

        if (pCharacteristic == pCharacteristic_11) {
            ESP_LOGI(TAG, "got APP Time warm up");
            App_value.time_warm_up = c[0];
            bk_writecomm(11, c[0], 255);
            ESP_LOGI(TAG, "warm_up: %d", c[0]);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        if (pCharacteristic == pCharacteristic_12) {
            ESP_LOGI(TAG, "got APP STATE");
            App_value.States = c[0];
            nvs_write_u8(NVS_KEY_STATE, c[0]);
            bk_writecomm(12, c[0], 255);
            ESP_LOGI(TAG, "STATE: %d", c[0]);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        if (pCharacteristic == pCharacteristic_13) {
            ESP_LOGI(TAG, "got APP ON or OFF");
            App_value.on_off = c[0];
            nvs_write_u8(NVS_KEY_ONOFF, c[0]);
            bk_writecomm(13, c[0], 255);
            ESP_LOGI(TAG, "ON/OFF: %d", c[0]);
        }
        if (pCharacteristic == pCharacteristic_14) {
            ESP_LOGI(TAG, "got APP boost_situation");
            App_value.boost_situation = c[0];
            nvs_write_u8(NVS_KEY_BOOST, c[0]);
            bk_writecomm(14, c[0], 255);
            ESP_LOGI(TAG, "BOOST: %d", c[0]);
        }
        if (pCharacteristic == pCharacteristic_15) {
            ESP_LOGI(TAG, "got APP Selettore1");
            App_value.Selettore1 = c[0];
            bk_writecomm(15, c[0], ';');
            ESP_LOGI(TAG, "S1: %d", c[0]);
        }
        if (pCharacteristic == pCharacteristic_16) {
            ESP_LOGI(TAG, "got APP Selettore2");
            App_value.Selettore2 = c[0];
            bk_writecomm(16, c[0], ';');
            ESP_LOGI(TAG, "S2: %d", c[0]);
        }
        nvs_commit_all();
    }
};

class MyServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override {
        ESP_LOGI(TAG, "Connected!");
    }

    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override {
        ESP_LOGI(TAG, "Disconnected!");
        vTaskDelay(pdMS_TO_TICKS(10));
        nvs_write_u8(NVS_KEY_STATE, c[0]);
        nvs_write_u8(NVS_KEY_ONOFF, c[0]);
        nvs_write_u8(NVS_KEY_BOOST, c[0]);
        nvs_commit_all();
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    }
};

static bool verify_data(uint8_t len) {
    uint8_t ret8;
    (void)len;

    switch (payloadReceived[0]) {
    case 11:
        ESP_LOGI(TAG, "got ATMEGA Time warm up:");
        App_value.time_warm_up = payloadReceived[1];
        nvs_write_u8(NVS_KEY_WARMUP, payloadReceived[1]);
        ret8 = payloadReceived[1];
        pCharacteristic_11->setValue(&ret8, 1); /* BLE update */
        break;
    case 12:
        ESP_LOGI(TAG, "got ATMEGA STATE");
        if (App_value.warm_up_on_off) {
            nvs_write_u8(NVS_KEY_STATE, payloadReceived[1]);
            switch (payloadReceived[1]) {
            case 0x00: App_value.States = ECO; break;
            case 0x01: App_value.States = SPORT; break;
            case 0x02: App_value.States = RACE; break;
            default: break;
            }
            ret8 = payloadReceived[1];
            pCharacteristic_12->setValue(&ret8, 1); /* BLE update */
        }
        break;
    case 13:
        ESP_LOGI(TAG, "got ATMEGA ON or OFF");
        if (App_value.warm_up_on_off) {
            if ((payloadReceived[1] == 0) || (payloadReceived[1] == 1)) {
                nvs_write_u8(NVS_KEY_ONOFF, payloadReceived[1]);
                App_value.on_off = payloadReceived[1];
                ret8 = payloadReceived[1];
                pCharacteristic_13->setValue(&ret8, 1); /* BLE update */
            }
        }
        break;
    case 14:
        if (App_value.warm_up_on_off) {
            nvs_write_u8(NVS_KEY_BOOST, payloadReceived[1]);
            switch (payloadReceived[1]) {
            case 0x00: App_value.boost_situation = BOOST_OFF; break;
            case 0x01: App_value.boost_situation = BOOST_1; break;
            case 0x02: App_value.boost_situation = BOOST_2; break;
            default: App_value.boost_situation = BOOST_OFF; break;
            }
            ret8 = payloadReceived[1];
            pCharacteristic_14->setValue(&ret8, 1); /* BLE update */
        }
        ESP_LOGI(TAG, "got ATMEGA boost_situation");
        break;
    case 15:
        ESP_LOGI(TAG, "got ATMEGA Selettore1");
        nvs_write_u8(NVS_KEY_S1, payloadReceived[1]);
        App_value.Selettore1 = payloadReceived[1];
        ret8 = payloadReceived[1];
        pCharacteristic_15->setValue(&ret8, 1); /* BLE update */
        break;
    case 16:
        ESP_LOGI(TAG, "got ATMEGA Selettore2");
        nvs_write_u8(NVS_KEY_S2, payloadReceived[1]);
        App_value.Selettore2 = payloadReceived[1];
        ret8 = payloadReceived[1];
        pCharacteristic_16->setValue(&ret8, 1); /* BLE update */
        break;
    case 17:
        ESP_LOGI(TAG, "got ATMEGA Warm_up");
        App_value.warm_up_on_off = payloadReceived[1];
        ret8 = payloadReceived[1];
        pCharacteristic_17->setValue(&ret8, 1); /* BLE update */
        break;
    case 18:
        ESP_LOGI(TAG, "got ATMEGA error");
        if (payloadReceived[1] == 0) {
            if (App_value.on_off == 0) {
                error_response = 0;
            } else {
                if ((App_value.States == ECO) && (App_value.boost_situation == BOOST_OFF)) error_response = 1;
                if ((App_value.States == ECO) && (App_value.boost_situation == BOOST_1)) error_response = 2;
                if ((App_value.States == ECO) && (App_value.boost_situation == BOOST_2)) error_response = 3;
                if ((App_value.States == SPORT) && (App_value.boost_situation == BOOST_OFF)) error_response = 4;
                if ((App_value.States == SPORT) && (App_value.boost_situation == BOOST_1)) error_response = 5;
                if ((App_value.States == SPORT) && (App_value.boost_situation == BOOST_2)) error_response = 6;
                if ((App_value.States == RACE) && (App_value.boost_situation == BOOST_OFF)) error_response = 7;
                if ((App_value.States == RACE) && (App_value.boost_situation == BOOST_1)) error_response = 8;
                if ((App_value.States == RACE) && (App_value.boost_situation == BOOST_2)) error_response = 9;
            }
            ESP_LOGI(TAG, "first value is wrong resend everything:");
            bk_writecomm(18, error_response, 255);
        } else {
            switch (payloadReceived[1]) {
            case 11:
                bk_writecomm(11, App_value.time_warm_up, 255);
                ESP_LOGI(TAG, "got error Time warm up:");
                break;
            case 12:
                bk_writecomm(12, App_value.States, 255);
                ESP_LOGI(TAG, "got error STATE");
                break;
            case 13:
                bk_writecomm(13, App_value.on_off, 255);
                ESP_LOGI(TAG, "got error ON or OFF");
                break;
            case 14:
                bk_writecomm(14, App_value.boost_situation, 255);
                ESP_LOGI(TAG, "got error boost_situation");
                break;
            case 15:
                bk_writecomm(15, App_value.Selettore1, 255);
                ESP_LOGI(TAG, "got error Selettore1");
                break;
            case 16:
                bk_writecomm(16, App_value.Selettore2, 255);
                ESP_LOGI(TAG, "got error Selettore2");
                break;
            default:
                ESP_LOGI(TAG, "got undefined error");
                break;
            }
        }
        break;
    default:
        ESP_LOGI(TAG, "got undefined ATMEGA");
        break;
    }
    ESP_LOGI(TAG, "%d", payloadReceived[1]);
    nvs_commit_all();
    return true;
}

static uint8_t handle_Serial_DataRead(uint32_t waitTimeout) {
    uint32_t payloadReceivedLength = 0;
    uint32_t timeOut = waitTimeout;

    while (timeOut) {
        vTaskDelay(pdMS_TO_TICKS(RECEIVE_INTERVAL));

        size_t availableBytes = 0;
        uart_get_buffered_data_len(ATMEGA_UART_PORT, &availableBytes);

        if (availableBytes != 0) {
            timeOut = 20;
            int readLen = uart_read_bytes(ATMEGA_UART_PORT,
                                           &payloadReceived[payloadReceivedLength],
                                           availableBytes,
                                           0);
            if (readLen > 0) {
                payloadReceivedLength += readLen;
            }
        }
        timeOut -= RECEIVE_INTERVAL;
    }

    return (uint8_t)payloadReceivedLength;
}

static bool bk_writecomm(uint8_t command0, uint8_t command1, uint8_t command2) {
    (void)command2;

    uart_write_bytes(ATMEGA_UART_PORT, (const char *)&command0, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    uart_write_bytes(ATMEGA_UART_PORT, (const char *)&command1, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    return true;
}

static void init_nvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle));
}

static void init_atmega_uart() {
    uart_config_t uart_config = {};
    uart_config.baud_rate = ATMEGA_UART_BAUD;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    ESP_ERROR_CHECK(uart_driver_install(ATMEGA_UART_PORT, ATMEGA_UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(ATMEGA_UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ATMEGA_UART_PORT, ATMEGA_UART_TXD_PIN, ATMEGA_UART_RXD_PIN,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void init_ble() {
    /* Create the BLE Device */
    NimBLEDevice::init("tune");

    /* Create the BLE Server */
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    /* Create BLE Service */
    pService_1 = pServer->createService(SERVICE_UUID_1);

    /* Create BLE Characteristics */
    pCharacteristic_11 = pService_1->createCharacteristic(
        CHARACTERISTIC_UUID_11,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);
    pCharacteristic_11->setCallbacks(new AppCallbacks());

    pCharacteristic_12 = pService_1->createCharacteristic(
        CHARACTERISTIC_UUID_12,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);
    pCharacteristic_12->setCallbacks(new AppCallbacks());

    pCharacteristic_13 = pService_1->createCharacteristic(
        CHARACTERISTIC_UUID_13,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);
    pCharacteristic_13->setCallbacks(new AppCallbacks());

    pCharacteristic_14 = pService_1->createCharacteristic(
        CHARACTERISTIC_UUID_14,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);
    pCharacteristic_14->setCallbacks(new AppCallbacks());

    pCharacteristic_15 = pService_1->createCharacteristic(
        CHARACTERISTIC_UUID_15,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);
    pCharacteristic_15->setCallbacks(new AppCallbacks());

    pCharacteristic_16 = pService_1->createCharacteristic(
        CHARACTERISTIC_UUID_16,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);
    pCharacteristic_16->setCallbacks(new AppCallbacks());

    pCharacteristic_17 = pService_1->createCharacteristic(
        CHARACTERISTIC_UUID_17,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);
    pCharacteristic_17->setCallbacks(new AppCallbacks());

    pCharacteristic_11->setValue(&App_value.time_warm_up, 1);
    pCharacteristic_12->setValue(&App_value.States, 1);
    pCharacteristic_13->setValue(&App_value.on_off, 1);
    pCharacteristic_14->setValue(&App_value.boost_situation, 1);
    pCharacteristic_15->setValue(&App_value.Selettore1, 1);
    pCharacteristic_16->setValue(&App_value.Selettore2, 1);
    pCharacteristic_17->setValue(&App_value.warm_up_on_off, 1); /* BLE update */

    /* Start the server (and all its services) */
    pServer->start();

    /* Add primary service advertising (needed for filtering by service UUID) */
    NimBLEAdvertising *pAdvertising = pServer->getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID_1);

    /* Start advertising */
    pAdvertising->start();
    ESP_LOGI(TAG, "Waiting a client connection to notify...");
}

extern "C" void app_main(void) {
    init_nvs();

    /* SerTED init: tx-rx with Atmega16M1 */
    init_atmega_uart();
    vTaskDelay(pdMS_TO_TICKS(100));

    App_value.time_warm_up = nvs_read_u8(NVS_KEY_WARMUP, 0);
    if (App_value.time_warm_up > 0x3F) App_value.time_warm_up = DEFAULT_WARM_UP;
    if (App_value.time_warm_up < 0x01) App_value.time_warm_up = 0x01;

    App_value.States = nvs_read_u8(NVS_KEY_STATE, 0);
    if (App_value.States > 0x03) App_value.States = 0x00;

    App_value.boost_situation = nvs_read_u8(NVS_KEY_BOOST, 0);
    if (App_value.boost_situation > 0x02) App_value.boost_situation = 0x00;

    App_value.on_off = nvs_read_u8(NVS_KEY_ONOFF, 0);
    if (App_value.on_off > 0x01) App_value.on_off = 0x00;

    App_value.Selettore1 = nvs_read_u8(NVS_KEY_S1, 0);
    if (App_value.Selettore1 > 0x10) App_value.Selettore1 = 0x00;

    App_value.Selettore2 = nvs_read_u8(NVS_KEY_S2, 0);
    if (App_value.Selettore2 > 0x10) App_value.Selettore2 = 0x00;

    init_ble();

    for (;;) {
        uint8_t ret = handle_Serial_DataRead(10u);
        if (ret > 0) {
            if (verify_data(ret) == true) {
                ESP_LOGI(TAG, "get correct data!");
            }
        }
    }
}
