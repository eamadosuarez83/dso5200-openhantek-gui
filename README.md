# dso5200-openhantek

Software de osciloscopio para el **Hantek DSO-5200 / DSO-5200A** en Linux: la interfaz gráfica de
[OpenHantek6022](https://github.com/OpenHantek/OpenHantek6022) con la comunicación con el aparato
reemplazada por el protocolo de [dso5200basic](https://github.com/eamadosuarez83/dso5200-cli)
(validado contra el aparato real).

Este fork solo soporta el DSO-5200/5200A: el código, los firmwares y los documentos de la familia
6022 se quitaron (quedan en la historia de git, etiqueta `respaldo-antes-quitar-6022`).

## Qué hay

- Captura en dos canales, V/div de 20 mV a 10 V, acoplamiento DC/AC (GND por software).
- Disparo por hardware desde CH1 y por software desde CH2.
- Canal MATH, espectro (FFT), mediciones, marcadores, exportación CSV/JSON.
- **Modo demo** (`--demoMode` o el botón del diálogo inicial): sin aparato, con el simulador de
  dso5200basic.

Detalle de lo verificado y lo pendiente: [DSO5200_PORT.md](DSO5200_PORT.md).

## Instalar (Manjaro / Arch)

```sh
cd packaging/manjaro
makepkg -si
```

Ver [packaging/manjaro/README.md](packaging/manjaro/README.md). Instala la GUI
(`dso5200-openhantek`), la CLI (`dso5200basic`) y las reglas udev.

## Compilar a mano

`dso5200basic` (repo `dso5200-cli`) tiene que estar como carpeta hermana de este repo:

```sh
git clone https://github.com/eamadosuarez83/dso5200-cli.git dso5200basic
git clone https://github.com/eamadosuarez83/dso5200-openhantek-gui.git dso5200gui
cmake -S dso5200gui -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/openhantek/OpenHantek          # con el osciloscopio conectado
./build/openhantek/OpenHantek -d       # modo demo
```

Dependencias: Qt 6 (base, OpenGL widgets, tools), FFTW3, libusb-1.0, CMake. Acceso USB sin root:
`utils/udev_rules/60-dso5200.rules` en `/etc/udev/rules.d/`.

En ARM (Raspberry Pi, Orange Pi) la GUI usa OpenGL ES automáticamente; en otras placas se fuerza con
`-e`.

## Licencia y créditos

GPL-3.0-or-later (GUI, de OpenHantek6022) y GPL-2.0-or-later (núcleo dso5200basic). La interfaz es
obra de la comunidad OpenHantek (Oliver Haag, David Gräff, Martin Homuth-Rosemann y otros); ver
[CHANGELOG](CHANGELOG) y la historia de git.
