/*
 * ReliabilityDemo
 *
 * Exercises:
 *   - connection-state callback (connect/disconnect)
 *   - tag-completion callback
 *   - tag-pool exhaustion (createTag returns NoMemory when full)
 *   - abortTag (cancel an in-flight operation)
 *   - reconnect (repeated connect/disconnect cycles)
 *
 * Point TARGET_IP/TARGET_PORT at a PLC or tools/synthetic_eip_server.py.
 */

#include <ESP32ControlLogix.h>

const IPAddress LOCAL_IP(192, 168, 1, 50);
const IPAddress LOCAL_GATEWAY(192, 168, 1, 1);
const IPAddress LOCAL_SUBNET(255, 255, 255, 0);
const IPAddress LOCAL_DNS(192, 168, 1, 1);

const IPAddress TARGET_IP(192, 168, 1, 2);
constexpr uint16_t TARGET_PORT = 44818;
constexpr uint32_t CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t TAG_TIMEOUT_MS = 5000;
constexpr uint32_t RETRY_DELAY_MS = 5000;

clx::PlcClient plc;

void onTag(int handle, clx::Status status, void *userData) {
    (void)userData;
    Serial.printf("  [tag cb] handle=%d status=%s\n", handle, clx::statusString(status));
}

void onState(clx::Status status, void *userData) {
    (void)userData;
    Serial.printf("  [state cb] %s\n", clx::statusString(status));
}

enum class Phase : uint8_t {
    Connect, Exhaust, Read, Abort, Disconnect, RetryWait,
};

Phase phase = Phase::Connect;
uint32_t phaseStarted = 0;
int tag = -1;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("ESP32ControlLogix %s - reliability demo\n", clx::version());

    plc.setTagCallback(onTag, nullptr);
    plc.setStateCallback(onState, nullptr);

    clx::Client::Config cfg;
    cfg.ip = LOCAL_IP;
    cfg.gateway = LOCAL_GATEWAY;
    cfg.subnet = LOCAL_SUBNET;
    cfg.dns = LOCAL_DNS;

    // Ethernet PHY configuration.
    // The defaults below match the M5Stack AtomS3 + AtomPoE (W5500) SPI PHY.
    // To use different SPI pins, uncomment and adjust:
    //   cfg.sck = 5;     // SPI clock
    //   cfg.miso = 7;    // SPI MISO
    //   cfg.mosi = 8;    // SPI MOSI
    //   cfg.cs = 6;      // chip select (SPI PHYs)
    //
    // To use an RMII PHY (e.g. LAN8720) on a board with the built-in EMAC,
    // uncomment and adjust these instead of the SPI defaults:
    //   cfg.phyType = ETH_PHY_LAN8720;
    //   cfg.rmii = true;
    //   cfg.phyAddr = 0;
    //   cfg.mdc = 23;
    //   cfg.mdio = 18;
    //   cfg.power = -1;
    //   cfg.clkMode = ETH_CLOCK_GPIO0_IN;  // 0

    plc.begin(cfg);
    plc.connect(TARGET_IP, TARGET_PORT, CONNECT_TIMEOUT_MS);
    phaseStarted = millis();
}

void loop() {
    switch (phase) {
        case Phase::Connect: {
            clx::Status st = plc.poll();
            if (plc.ready()) {
                Serial.printf("Connected: session=0x%08lX\n", (unsigned long)plc.sessionHandle());
                phase = Phase::Exhaust;
                phaseStarted = millis();
            } else if (st == clx::Status::Timeout || st == clx::Status::Error || st == clx::Status::Closed) {
                Serial.printf("connect failed: %s\n", clx::statusString(st));
                phase = Phase::RetryWait;
                phaseStarted = millis();
            }
            break;
        }
        case Phase::Exhaust: {
            // Create tags until the pool is exhausted.
            int h = plc.createTag("TestDint", 1);
            if (h >= 0) {
                Serial.printf("createTag -> %d (count=%d)\n", h, plc.tagCount());
            } else {
                Serial.printf("createTag -> %s (pool exhausted, count=%d)\n",
                              clx::statusString(static_cast<clx::Status>(h)), plc.tagCount());
                tag = 0;  // reuse handle 0 for the read/abort demo
                plc.read(tag, TAG_TIMEOUT_MS);
                phase = Phase::Read;
                phaseStarted = millis();
            }
            break;
        }
        case Phase::Read: {
            plc.poll();
            clx::Status st = plc.tagStatus(tag);
            if (st == clx::Status::Ok) {
                Serial.printf("read done: TestDint = %ld\n", (long)plc.tag(tag)->getInt32(0));
                plc.read(tag, TAG_TIMEOUT_MS);  // start another read to abort
                phase = Phase::Abort;
                phaseStarted = millis();
            } else if (st == clx::Status::Error) {
                Serial.printf("read failed (CIP 0x%02X)\n", plc.tag(tag)->resultCode());
                phase = Phase::Disconnect;
                phaseStarted = millis();
            }
            break;
        }
        case Phase::Abort: {
            plc.poll();
            plc.abortTag(tag);
            Serial.println("aborted in-flight read");
            phase = Phase::Disconnect;
            phaseStarted = millis();
            break;
        }
        case Phase::Disconnect: {
            // Destroy all tags, then disconnect.
            for (int i = 0; i < int(clx::PlcClient::kMaxTags); ++i) {
                plc.destroyTag(i);
            }
            plc.disconnect();
            Serial.println("disconnected; will reconnect.");
            phase = Phase::RetryWait;
            phaseStarted = millis();
            break;
        }
        case Phase::RetryWait:
        default:
            if (millis() - phaseStarted >= RETRY_DELAY_MS) {
                plc.connect(TARGET_IP, TARGET_PORT, CONNECT_TIMEOUT_MS);
                phase = Phase::Connect;
                phaseStarted = millis();
            }
            break;
    }
    delay(10);
}
