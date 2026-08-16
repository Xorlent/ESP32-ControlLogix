/*
 * PlcClientDemo - the top-level public API demo.
 *
 * Uses clx::PlcClient to connect to a PLC (or the synthetic server) and do a
 * read-modify-write of a DINT tag through the bounded tag registry.
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

enum class Phase : uint8_t {
    Connect, Read, Write, Verify, Done, RetryWait,
};

Phase phase = Phase::Connect;
uint32_t phaseStarted = 0;
int tag = -1;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("ESP32ControlLogix %s - PlcClient demo\n", clx::version());

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

    clx::Status st = plc.begin(cfg);
    Serial.printf("PlcClient::begin() -> %s\n", clx::statusString(st));
    st = plc.connect(TARGET_IP, TARGET_PORT, CONNECT_TIMEOUT_MS);
    Serial.printf("PlcClient::connect() -> %s\n", clx::statusString(st));
    phaseStarted = millis();
}

void loop() {
    switch (phase) {
        case Phase::Connect: {
            clx::Status st = plc.poll();
            if (plc.ready()) {
                Serial.printf("Connected: session=0x%08lX\n",
                              (unsigned long)plc.sessionHandle());
                tag = plc.createTag("TestDint", 1);
                Serial.printf("createTag -> %d (%s)\n", tag,
                              tag < 0 ? clx::statusString(static_cast<clx::Status>(tag)) : "ok");
                if (tag < 0) {
                    phase = Phase::Done;
                    break;
                }
                plc.read(tag, TAG_TIMEOUT_MS);
                phase = Phase::Read;
                phaseStarted = millis();
            } else if (st == clx::Status::Timeout || st == clx::Status::Error) {
                Serial.printf("connect failed: %s\n", clx::statusString(st));
                phase = Phase::RetryWait;
                phaseStarted = millis();
            }
            break;
        }
        case Phase::Read: {
            plc.poll();
            clx::Status st = plc.tagStatus(tag);
            if (st == clx::Status::Ok) {
                Serial.printf("TestDint = %ld\n", (long)plc.tag(tag)->getInt32(0));
                plc.tag(tag)->setInt32(0, 2026);
                plc.write(tag, TAG_TIMEOUT_MS);
                phase = Phase::Write;
                phaseStarted = millis();
            } else if (st == clx::Status::Error) {
                Serial.printf("read failed (CIP 0x%02X)\n", plc.tag(tag)->resultCode());
                phase = Phase::Done;
            }
            break;
        }
        case Phase::Write: {
            plc.poll();
            clx::Status st = plc.tagStatus(tag);
            if (st == clx::Status::Ok) {
                Serial.println("wrote TestDint = 2026");
                plc.read(tag, TAG_TIMEOUT_MS);
                phase = Phase::Verify;
                phaseStarted = millis();
            } else if (st == clx::Status::Error) {
                Serial.printf("write failed (CIP 0x%02X)\n", plc.tag(tag)->resultCode());
                phase = Phase::Done;
            }
            break;
        }
        case Phase::Verify: {
            plc.poll();
            clx::Status st = plc.tagStatus(tag);
            if (st == clx::Status::Ok) {
                Serial.printf("TestDint = %ld (verify)\n", (long)plc.tag(tag)->getInt32(0));
                plc.destroyTag(tag);
                tag = -1;
                phase = Phase::Done;
                phaseStarted = millis();
            } else if (st == clx::Status::Error) {
                Serial.printf("verify read failed (CIP 0x%02X)\n", plc.tag(tag)->resultCode());
                phase = Phase::Done;
            }
            break;
        }
        case Phase::Done: {
            plc.disconnect();
            Serial.println("done; will retry.");
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
