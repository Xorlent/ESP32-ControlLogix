/*
 * ConnectedTagReadWrite - Connected-messaging demo.
 *
 * This demonstrates the efficient connected path (SendUnitData) with
 * connection-ID direction and sequence-number validation, as opposed to the
 * unconnected SendRRData path used by examples/TagReadWrite.
 *
 * Point TARGET_IP/TARGET_PORT at a PLC or the host-side synthetic server
 * (tools/synthetic_eip_server.py).
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
constexpr uint32_t CONN_TIMEOUT_MS = 5000;
constexpr uint32_t RETRY_DELAY_MS = 5000;

clx::Client eth;
clx::Client::Config cfg;
clx::TcpConnection tcp;
clx::Session session;
clx::Connection conn;

enum class Phase : uint8_t {
    WaitEthernet, Connect, OpenSession, ConnOps, CloseSession, RetryWait,
};

Phase phase = Phase::WaitEthernet;
uint32_t phaseStarted = 0;
int step = 0;          // 0=open, 1=read, 2=write, 3=read, 4=close
bool stepStarted = false;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("ESP32ControlLogix %s - connected tag read/write demo\n", clx::version());

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
    uint8_t path[64];
    size_t pl;
    uint8_t data[16];
    switch (step) {
        case 0:  // Forward Open
            st = conn.open(tcp, session.handle(), "TestDint", CONN_TIMEOUT_MS);
            break;
        case 1:  // connected Read Tag
            pl = clx::appendSymbolic(path, "TestDint");
            clx::putU16(data, 1);  // element count
            st = conn.send(tcp, session.handle(), 0x4C, path, pl, data, 2, CONN_TIMEOUT_MS);
            break;
        case 2:  // connected Write Tag = 2026
            pl = clx::appendSymbolic(path, "TestDint");
            clx::putU16(data, 1);          // element count
            clx::putU16(data + 2, 0x00C4); // data type DINT
            clx::putU32(data + 4, 2026);   // value
            st = conn.send(tcp, session.handle(), 0x4D, path, pl, data, 8, CONN_TIMEOUT_MS);
            break;
        case 3:  // connected Read Tag (verify)
            pl = clx::appendSymbolic(path, "TestDint");
            clx::putU16(data, 1);
            st = conn.send(tcp, session.handle(), 0x4C, path, pl, data, 2, CONN_TIMEOUT_MS);
            break;
        case 4:  // Forward Close
            st = conn.close(tcp, session.handle(), CONN_TIMEOUT_MS);
            break;
        default:
            st = clx::Status::Error;
            break;
    }
    Serial.printf("step %d -> %s\n", step, clx::statusString(st));
    stepStarted = (st == clx::Status::Pending);
}

void finishStep() {
    switch (step) {
        case 0:
            Serial.printf("  connection open: O->T=0x%08lX T->O=0x%08lX\n",
                          (unsigned long)conn.originatorConnectionId(),
                          (unsigned long)conn.targetConnectionId());
            break;
        case 1:
        case 3:
            if (conn.resultCode() == 0 && conn.dataLength() >= 6) {
                Serial.printf("  TestDint = %ld\n", (long)clx::getU32(conn.data() + 2));
            } else {
                Serial.printf("  read failed: CIP status 0x%02X\n", conn.resultCode());
            }
            break;
        case 2:
            Serial.printf("  wrote TestDint = 2026 (status 0x%02X)\n", conn.resultCode());
            break;
        case 4:
            Serial.println("  connection closed");
            break;
    }
    ++step;
    stepStarted = false;
    if (step > 4) {
        Serial.println("Connected operations complete; closing session.");
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
                st = tcp.connect(TARGET_IP, TARGET_PORT, CONNECT_TIMEOUT_MS);
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
            clx::Status st = tcp.poll();
            if (st == clx::Status::Ok) {
                Serial.println("TCP connected; opening session.");
                st = session.open(tcp, SESSION_TIMEOUT_MS);
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
                phase = Phase::ConnOps;
                phaseStarted = millis();
            } else if (st == clx::Status::Timeout || st == clx::Status::Error) {
                Serial.printf("RegisterSession failed: %s\n", clx::statusString(st));
                phase = Phase::RetryWait;
                phaseStarted = millis();
            }
            break;
        }
        case Phase::ConnOps: {
            if (!stepStarted) {
                startStep();
            } else {
                clx::Status st = conn.poll();
                if (st == clx::Status::Ok) {
                    finishStep();
                } else if (st == clx::Status::Timeout || st == clx::Status::Error) {
                    Serial.printf("step %d failed: %s\n", step, clx::statusString(st));
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
                tcp.close();
                phase = Phase::RetryWait;
                phaseStarted = millis();
            } else if (st == clx::Status::Timeout || st == clx::Status::Error) {
                session.abort();
                tcp.close();
                phase = Phase::RetryWait;
                phaseStarted = millis();
            }
            break;
        }
        case Phase::RetryWait:
        default:
            session.abort();
            tcp.close();
            if (millis() - phaseStarted >= RETRY_DELAY_MS) {
                phase = Phase::WaitEthernet;
                phaseStarted = millis();
            }
            break;
    }
    delay(10);
}

