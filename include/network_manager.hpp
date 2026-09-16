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

 private:
  NetworkManager() = default;
  static void handleEvent(WiFiEvent_t event);

  volatile bool connected_ = false;
  volatile bool linkUp_ = false;

  // El cliente SNTP se arranca una sola vez (ver beginTimeSync)
  bool timeSyncStarted_ = false;
};
