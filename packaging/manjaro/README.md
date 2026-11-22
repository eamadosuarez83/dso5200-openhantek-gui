# Paquete para Manjaro / Arch Linux

`PKGBUILD` del paquete **`dso5200-openhantek`**: el software del osciloscopio Hantek
DSO-5200/5200A, construido desde GitHub.

## Qué instala

| Archivo | Qué es |
|---|---|
| `/usr/bin/dso5200-openhantek` | La interfaz gráfica (fork de OpenHantek6022). Aparece en el menú como **OpenHantek DSO-5200** |
| `/usr/bin/dso5200basic` | La CLI del protocolo (`dso5200basic`), ya apuntando a su firmware |
| `/usr/bin/dso5200-viewer` | Visor en vivo de la CLI (necesita `python-matplotlib` y `tk`) |
| `/usr/lib/udev/rules.d/60-dso5200.rules` | Permiso de acceso USB sin root |

La interfaz gráfica lleva el firmware dentro del ejecutable. La CLI usa el de
`/usr/share/dso5200basic/firmware`.

El binario y el acceso directo tienen nombres propios, así que el paquete puede
convivir con `openhantek6022` del AUR.

## Instalar

Como usuario normal, no como root:

```sh
cd packaging/manjaro
makepkg -si
```

`makepkg` clona los dos repositorios desde GitHub (la GUI y
[dso5200-cli](https://github.com/eamadosuarez83/dso5200-cli)), los compila y,
con `-i`, instala el paquete con `pacman` (pide la contraseña de sudo). Con `-s`
instala antes las dependencias que falten.

Si ya tienes el `.pkg.tar.zst` construido:

```sh
sudo pacman -U dso5200-openhantek-*.pkg.tar.zst
```

Después de instalar, **desconecta y vuelve a conectar el osciloscopio**, para que
se apliquen las reglas udev.

Si antes copiaste a mano `60-dso5200.rules` en `/etc/udev/rules.d/`, puedes
borrarla con `sudo rm /etc/udev/rules.d/60-dso5200.rules`. Tiene el mismo nombre,
así que mientras exista tapa a la del paquete; el contenido es el mismo.

## Actualizar

La versión del paquete sale de los commits de los dos repositorios. Para
recoger lo último de GitHub, vuelve a ejecutar `makepkg -si` en esta carpeta.

## Desinstalar

```sh
sudo pacman -R dso5200-openhantek
```

La configuración de la GUI queda en `~/.config/OpenHantek/`.
