# RTC DS3231 - Guía de Uso

## Configuración de Hardware

El RTC DS3231 está conectado mediante I2C:
- **SDA**: Pin 33
- **SCL**: Pin 5

## Inicialización Automática

El RTC se inicializa automáticamente en el `setup()` del firmware. Al arrancar verás en el log:

```
RTC initialized | Time: 2025-12-26 10:30:45 | Temp: 25.50°C
```

## Configurar Fecha y Hora

Si el RTC perdió alimentación o la hora es incorrecta, puedes configurarla mediante código:

### Opción 1: Desde el código (temporal)

Agrega en `setup()` después de la inicialización del RTC:

```cpp
// Configurar fecha y hora: 26 de diciembre de 2025, 10:30:00
RtcManager::instance().setDateTime(2025, 12, 26, 10, 30, 0);
```

### Opción 2: Sincronización con servidor NTP (recomendado)

El firmware podría sincronizar automáticamente con un servidor NTP cuando tenga conexión a Internet (funcionalidad futura).

## Funciones Disponibles

```cpp
auto &rtc = RtcManager::instance();

// Obtener timestamp Unix (segundos desde 1970)
time_t unixTime = rtc.getUnixTime();

// Obtener fecha y hora actual
DateTime now = rtc.now();
Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n", 
              now.year(), now.month(), now.day(),
              now.hour(), now.minute(), now.second());

// Obtener temperatura del RTC (sensor integrado)
float temp = rtc.getTemperature();

// Verificar si perdió alimentación
if (rtc.lostPower()) {
    Serial.println("RTC perdió alimentación - configurar hora!");
}

// Obtener strings formateadas
String dateTime = rtc.getDateTimeString();  // "2025-12-26 10:30:45"
String date = rtc.getDateString();          // "2025-12-26"
String time = rtc.getTimeString();          // "10:30:45"
```

## Integración con SD Card

Los timestamps en los archivos CSV ahora usan el RTC:

```csv
timestamp,device_name,device_ip,values
1735208400,Device_1,192.168.1.10,"12.34,56.78"
```

El timestamp es Unix time (segundos desde 1970-01-01 00:00:00 UTC).

## Características del DS3231

- **Precisión**: ±2ppm (±1 minuto/año aproximadamente)
- **Batería**: Mantiene la hora con batería CR2032 cuando se pierde alimentación
- **Temperatura**: Sensor integrado con compensación automática
- **Alarmas**: Soporta 2 alarmas programables (no implementadas aún)

## Nombres de Archivos CSV

Los archivos se nombran automáticamente por fecha:
- `/data/20251226.csv` - Archivo del 26 de diciembre de 2025
- `/data/20251227.csv` - Archivo del 27 de diciembre de 2025

Cada día se crea un nuevo archivo automáticamente.
