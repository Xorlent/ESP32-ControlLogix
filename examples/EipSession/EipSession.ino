/*
 * EipSession - EtherNet/IP session demo.
 *
 *   1. begins Ethernet (static IPv4) and polls until ready;
 *   2. non-blocking TCP-connects to TARGET_IP:TARGET_PORT;
 *   3. opens a session (RegisterSession) and polls until registered;
 *   4. prints the returned session handle;
 *   5. closes the session (UnregisterSession) and repeats.
 *
 * Point TARGET_IP/TARGET_PORT at a PLC (EtherNet/IP port 44818) or a host
 * running a TCP server. Against a raw server (e.g. ncat) you can observe the
 * RegisterSession request bytes, but a real EtherNet/IP device is required to
 * receive a valid session handle.
 */

#include <ESP32ControlLogix.h>

// Static IPv4 for the AtomPoE on the target LAN.
const IPAddress LOCAL_IP(192, 168, 1, 50);
const IPAddress LOCAL_GATEWAY(192, 168, 1, 1);
const IPAddress LOCAL_SUBNET(255, 255, 255, 0);
const IPAddress LOCAL_DNS(192, 168, 1, 1);

// EtherNet/IP target (change to your PLC).
const IPAddress TARGET_IP(192, 168, 1, 2);
constexpr uint16_t TARGET_PORT = 44818;
constexpr uint32_t CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t SESSION_TIMEOUT_MS = 5000;
constexpr uint32_t HOLD_MS = 2000;
constexpr uint32_t RETRY_DELAY_MS = 5000;

clx::Client eth;
clx::Client::Config cfg;
clx::TcpConnection conn;
clx::Session session;

enum class Phase : uint8_t {
    WaitEthernet,
    Connect,
    OpenSession,
    Registered,
    CloseSession,
    RetryWait,
};

Phase phase = Phase::WaitEthernet;
uint32_t phaseStarted = 0;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("ESP32ControlLogix %s - EtherNet/IP session demo\n", clx::version());

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
                Serial.println("Ethernet failed; restarting begin().");
                eth.begin(cfg);
                phaseStarted = millis();
            }
            break;
        }
        case Phase::Connect: {
            clx::Status st = conn.poll();
            if (st == clx::Status::Ok) {
                Serial.println("TCP connected; opening session (RegisterSession).");
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
                phase = Phase::Registered;
                phaseStarted = millis();
            } else if (st == clx::Status::Timeout || st == clx::Status::Error) {
                Serial.printf("RegisterSession failed: %s\n", clx::statusString(st));
                phase = Phase::RetryWait;
                phaseStarted = millis();
            }
            break;
        }
        case Phase::Registered: {
            if (millis() - phaseStarted >= HOLD_MS) {
                clx::Status st = session.close();
                Serial.printf("Session::close() -> %s\n", clx::statusString(st));
                phase = Phase::CloseSession;
                phaseStarted = millis();
            }
            break;
        }
        case Phase::CloseSession: {
            clx::Status st = session.poll();
            if (st == clx::Status::Ok) {
                Serial.println("Session closed (UnregisterSession complete).");
                conn.close();
                phase = Phase::RetryWait;
                phaseStarted = millis();
            } else if (st == clx::Status::Timeout || st == clx::Status::Error) {
                Serial.printf("UnregisterSession failed: %s\n", clx::statusString(st));
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
