#include "Client.h"

#include <Arduino.h>
#include <SPI.h>
#include <ETH.h>

namespace clx {

Client::~Client() {
    if (started_) {
        ETH.end();
        started_ = false;
    }
    ready_ = false;
}

Status Client::begin(const Config &cfg) {
    if (started_) {
        ETH.end();
        started_ = false;
    }
    cfg_ = cfg;
    ready_ = false;
    failed_ = false;

    // Start the PHY driver first. This creates the netif that ETH.config()
    // requires, so begin() must precede config().
    bool ok;
#if CONFIG_ETH_USE_ESP32_EMAC
    if (cfg.rmii) {
        // RMII PHY (e.g. LAN8720) on a board with the built-in EMAC.
        ok = ETH.begin(cfg.phyType, cfg.phyAddr, cfg.mdc, cfg.mdio, cfg.power,
                       static_cast<eth_clock_mode_t>(cfg.clkMode));
    } else
#endif
    {
        // SPI PHY (e.g. W5500). The chip-select is managed by the ETH driver,
        // not by SPI's SS pin, so it is not passed to SPI.begin().
        SPI.begin(cfg.sck, cfg.miso, cfg.mosi);
        ok = ETH.begin(cfg.phyType, cfg.phyAddr, cfg.cs, cfg.irq, cfg.rst, SPI);
    }
    if (!ok) {
        failed_ = true;
        return Status::Error;
    }

    // Apply the static IPv4 configuration after the netif exists.
    if (!ETH.config(cfg.ip, cfg.gateway, cfg.subnet, cfg.dns)) {
        failed_ = true;
        return Status::Error;
    }

    started_ = true;
    return Status::Pending;
}

Status Client::poll() {
    if (failed_) {
        return Status::Error;
    }
    if (!started_) {
        return Status::NotReady;
    }
    if (ready_) {
        return Status::Ok;
    }
    if (ETH.linkUp() && ETH.hasIP()) {
        ready_ = true;
        return Status::Ok;
    }
    return Status::Pending;
}

IPAddress Client::localIP() const {
    return ready_ ? ETH.localIP() : IPAddress(0, 0, 0, 0);
}

IPAddress Client::subnetMask() const {
    return ready_ ? ETH.subnetMask() : IPAddress(0, 0, 0, 0);
}

}  // namespace clx
