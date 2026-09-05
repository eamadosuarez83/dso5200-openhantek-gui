# Puerto de la GUI de OpenHantek6022 al DSO-5200/5200A

Este es un fork de OpenHantek6022 al que se le está reemplazando la capa de
comunicación con el osciloscopio (pensada para la familia DSO-6022) por el
protocolo del DSO-5200/5200A, reutilizando toda la interfaz gráfica (ventana
principal, docks, render OpenGL del scope, exportadores, post-procesado).

El protocolo del DSO-5200/5200A en sí (tablas de timebase, V/división, relés,
GND) ya estaba resuelto y validado contra hardware real en el proyecto
hermano `../dso5200basic/` (CLI sin Qt); este fork porta esa lógica ya probada
a una clase de control nueva que habla con la GUI de OpenHantek6022.

## Qué se hizo

- **`src/dso5200/idsocontrol.h`**: interfaz abstracta `IDsoControl`, extraída
  del `HantekDsoControl` original (misma lista exacta de slots/señales). Tanto
  `HantekDsoControl` (familia 6022, sin tocar) como la clase nueva la
  implementan, para que `mainwindow.cpp`/`main.cpp`/
  `usb/devicereconnectionsupervisor.cpp` puedan manejar cualquiera de las dos
  sin saber cuál está conectada.
- **`src/dso5200/dso5200dsocontrol.{h,cpp}`**: `Dso5200DsoControl`, la clase
  de control real para el DSO-5200/5200A. La lógica de muestreo/ganancia/
  disparo está portada casi textual desde `Dso5200Control` de
  `dso5200basic` (ya validada contra hardware). Corre su propio hilo de
  captura (no usa el `CapturingThread` del 6022, que asume su protocolo de
  streaming continuo).
- **`src/dso5200/modelDSO5200.{h,cpp}`**: definición del modelo (VID/PID,
  tabla real de V/división 10V..10mV, acoples DC/AC/GND, samplerates).
- **`src/dso5200/protocol/`**: los structs de comandos bulk/control del
  DSO-5200, copiados de `dso5200basic/src/protocol/`, en su propio namespace
  `Hantek5200` (necesario: el namespace `Hantek` y las clases
  `ControlCommand`/`BulkCommand` del 6022 tienen el mismo nombre y ambos
  conviven en el mismo binario).
- Cambios de soporte: `ScopeDevice::bulkTransfer/bulkWrite/bulkRead<T>` ahora
  son públicos (el 6022 nunca los necesitó); `DSOModel` tiene un flag
  `twoStageFirmware` porque el DSO-5200 necesita subir loader + firmware en
  dos etapas (chip FX2 puro), a diferencia del 6022 (FX2LP, una etapa); se
  copiaron los `.hex` de firmware desde `dso5200basic/firmware/`.

Todo el stack del 6022 (`hantekdso/`, `hantekprotocol/`, modelos, modo demo)
queda intacto — es una convivencia, no un reemplazo total, para no arriesgar
lo que ya funcionaba.

## Estado verificado (2026-09-05, con DSO-5200A real conectado)

- Compila limpio (`cmake` + `make`, Qt 6.11.1).
- Detección del dispositivo sin firmware (`04b4:520a`), subida de firmware en
  dos etapas, y re-enumeración correcta como `04b5:520a` "DSO-5200A" — visto
  tanto en `lsusb` como en el log de la app.
- La app conecta, crea la ventana principal, inicializa OpenGL (Mesa 4.6),
  aplica ajustes iniciales (samplerate, canales activos) y corre sin errores
  de comunicación.
- Cierre limpio de la app (sin crashes) tras varias corridas de decenas de
  segundos con el osciloscopio real conectado.

## Problema encontrado hoy: proporción panel de controles / gráfica

Con el layout por defecto (sin estado de ventana guardado — primera vez que
corre este perfil), el panel de controles de la derecha ocupaba una
proporción mucho mayor de lo esperado (hasta ocupar casi todo el ancho en
algunas corridas), dejando la gráfica reducida a una franja angosta a la
izquierda. Se confirmó que **no es específico del DSO-5200**: el mismo efecto
(aunque menos extremo, ~50/50 en vez del ~20/80 esperado) se ve también en
modo demo con la GUI original sin tocar, en esta pantalla de 1366×768.

Causa identificada: los layouts internos de los docks (`VoltageDock`,
`HorizontalDock`, etc.) usan columnas con "stretch" para sus comboboxes, y el
widget contenedor de cada dock tiene política de tamaño `Minimum` (no
`Fixed`), así que cuando `QMainWindow` reparte espacio sobrante (por ejemplo
al no haber estado de ventana guardado), esas columnas lo absorben en vez de
cedérselo a la gráfica central.

**Mitigación aplicada** (`src/mainwindow.cpp`, después de crear los cuatro
docks): se les puso un `setMaximumWidth(300)` para que no puedan crecer más
allá de eso sin importar cuánto espacio sobrante reparta la ventana. Esto se
probó en modo demo pero **no se terminó de confirmar visualmente con el
osciloscopio real** — quedó pendiente para la próxima sesión.

## Pendiente para la próxima sesión

- El usuario reportó "problemas" adicionales usando la GUI con el
  osciloscopio real conectado, sin detallar cuáles — a revisar juntos.
- Confirmar si el `setMaximumWidth(300)` deja una proporción razonable
  (~20-25% controles / resto gráfica) con hardware real conectado.
- Probar de punta a punta: cambio de ganancia/V-div, cambio de base de
  tiempo, disparo, y que la forma de onda real se vea correctamente en
  pantalla (hasta ahora sólo se confirmó que la app conecta, sube firmware y
  no crashea — no se confirmó visualmente una captura real en la gráfica).
- Sin verificar: el bit de acople DC/AC cuando el modo es GND (ver comentario
  en `Dso5200DsoControl::applyRelays()` — se dejó en DC como supuesto sin
  confirmar contra hardware).
- No portados (fuera de alcance por ahora, sin equivalente en este hardware):
  calibración EEPROM/en vivo, modo roll, canal matemático, generador de
  frecuencia de calibración.

## Cómo compilar y correr

```sh
cd dso5200gui
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./openhantek/OpenHantek                # con el osciloscopio real conectado
./openhantek/OpenHantek --demoMode     # sin hardware, señales sintéticas (familia 6022 solamente)
```

Requiere las reglas udev para acceso sin root (`utils/udev_rules/`, o las que
ya tenía instaladas `dso5200basic` en `/etc/udev/rules.d/`, que cubren los
mismos VID/PID 04b4/04b5 : 5200/520a).
