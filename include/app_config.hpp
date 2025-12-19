#pragma once

#include <Arduino.h>
#include <ETH.h>

#ifndef ENABLE_DEBUG_LOG
#define ENABLE_DEBUG_LOG 1
#endif

constexpr char kDeviceHostname[] = "pump-monitor";

// WT32-ETH01 / LAN8720 wiring
constexpr int kEthAddr = 1;
constexpr int kEthPowerPin = 16;
constexpr int kEthMdcPin = 23;
constexpr int kEthMdioPin = 18;
constexpr eth_phy_type_t kEthPhy = ETH_PHY_LAN8720;
constexpr eth_clock_mode_t kEthClockMode = ETH_CLOCK_GPIO17_OUT;

// Opcional: red estática. Pon kUseStaticIp = true y actualiza los valores.
constexpr bool kUseStaticIp = false;
static const IPAddress kStaticIp(192, 168, 1, 120);
static const IPAddress kStaticGateway(192, 168, 1, 254);
static const IPAddress kStaticSubnet(255, 255, 255, 0);
static const IPAddress kStaticDns(8, 8, 8, 8);

// Task sizing
constexpr uint32_t kNetworkTaskStackWords = 4096;
constexpr UBaseType_t kNetworkTaskPriority = 1;
constexpr BaseType_t kNetworkTaskCore = PRO_CPU_NUM;  // Core 0 para red
constexpr uint32_t kNetworkTaskPeriodMs = 2000;
