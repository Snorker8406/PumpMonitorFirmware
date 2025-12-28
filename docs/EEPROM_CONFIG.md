# Sistema de Configuración en EEPROM

## Descripción General

El sistema utiliza la biblioteca **Preferences** de ESP32 (más robusta que EEPROM) para almacenar configuraciones persistentes en memoria no volátil. La configuración sobrevive reinicios y cortes de energía.

## Configuraciones Disponibles

### 1. Master Web Service URL

**Propósito**: URL del servicio web principal para comunicación HTTP/HTTPS

**Valor por defecto**: `https://pumpmonitor.agrotecsa.com.mx/`

**Ubicación en memoria**: Namespace `pumpmon`, key `masterUrl`

**Límite**: 128 caracteres

## Inicialización

Al arrancar el sistema:

1. **Primera vez** (EEPROM vacía):
   ```
   EEPROM: Initialized
   EEPROM: Master URL set to default: https://pumpmonitor.agrotecsa.com.mx/
   Master URL: https://pumpmonitor.agrotecsa.com.mx/
   ```

2. **Arranques subsecuentes** (valor guardado):
   ```
   EEPROM: Initialized
   EEPROM: Master URL loaded: https://ejemplo.com/api
   Master URL: https://ejemplo.com/api
   ```

## Actualización Dinámica vía MQTT

### Topic de Control

El sistema se suscribe automáticamente al topic:
```
device/{MAC}/masterWebService
```

Donde `{MAC}` es la dirección MAC del dispositivo sin ":" (ej: `AABBCCDDEEFF`)

### Actualizar la URL

**Publicar al topic**:
```
device/AABBCCDDEEFF/masterWebService
```

**Payload** (solo la URL):
```
https://nuevo-servidor.ejemplo.com/api/v2
```

**Logs al recibir actualización**:
```
MQTT msg [device/AABBCCDDEEFF/masterWebService]: https://nuevo-servidor.ejemplo.com/api/v2
EEPROM: Master URL updated: https://nuevo-servidor.ejemplo.com/api/v2
MQTT: Master URL updated via MQTT
```

### Validaciones

El sistema valida:
- ✅ URL no vacía
- ✅ Longitud máxima 128 caracteres
- ✅ EEPROM inicializado correctamente

Si alguna validación falla:
```
EEPROM: Invalid URL (empty)
MQTT: Failed to update Master URL
```

## API de Programación

### Obtener URL actual
```cpp
auto &eeprom = EepromManager::instance();
String url = eeprom.getMasterWebServiceURL();
```

### Actualizar URL programáticamente
```cpp
auto &eeprom = EepromManager::instance();
if (eeprom.setMasterWebServiceURL("https://nueva-url.com")) {
  // Éxito
} else {
  // Error
}
```

### Resetear a valores por defecto
```cpp
auto &eeprom = EepromManager::instance();
eeprom.resetToDefaults(); // Borra todo y restaura URL por defecto
```

## Estructura de Datos

### EepromManager
```cpp
class EepromManager {
 public:
  static EepromManager &instance();  // Singleton
  void begin();                      // Inicializar
  String getMasterWebServiceURL();   // Leer URL
  bool setMasterWebServiceURL(const char* url);  // Actualizar URL
  void resetToDefaults();            // Resetear todo
};
```

### Storage Backend
- **Biblioteca**: `Preferences` (ESP32)
- **Namespace**: `pumpmon`
- **Modo**: Lectura/Escritura
- **Persistencia**: NVS (Non-Volatile Storage)

## Topics MQTT Suscritos

Al conectarse a MQTT, el sistema automáticamente se suscribe a:

1. `device/{MAC}` - Topic general del dispositivo
2. `device/{MAC}/masterWebService` - Actualización de URL

## Ventajas del Sistema

1. **Persistencia**: Configuración sobrevive reinicios y cortes de energía
2. **Configuración Remota**: Cambio de URL sin necesidad de reprogramar
3. **Validación**: Previene escritura de valores inválidos
4. **Logging Completo**: Todas las operaciones se registran
5. **Defaults Seguros**: Valor por defecto útil en primera ejecución
6. **Error Handling**: Manejo robusto de errores de lectura/escritura

## Expansión Futura

El sistema está diseñado para agregar fácilmente más configuraciones:

```cpp
// En eeprom_manager.hpp
static constexpr const char* kKeyIntervalMs = "intervalMs";
static constexpr uint32_t kDefaultIntervalMs = 60000;

// En eeprom_manager.cpp
uint32_t EepromManager::getModbusIntervalMs() {
  return prefs_.getUInt(kKeyIntervalMs, kDefaultIntervalMs);
}

bool EepromManager::setModbusIntervalMs(uint32_t interval) {
  return prefs_.putUInt(kKeyIntervalMs, interval) > 0;
}
```

## Troubleshooting

### EEPROM no inicializa
```
EEPROM: Failed to initialize Preferences
```
**Causa**: Corrupción de NVS o problema de hardware
**Solución**: Borrar partición NVS con `esptool.py erase_flash`

### URL no se guarda
```
EEPROM: Failed to write Master URL
```
**Causa**: Espacio NVS agotado o partición de solo lectura
**Solución**: Verificar configuración de particiones en platformio.ini

### URL demasiado larga
```
EEPROM: URL too long (max 128 chars)
```
**Solución**: Reducir longitud de URL o aumentar `kMaxUrlLength`
