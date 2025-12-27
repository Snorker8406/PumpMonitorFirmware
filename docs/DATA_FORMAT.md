# Formato de Almacenamiento de Datos

## Descripción General

El sistema ahora guarda los datos de Modbus en formato **hexadecimal crudo** en archivos de texto (.txt), preservando los valores raw de los registros sin procesamiento.

## Formato de Archivo

### Ubicación
```
/data/YYYYMMDD.txt
```

### Formato de Línea
```
{timestamp},{device_name},{hexadecimales}
```

**Ejemplo:**
```
1735689234,RTU_M,1234ABCD5678EF0012340000FFFF
1735689234,RTU_B,5678DEAD00BEEFCAFE1234567890AB
```

### Campos

1. **timestamp**: Unix timestamp (segundos desde epoch)
2. **device_name**: Nombre del dispositivo Modbus
3. **hexadecimales**: Cadena continua de valores hexadecimales de 4 dígitos por registro

## Detalles Técnicos

### Estructura de Datos

#### ModbusDeviceData
```cpp
struct ModbusDeviceData {
  const char* name;
  IPAddress ip;
  std::vector<float> values;      // Valores procesados (para display)
  std::vector<uint16_t> rawData;  // Datos crudos en hexadecimal
  bool success;
};
```

#### SensorDataRecord
```cpp
struct SensorDataRecord {
  unsigned long timestamp;
  const char* deviceName;
  IPAddress deviceIp;
  std::vector<float> values;      // Valores procesados (para display)
  std::vector<uint16_t> rawData;  // Datos crudos en hexadecimal
};
```

### Proceso de Guardado

1. **Lectura Modbus**: Se leen hasta 120 registros por dispositivo en chunks de 60
2. **Almacenamiento Dual**:
   - `rawData`: Preserva valores uint16_t sin modificar
   - `values`: Convierte a float para display/logs
3. **Formato Hexadecimal**: Cada registro se escribe como 4 dígitos hexadecimales (ej: `1234`)
4. **Escritura en Batch**: Todos los dispositivos se escriben en una operación por ciclo

### Configuración de Dispositivos

Los dispositivos se configuran en `app_config.hpp`:

```cpp
const ModbusDeviceConfig kModbusDevices[] = {
  {"RTU_M", IPAddress(192,168,1,20), 1, 0, 120, HOLDING_REGISTER},
  {"RTU_B", IPAddress(192,168,1,21), 1, 0, 120, HOLDING_REGISTER}
};
```

## Ventajas del Formato Hexadecimal

1. **Precisión**: No hay pérdida de información por conversión a float
2. **Compacto**: Más eficiente que CSV con valores decimales
3. **Debugging**: Facilita análisis de protocolos Modbus
4. **Reversible**: Los valores raw pueden ser reinterpretados posteriormente
5. **Integridad**: Los datos originales permanecen sin modificación

## Recuperación de Datos

Para interpretar los datos hexadecimales:

```python
# Ejemplo en Python
hex_string = "1234ABCD5678"
registers = []

# Dividir en registros de 4 dígitos hex
for i in range(0, len(hex_string), 4):
    reg_hex = hex_string[i:i+4]
    reg_value = int(reg_hex, 16)
    registers.append(reg_value)

# Convertir pares a float si es necesario
for i in range(0, len(registers), 2):
    if i + 1 < len(registers):
        # Reconstruir float desde dos registros uint16_t
        high = registers[i]
        low = registers[i+1]
        # Aplicar conversión según el encoding usado
```

## Migración desde CSV

### Formato Anterior
```
timestamp,device_name,device_ip,values
1735689234,RTU_M,192.168.1.20,"12.34,56.78,90.12"
```

### Formato Nuevo
```
1735689234,RTU_M,1234ABCD5678EF00
```

**Ventajas del cambio:**
- Sin header (ahorro de espacio)
- Sin necesidad de escapar comillas
- Datos más compactos
- Mayor fidelidad de datos

## Notas de Implementación

- El timestamp proviene del RTC DS3231 para máxima precisión
- Los archivos rotan diariamente de forma automática
- El sistema mantiene compatibilidad con logs de error en formato separado
- Los valores float se siguen calculando en memoria para display/logs pero no se guardan
