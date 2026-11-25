# Puerto de la GUI de OpenHantek6022 al DSO-5200/5200A

Este es un fork de OpenHantek6022 al que se le reemplaza la capa de comunicación con el
osciloscopio (pensada para la familia DSO-6022) por el protocolo del DSO-5200/5200A,
reutilizando toda la interfaz gráfica (ventana principal, docks, render OpenGL, exportadores,
post-procesado).

## Arquitectura (desde 21-nov-2026, rama `nucleo-dso5200basic`)

El protocolo vive **solo** en el proyecto hermano `../dso5200basic/` (repo
eamadosuarez83/dso5200-cli), que es la referencia validada contra el aparato real. La GUI lo
compila tal cual; las mejoras del protocolo se hacen allí, una sola vez, y la GUI las recoge al
recompilar.

- **`openhantek/CMakeLists.txt`**: biblioteca estática `dso5200core` con las fuentes de
  `dso5200basic` (`device/dso5200.cpp`, `usb/usbdevice.cpp`, `protocol/*.cpp`), tomadas de
  `DSO5200BASIC_DIR` (por omisión `../../dso5200basic`). El namespace `Hantek` y
  `libUsbErrorString(int)`, que chocan con los del 6022, se renombran con macros solo en esas
  unidades.
- **`openhantek/dso5200core/dso5200core.{h,cpp}`**: puente con cabecera neutra (sin Qt ni tipos de
  `dso5200basic`). El núcleo abre su propia conexión USB; la GUI cierra la suya antes.
- **`src/dso5200/dso5200dsocontrol.{h,cpp}`**: `Dso5200DsoControl`, adaptador fino que implementa
  `IDsoControl` (`src/dso5200/idsocontrol.h`). Sin lógica de protocolo: traduce unidades de la
  sonda, convierte cada frame en `DSOsamples` y reenvía la frecuencia de muestreo real.
- **`src/dso5200/modelDSO5200.{h,cpp}`**: modelo (VID/PID, V/div 20 mV..10 V, DC/AC/GND).
- **El 6022 se quitó** (25-nov-2026, "Paso 3"; respaldo en la etiqueta `respaldo-antes-quitar-6022`):
  `HantekDsoControl`, hilo de captura, disparo por software y protocolo USB del 6022, sus modelos y
  firmwares, la compilación para Windows (dependía de sus drivers) y los documentos y la web del
  upstream. También los controles que no hacían nada con el 5200: frecuencia de la salida de
  calibración, suavizado del disparo, modos NORMAL/SINGLE, MATH como fuente de disparo, calibrar
  offset, comando manual y la casilla de "modificación AC". Del 6022 quedan las piezas comunes de la
  GUI (`hantekdso/`: modelo, especificación, muestras, canal MATH).

## Qué funciona (verificado con el DSO-5200A y la CAL, 0..2 V a 1 kHz)

- Subida de firmware, conexión y captura continua sin errores.
- V/div y base de tiempo: a 1 ms/div, 10 ciclos; mediciones de la GUI (Vpp, Vrms, frecuencia)
  coherentes con la CLI.
- Disparo por hardware en CH1 (nivel y pendiente).
- **Acoplamiento DC / AC / GND desde el desplegable de cada canal** (22-nov-2026, núcleo
  `a072f1b`): DC y AC mueven el relé real; GND es por software (el aparato no tiene relé de
  masa) y el canal se dibuja en 0 V. Arranque en DC. Hasta esa fecha todo se medía en AC sin
  saberlo.

- **Ver un solo canal**: los dos canales quedan siempre activos en el aparato y desmarcar uno en
  la GUI solo lo oculta (22-nov-2026). Con un solo canal activo en el hardware, el aparato entrega
  ese canal en la posición del búfer de CH1 (con otro cero y otra ganancia) y el núcleo lo leía en
  la suya: la traza se caía. Pendiente arreglarlo en `dso5200basic` (comando `channel 1|2` de la CLI).

- **Disparo desde CH2 por software** (22-nov-2026). El aparato solo dispara por hardware desde
  CH1 (dso5200basic `6a7da56`: ninguno de los otros valores del campo de fuente dispara). Con la
  fuente en CH2, el adaptador busca en cada captura el cruce del nivel con la pendiente elegida
  (histéresis 0.1 div) más cercano al centro del registro y lo marca como punto de disparo. Va
  bien cuando entran varios períodos en el registro (CAL de 1 kHz a 1 ms/div: el flanco cae a
  ±500 muestras del centro y la pantalla se llena). Con pocos períodos (200 µs/div: unos 2) el
  flanco elegido puede quedar lejos del centro y la traza "salta" con un hueco variable a un lado.

- **Canal MATH** (25-nov-2026): lo calcula el adaptador con `MathChannel` de la GUI (antes el hueco
  quedaba vacío). Funciona aunque CH1 o CH2 estén ocultos.
- **Modo demo** (25-nov-2026): `--demoMode` o el botón del diálogo inicial usan el simulador de
  dso5200basic (`src/device/simulator.cpp`, compilado sin cambios en `dso5200core`).

## Pendiente

- El núcleo solo ofrece el modo AUTO y el pre-disparo por hardware fijo al 50 % (la GUI desplaza la
  vista dentro del registro).
- Probar desconectar el USB con la GUI abierta (reconexión).

## Depuración

`DSO5200_CAL=1` (o `2`) al lanzar: imprime a stderr, por frame, las estadísticas crudas del ADC
del núcleo (`[cal]`) y las de las muestras que se entregan a la pantalla (`[gui]`), para comparar
con la CLI. Ojo: las estadísticas `[cal]` son del ADC crudo, así que en GND siguen mostrando la
señal real; `[gui]` muestra los 0 V.

## Cómo compilar y correr

```sh
cd dso5200gui
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release     # -DDSO5200BASIC_DIR=... si no está como carpeta hermana
make -j$(nproc)
./openhantek/OpenHantek                # con el osciloscopio real conectado
./openhantek/OpenHantek --demoMode     # sin hardware: simulador de dso5200basic
```

Requiere las reglas udev para acceso sin root (`utils/udev_rules/`, o las que ya tenía instaladas
`dso5200basic` en `/etc/udev/rules.d/`, que cubren los mismos VID/PID 04b4/04b5 : 5200/520a).
