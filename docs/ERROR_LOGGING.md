# Sistema de Logging de Errores a SD

## Descripción

El firmware ahora incluye un sistema automático de logging que guarda todos los errores y advertencias en archivos de texto en la tarjeta SD.

## Estructura de Archivos

### Carpetas:
- `/data` - Datos de sensores (CSV)
- `/error` - Logs de errores (TXT)

### Nomenclatura de Archivos de Error:
```
/error/err_YYYYMMDD.txt
```

Ejemplos:
- `/error/err_20251226.txt` - Errores del 26 de diciembre de 2025
- `/error/err_20251227.txt` - Errores del 27 de diciembre de 2025

## Formato de Log

Cada línea en el archivo de error tiene el siguiente formato:

```
[TIMESTAMP] [NIVEL] Mensaje
```

Ejemplo:
```
[2025-12-26 10:30:45] [ERROR] SD initialization failed
[2025-12-26 10:31:15] [WARN] Modbus read failed at offset 20 (unitId=1, regType=HOLD, ip=192.168.1.10)
[2025-12-26 10:32:00] [ERROR] Failed to read Device_1 (192.168.1.10)
```

## Niveles de Log Guardados

Solo se guardan en SD:
- **ERROR** (E) - Errores críticos que impiden funcionamiento
- **WARN** (W) - Advertencias de problemas no críticos

Los niveles INFO y DEBUG solo se muestran en el monitor serial.

## Logs Implementados por Componente

### Network Manager
- ❌ ERROR: Fallo al inicializar Ethernet
- ❌ ERROR: Timeout de conexión Ethernet
- ⚠️ WARN: Pérdida de enlace Ethernet

### Modbus Manager
- ❌ ERROR: Fallo de conexión a dispositivo Modbus
- ❌ ERROR: Fallo de lectura en offset específico (con IP, unitId, tipo de registro)
- ❌ ERROR: Fallo de lectura de dispositivo completo (con nombre e IP)
- ❌ ERROR: Fallo de lectura de todos los dispositivos

### MQTT Manager
- ❌ ERROR: Fallo de conexión a broker (con detalles: RC, broker, puerto, usuario)
- ⚠️ WARN: Desconexión MQTT

### SD Manager
- ⚠️ WARN: SD ya inicializada
- ❌ ERROR: Fallo de inicialización SD
- ❌ ERROR: Tarjeta SD no detectada
- ⚠️ WARN: No se pudo crear directorio de datos
- ⚠️ WARN: No se pudo crear directorio de errores
- ❌ ERROR: Fallo al abrir archivo para escritura
- ❌ ERROR: Fallo al escribir batch de datos

### RTC Manager
- ⚠️ WARN: RTC ya inicializado
- ❌ ERROR: RTC no encontrado
- ⚠️ WARN: RTC perdió alimentación

### Main (Tareas)
- ❌ ERROR: Fallo de inicialización de tarea SD
- ⚠️ WARN: SD no disponible, reintentando
- ❌ ERROR: Device read failed (para cada dispositivo)
- ❌ ERROR: Fallo al escribir registros en SD (con cantidad)
- ❌ ERROR: Fallo de lectura Modbus general

## Uso Manual

Aunque los logs se escriben automáticamente con `LOGE()` y `LOGW()`, también puedes escribir logs manualmente:

```cpp
#include "sd_manager.hpp"

auto &sd = SdManager::instance();

// Escribir log simple
sd.writeErrorLog("ERROR", "Custom error message\n");

// Escribir log formateado
sd.writeErrorLogFormatted("WARN", "Value out of range: %d\n", value);
```

## Ventajas del Sistema

1. **Persistencia**: Los errores se guardan incluso si el dispositivo se reinicia
2. **Análisis post-mortem**: Puedes revisar qué salió mal sin monitor serial
3. **Organización**: Un archivo por día facilita la búsqueda
4. **Timestamps precisos**: Con RTC, sabes exactamente cuándo ocurrió cada error
5. **Automático**: No requiere código adicional, funciona con macros existentes

## Monitoreo Remoto

Los archivos de error en la SD pueden ser:
- Descargados periódicamente para análisis
- Enviados por MQTT (implementación futura)
- Revisados mediante un servidor web en el ESP32 (implementación futura)

## Consideraciones

- Los archivos de error NO se rotan automáticamente
- Considera implementar limpieza de archivos antiguos (>30 días)
- Cada escritura abre/cierra el archivo (seguro pero no óptimo para alta frecuencia)
- El sistema es thread-safe: solo una tarea escribe a la vez

## Ejemplo de Archivo de Error Completo

```
[2025-12-26 08:00:00] [ERROR] RTC not found!
[2025-12-26 08:00:05] [ERROR] SD initialization failed
[2025-12-26 08:00:15] [ERROR] ETH.begin() failed - check hardware configuration
[2025-12-26 08:05:30] [WARN] RTC lost power, time may be incorrect!
[2025-12-26 08:05:45] [ERROR] Modbus connection failed to 192.168.1.10
[2025-12-26 08:06:00] [ERROR] Failed to read Device_1 (192.168.1.10)
[2025-12-26 08:10:15] [ERROR] MQTT connect failed rc=-2 (broker=mqtt.agrotecsa.com.mx:8883, user=PM_AABBCCDDEEFF)
[2025-12-26 10:30:22] [WARN] SD not available, retrying initialization...
[2025-12-26 10:30:27] [ERROR] SD: failed to write 2 records
```

Este log muestra claramente la secuencia de eventos y permite diagnosticar problemas sin necesidad de estar conectado al monitor serial.
