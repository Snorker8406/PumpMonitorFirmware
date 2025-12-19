# DECISION.md  
## Decisiones de Arquitectura y Buenas Prácticas – ESP32 v2

### Propósito del documento
Este documento establece las **decisiones técnicas y de diseño obligatorias** para el desarrollo de la versión **v2** del proyecto ESP32.  
Su objetivo es garantizar **mantenibilidad, escalabilidad, testabilidad y claridad arquitectónica**, así como servir de **guía permanente** para desarrolladores y herramientas de asistencia por IA.

---

## 1. Reescritura desde cero (Clean Rewrite)

- La versión v2 se desarrolla **desde cero**.
- **El código nuevo no debe importar, incluir ni depender de ningún archivo del proyecto legacy.**
- No se permite copiar y pegar código del legacy, ni parcial ni totalmente.

**Justificación:**  
El legacy presenta acoplamientos implícitos, lógica distribuida en el `loop()` y patrones difíciles de escalar. Reutilizar código impediría una arquitectura limpia y sostenible.

---

## 2. Rol del código legacy

- El proyecto legacy se conserva **exclusivamente como referencia funcional**.
- El legacy actúa como **ground truth de comportamiento**, no como referencia de diseño.
- Su uso permitido es:
  - Verificar flujos funcionales.
  - Confirmar protocolos, formatos de mensajes y timings esperados.
  - Comparar resultados observables (inputs / outputs).

**Uso prohibido:**
- Copiar estructuras de control, patrones de concurrencia o arquitectura.
- Reproducir el “mega loop” basado en `millis()`.

---

## 3. Separación estricta por capas

La arquitectura de v2 debe respetar una separación clara de responsabilidades:

- **Drivers**  
  Acceso directo a hardware (GPIO, UART, SPI, I2C, SD, RTC, Ethernet, WiFi).

- **Services**  
  Lógica de dominio (MQTT, Modbus, almacenamiento, sincronización, reglas).

- **Application / Orchestration**  
  Coordinación de servicios, estados del sistema, flujos de alto nivel.

**Reglas:**
- Drivers no conocen servicios.
- Servicios no acceden directamente al hardware.
- La aplicación no contiene lógica de bajo nivel.

---

## 4. Concurrencia y asincronía controlada

- No se permite lógica distribuida basada en `millis()` dispersa en múltiples archivos.
- El control temporal debe ser **centralizado** mediante:
  - Scheduler cooperativo **o**
  - FreeRTOS (tareas, timers, colas, event groups).

**Principios:**
- No usar `delay()` en código productivo.
- Las tareas deben ser:
  - No bloqueantes.
  - Con responsabilidades claras.
  - Comunicadas mediante mecanismos explícitos (queues/events).

---

## 5. Estado explícito y predecible

- El sistema debe manejar su estado de forma explícita:
  - FSM (Finite State Machine) cuando aplique.
  - Variables de estado bien definidas.
- Evitar flags implícitos, dependencias temporales ocultas o efectos colaterales.

---

## 6. Encapsulamiento total del I/O

- Todo acceso a hardware debe estar encapsulado en módulos dedicados.
- Está prohibido acceder directamente a GPIO, UART, SPI, etc., desde la lógica de aplicación.
- El hardware debe ser intercambiable sin romper la lógica de negocio.

---

## 7. Gestión de configuración y constantes

- No se permiten “magic numbers”.
- Toda configuración debe centralizarse:
  - Archivos de configuración.
  - Estructuras claras.
- La configuración no debe mezclarse con lógica de negocio.

---

## 8. Uso responsable de variables globales

- Evitar variables globales mutables.
- Preferir:
  - Inyección de dependencias.
  - Objetos propietarios del estado.
- Las excepciones deben estar justificadas y documentadas.

---

## 9. Logging y diagnóstico

- El sistema debe contar con un mecanismo de logging centralizado.
- Los logs deben permitir:
  - Diagnóstico en campo.
  - Comparación de comportamiento entre legacy y v2.
- No usar `Serial.print` disperso como mecanismo principal de logging.

---

## 10. Testabilidad y validación

- Cada módulo debe poder validarse de forma aislada.
- La funcionalidad de v2 debe contrastarse contra:
  - Los requisitos documentados.
  - El comportamiento observable del legacy.
- Cualquier diferencia funcional debe documentarse explícitamente.

---

## 11. Documentación como parte del código

- Las decisiones arquitectónicas no deben vivir solo en la cabeza del desarrollador.
- Este documento es **normativo**, no descriptivo.
- Cambios relevantes deben reflejarse aquí antes o durante su implementación.

---

## 12. Uso de IA como asistente

- Las herramientas de IA deben:
  - Seguir este documento como regla base.
  - Priorizar claridad, separación de responsabilidades y mantenibilidad.
  - Usar el legacy solo para confirmar comportamiento, nunca para replicar diseño.

---

## 13. Regla final

> **Si una decisión compromete la claridad, mantenibilidad o testabilidad del sistema, la decisión es incorrecta, incluso si “funciona”.**

---