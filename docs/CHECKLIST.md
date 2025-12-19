# CHECKLIST.md  
## Checklist de Revisión – ESP32 v2

Esta checklist debe cumplirse para:
- Pull Requests
- Código generado por IA
- Revisiones manuales

---

## 1. Reglas fundamentales

- [ ] El código **no importa ni depende del legacy**
- [ ] El legacy solo se usó como referencia de comportamiento
- [ ] Se respetan `DECISION.md` y `ARCHITECTURE.md`

---

## 2. Arquitectura y diseño

- [ ] El código pertenece claramente a una capa (driver/service/app)
- [ ] No hay acceso directo a hardware desde la lógica de negocio
- [ ] No existen dependencias cruzadas entre capas

---

## 3. Concurrencia y timing

- [ ] No se usa `delay()`
- [ ] No hay lógica temporal dispersa con `millis()`
- [ ] El timing es centralizado (FreeRTOS / scheduler)
- [ ] Las tareas no bloquean innecesariamente

---

## 4. FreeRTOS (si aplica)

- [ ] Cada tarea tiene responsabilidad clara
- [ ] Comunicación vía queues/events
- [ ] Afinidad de núcleo justificada
- [ ] Stack size razonable y documentado

---

## 5. Estado y lógica

- [ ] El estado del sistema es explícito
- [ ] No existen flags implícitos dependientes del orden
- [ ] FSM documentada si aplica

---

## 6. Código y calidad

- [ ] No hay magic numbers
- [ ] Configuración separada de lógica
- [ ] Variables globales justificadas o evitadas
- [ ] Código legible y mantenible

---

## 7. Logging y diagnóstico

- [ ] Logging consistente y centralizado
- [ ] Logs útiles para comparar v2 vs legacy
- [ ] No uso de `Serial.print` disperso

---

## 8. Validación funcional

- [ ] Cumple requisitos definidos
- [ ] Comportamiento comparable con legacy
- [ ] Diferencias documentadas

---

## Regla final

> Si no pasa esta checklist, **no se integra**.