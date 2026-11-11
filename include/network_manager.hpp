#pragma once

#include <ETH.h>
#include <WiFi.h>
#include "app_config.hpp"

class NetworkManager {
 public:
  static NetworkManager &instance();

  bool begin(uint32_t timeoutMs = 10000);
  bool isConnected() const;
  bool hasLink() const;
  IPAddress localIP() const;
  uint32_t linkSpeedMbps() const;
  const char *macString() const;

  /// Arranca el cliente SNTP para poner el reloj del sistema en hora (UTC), que
  /// es lo que mbedTLS necesita para validar las fechas del certificado del
  /// broker. No bloquea y no toca el DS3231, que guarda hora local y sigue
  /// siendo la fuente de los timestamps de datos.
  ///
  /// Llamar solo desde `setup()`, con la red lista y antes de crear las tareas.
  void beginTimeSync();

  /// Espera hasta `timeoutMs` a que el reloj quede en hora. Si no llega, se
  /// continúa igual: se valida la cadena del certificado pero no las fechas.
  bool waitForSystemClock(uint32_t timeoutMs = kNtpSyncTimeoutMs);

  /// true si el reloj del sistema ya tiene hora real (no el 1970 del arranque).
  static bool systemClockIsValid();

  /// true si la EEPROM pide IP fija pero este arranque usa DHCP de respaldo
  /// porque con la IP fija no se alcanzó el broker (ver kNetStaticFallbackTimeoutMs).
  bool dhcpFallbackActive() const { return dhcpFallbackActive_; }

  /// Supervisa que la red con la que arrancó el equipo llegue al broker.
  /// Llamar en cada iteración de la tarea MQTT (dueña del cliente) con el estado
  /// actual de la conexión. Si con IP fija, o con el DHCP de respaldo, no se
  /// conecta nunca en kNetStaticFallbackTimeoutMs de enlace arriba, reinicia el
  /// ESP32 alternando a la otra configuración (una sola vez por arranque en frío).
  void superviseStaticIp(bool brokerConnected);

  /// Rearma el respaldo a DHCP. Llamar al guardar una configuración de red nueva,
  /// antes del reinicio, para que la IP fija nueva tenga su propio intento.
  void resetStaticFallback();

 private:
  NetworkManager() = default;
  static void handleEvent(WiFiEvent_t event);

  volatile bool connected_ = false;
  volatile bool linkUp_ = false;

  // Respaldo DHCP (estado del arranque en curso; el que cruza reinicios vive en RTC).
  bool staticConfigured_ = false;   // la EEPROM pide IP fija
  bool dhcpFallbackActive_ = false; // este arranque usa DHCP de respaldo
  bool brokerReached_ = false;      // ya se conectó al broker en este arranque
  uint32_t linkUpTicks_ = 0;        // ticks con enlace arriba sin broker
  TickType_t lastSuperviseTick_ = 0;

  // El cliente SNTP se arranca una sola vez (ver beginTimeSync)
  bool timeSyncStarted_ = false;
};
