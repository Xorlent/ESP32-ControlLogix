/*
 * TagReadWrite - ContolLogix tag demo.
 *
 * Performs:
 *   1. read TestDint   -> expect 1001
 *   2. write TestDint  = 2026
 *   3. read TestDint   -> expect 2026 (verifies the write)
 *   4. read TestReal   -> expect 3.14
 *   5. read TestString -> expect "Hello World"
 *
 * Point TARGET_IP/TARGET_PORT at a PLC or the host-side synthetic server
 * (tools/synthetic_eip_server.py), which holds these tags in memory.
 */

#include <ESP32ControlLogix.h>

const IPAddress LOCAL_IP(192, 168, 1, 50);
const IPAddress LOCAL_GATEWAY(192, 168, 1, 1);
const IPAddress LOCAL_SUBNET(255, 255, 255, 0);
const IPAddress LOCAL_DNS(192, 168, 1, 1);

const IPAddress TARGET_IP(192, 168, 1, 2);
constexpr uint16_t TARGET_PORT = 44818;
constexpr uint32_t CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t SESSION_TIMEOUT_MS = 5000;
constexpr uint32_t TAG_TIMEOUT_MS = 5000;
constexpr uint32_t RETRY_DELAY_MS = 5000;

clx::Client eth;
clx::Client::Config cfg;
clx::TcpConnection conn;
clx::Session session;
clx::Tag tag;

enum class Phase : uint8_t {
    WaitEthernet, Connect, OpenSession, TagOps, CloseSession, RetryWait,
};

Phase phase = Phase::WaitEthernet;
uint32_t phaseStarted = 0;
int step = 0;
bool stepStarted = false;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("ESP32ControlLogix %s - Logix tag read/write demo\n", clx::version());

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

    clx::Status st = eth.begin(cfg);
    Serial.printf("Client::begin() -> %s\n", clx::statusString(st));
    if (st == clx::Status::Error) {
        phase = Phase::RetryWait;
    }
    phaseStarted = millis();
}

void startStep() {
    clx::Status st;
    switch (step) {
        case 0: st = tag.read(conn, session.handle(), "TestDint", 1, TAG_TIMEOUT_MS); break;
        case 1:
            tag.setInt32(0, 2026);
            st = tag.write(conn, session.handle(), "TestDint", 1, TAG_TIMEOUT_MS);
            break;
        case 2: st = tag.read(conn, session.handle(), "TestDint", 1, TAG_TIMEOUT_MS); break;
        case 3: st = tag.read(conn, session.handle(), "TestReal", 1, TAG_TIMEOUT_MS); break;
        case 4: st = tag.read(conn, session.handle(), "TestString", 1, TAG_TIMEOUT_MS); break;
        default: st = clx::Status::Error; break;
    }
    Serial.printf("step %d -> %s\n", step, clx::statusString(st));
    stepStarted = (st == clx::Status::Pending);
}

void finishStep() {
    switch (step) {
        case 0: Serial.printf("  TestDint = %ld\n", (long)tag.getInt32(0)); break;
        case 1: Serial.println("  wrote TestDint = 2026"); break;
        case 2: Serial.printf("  TestDint = %ld\n", (long)tag.getInt32(0)); break;
        case 3: Serial.printf("  TestReal = %.3f\n", (double)tag.getFloat32(0)); break;
        case 4: {
            char buf[128];
            tag.getString(buf, sizeof(buf));
            Serial.printf("  TestString = \"%s\"\n", buf);
            break;
        }
    }
    ++step;
    stepStarted = false;
    if (step >= 5) {
        Serial.println("Tag operations complete; closing session.");
        session.close();
        phase = Phase::CloseSession;
    }
}

void loop() {
    switch (phase) {
        case Phase::WaitEthernet: {
            clx::Status st = eth.poll();
            if (st == clx::Status::Ok) {
                Serial.printf("Ethernet ready: ip=%s\n", eth.localIP().toString().c_str());
                Serial.printf("Connecting to %s:%u ...\n", TARGET_IP.toString().c_str(), TARGET_PORT);
                st = conn.connect(TARGET_IP, TARGET_PORT, CONNECT_TIMEOUT_MS);
                Serial.printf("TcpConnection::connect() -> %s\n", clx::statusString(st));
                phase = Phase::Connect;
                phaseStarted = millis();
            } else if (st == clx::Status::Error) {
                eth.begin(cfg);
                phaseStarted = millis();
            }
            break;
        }
        case Phase::Connect: {
            clx::Status st = conn.poll();
            if (st == clx::Status::Ok) {
                Serial.println("TCP connected; opening session.");
                st = session.open(conn, SESSION_TIMEOUT_MS);
                Serial.printf("Session::open() -> %s\n", clx::statusString(st));
                phase = Phase::OpenSession;
                phaseStarted = millis();
            } else if (st == clx::Status::Timeout || st == clx::Status::Error) {
                Serial.printf("TCP connect failed: %s\n", clx::statusString(st));
                phase = Phase::RetryWait;
                phaseStarted = millis();
            }
            break;
        }
        case Phase::OpenSession: {
            clx::Status st = session.poll();
            if (st == clx::Status::Ok) {
                Serial.printf("Session registered: handle=0x%08lX\n",
                              (unsigned long)session.handle());
                step = 0;
                stepStarted = false;
                phase = Phase::TagOps;
                phaseStarted = millis();
            } else if (st == clx::Status::Timeout || st == clx::Status::Error) {
                Serial.printf("RegisterSession failed: %s\n", clx::statusString(st));
                phase = Phase::RetryWait;
                phaseStarted = millis();
            }
            break;
        }
        case Phase::TagOps: {
            if (!stepStarted) {
                startStep();
            } else {
                clx::Status st = tag.poll();
                if (st == clx::Status::Ok) {
                    finishStep();
                } else if (st == clx::Status::Timeout || st == clx::Status::Error) {
                    Serial.printf("step %d failed: %s (CIP 0x%02X)\n", step, clx::statusString(st), tag.resultCode());
                    phase = Phase::RetryWait;
                    phaseStarted = millis();
                }
            }
            break;
        }
        case Phase::CloseSession: {
            clx::Status st = session.poll();
            if (st == clx::Status::Ok) {
                Serial.println("Session closed.");
                conn.close();
                phase = Phase::RetryWait;
                phaseStarted = millis();
            } else if (st == clx::Status::Timeout || st == clx::Status::Error) {
                session.abort();
                conn.close();
                phase = Phase::RetryWait;
                phaseStarted = millis();
            }
            break;
        }
        case Phase::RetryWait:
        default:
            session.abort();
            conn.close();
            if (millis() - phaseStarted >= RETRY_DELAY_MS) {
                phase = Phase::WaitEthernet;
                phaseStarted = millis();
            }
            break;
    }
    delay(10);
}

