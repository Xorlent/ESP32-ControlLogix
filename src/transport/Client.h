#pragma once

#include <ETH.h>
#include <IPAddress.h>
#include "Status.h"

namespace clx {

/*
 * Non-blocking Ethernet client over the ESP32 ETH driver.
 *
 * begin() starts the ETH driver with a static IPv4 configuration and returns
 * immediately. poll() advances link/IP state; it returns Pending until the
 * interface is ready, Ok once it has the configured address, or Error on
 * failure. No call blocks.
 *
 * The PHY type, SPI pins, and IPv4 settings are all supplied by the sketch via
 * Config; the defaults below match the M5Stack AtomS3 + AtomPoE (W5500).
 */
class Client {
public:
    // Ethernet configuration. The sketch supplies these values; the defaults
    // below match the M5Stack AtomS3 + AtomPoE (W5500) and should be overridden
    // for other hardware.
    struct Config {
        eth_phy_type_t phyType = ETH_PHY_W5500;  // PHY type (from ETH.h)
        IPAddress ip{192, 168, 1, 50};
        IPAddress gateway{192, 168, 1, 1};
        IPAddress subnet{255, 255, 255, 0};
        IPAddress dns{192, 168, 1, 1};
        int sck = 5;     // SPI clock
        int miso = 7;    // SPI MISO
        int mosi = 8;    // SPI MOSI
        int cs = 6;      // chip select (SPI PHYs)
        int irq = -1;    // interrupt (SPI PHYs; unused on AtomPoE)
        int rst = -1;    // reset (SPI PHYs; unused on AtomPoE)
        int phyAddr = 1; // PHY address
        // RMII PHYs (e.g. LAN8720) on boards with the built-in EMAC.
        bool rmii = false;  // true = RMII PHY, false = SPI PHY
        int mdc = 23;       // MDC pin (RMII)
        int mdio = 18;      // MDIO pin (RMII)
        int power = -1;     // power pin (RMII; -1 = unused)
        int clkMode = 0;    // eth_clock_mode_t value (0 = ETH_CLOCK_GPIO0_IN)
    };

    Client() = default;
    ~Client();

    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    // Start Ethernet setup and return immediately.
    Status begin(const Config &cfg);

    // Advance link/IP state. Returns Pending until ready, Ok once the
    // configured static address is live, or Error on failure.
    Status poll();

    // True once link + IP are ready.
    bool ready() const { return ready_; }

    // Local IPv4 address (0.0.0.0 until ready()).
    IPAddress localIP() const;

    // Subnet mask (0.0.0.0 until ready()).
    IPAddress subnetMask() const;

private:
    Config cfg_;
    bool started_ = false;
    bool ready_ = false;
    bool failed_ = false;
};

}  // namespace clx
