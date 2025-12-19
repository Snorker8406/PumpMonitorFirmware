# ARCHITECTURE.md  
## Arquitectura del Sistema – ESP32 v2

### Propósito
Este documento describe **cómo está construido el sistema v2**, incluyendo:
- Modelo de concurrencia.
- Organización por capas.
- Estrategia de temporización.
- Uso de FreeRTOS y/o scheduler cooperativo.
- Reglas técnicas operativas.

Este documento **implementa las decisiones normativas** definidas en `DECISION.md`.

---

## 1. Visión general de la arquitectura

El sistema se organiza en capas estrictamente separadas:

+---------------------------+
| Application |
| (Orchestration / FSM) |
+---------------------------+
| Services |
| (MQTT, Modbus, Storage) |
+---------------------------+
| Drivers |
| (GPIO, UART, SD, Net) |
+---------------------------+
| ESP32 Hardware |
+---------------------------+

yaml
Copiar código

---

## 2. Modelo de concurrencia soportado

La arquitectura admite **dos modelos**, con reglas claras de uso.

---

## 3. Opción A – Scheduler cooperativo

### 3.1 Descripción
Modelo basado en ejecución periódica de tareas **no bloqueantes**, coordinadas por un scheduler central.

Ejemplo conceptual:
- Tick base: 1 ms o 10 ms
- Jobs registrados con período fijo
- Cada job ejecuta lógica corta y retorna

### 3.2 Cuándo usarlo
- Migración inicial desde legacy.
- Sistemas con baja latencia crítica.
- Menor complejidad mental.
- Debug más sencillo.

### 3.3 Reglas obligatorias
- Ningún job puede bloquear.
- Tiempo de ejecución predecible.
- Prohibido `delay()`.
- El scheduler es el **único dueño del tiempo**.
- Jobs no conocen entre sí; solo usan servicios.

---

## 4. Opción B – FreeRTOS (arquitectura preferente)

### 4.1 Descripción
Uso explícito de FreeRTOS para aislar responsabilidades y latencias.

### 4.2 Tareas recomendadas

| Task        | Responsabilidad principal      | Core sugerido |
|-------------|--------------------------------|---------------|
| NetTask     | Ethernet/WiFi, reconnect       | Core 0        |
| MqttTask    | MQTT publish/subscribe         | Core 0        |
| ModbusTask  | Modbus TCP/RTU                 | Core 1        |
| StorageTask | SD / filesystem                | Core 1        |
| AppTask     | FSM / orquestación             | Core 1        |

### 4.3 Comunicación entre tareas
- `QueueHandle_t` para datos.
- `EventGroup` para señales.
- Prohibido acceso directo entre tareas.

### 4.4 Reglas de uso de núcleos
- Core 0: comunicaciones y red.
- Core 1: lógica de negocio y control.
- Afinidad explícita (`xTaskCreatePinnedToCore`).

---

## 5. Temporización y timers

- Prohibido uso directo de `millis()` en lógica de negocio.
- Opciones permitidas:
  - Timers de FreeRTOS.
  - Scheduler cooperativo central.
- El tiempo es una **dependencia explícita**, no implícita.

---

## 6. Gestión de estado

- El sistema utiliza **estado explícito**:
  - FSM cuando aplique.
  - Estados claros de red, servicio y sistema.
- Prohibido estado implícito basado en orden temporal.

---

## 7. Estructura de carpetas sugerida

src/
app/
AppController.*
SystemState.*
services/
MqttService.*
ModbusService.*
StorageService.*
drivers/
GpioDriver.*
UartDriver.*
SdDriver.*
os/
Scheduler.*
Tasks.*
include/
lib/
docs/

yaml
Copiar código

---

## 8. Logging y diagnóstico

- Logging centralizado.
- Niveles: DEBUG, INFO, WARN, ERROR.
- Logs coherentes para comparar v2 vs legacy.

---

## 9. Decisión actual

> **Decisión vigente:**  
> El proyecto v2 adopta **FreeRTOS como arquitectura base**, con posibilidad de scheduler cooperativo interno dentro de tareas cuando sea útil.

Cualquier cambio debe documentarse y justificarse.