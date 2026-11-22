// SPDX-License-Identifier: GPL-2.0-or-later
//
// Puente hacia el núcleo del protocolo DSO-5200/5200A de dso5200basic.
//
// dso5200basic (../../dso5200basic, repo eamadosuarez83/dso5200-cli) es la referencia del protocolo,
// validada contra el aparato real, y NO se modifica desde aquí: sus fuentes se compilan tal cual en
// la biblioteca estática dso5200core (ver openhantek/CMakeLists.txt). Esta cabecera es deliberadamente
// neutra: no incluye Qt ni ningún tipo de dso5200basic, para que la GUI y el núcleo no se mezclen.
// En particular, ambos definen un namespace `Hantek`, una función `libUsbErrorString(int)` y macros
// HANTEK_* con el mismo nombre; las fuentes de dso5200basic se compilan con macros que renombran los
// dos primeros, y los terceros nunca se ven desde la GUI porque solo dso5200core.cpp las incluye.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct libusb_device;

class Dso5200Core {
  public:
    /// Un canal de una captura, ya convertido a voltios en la entrada del aparato (sin sonda).
    struct Channel {
        unsigned id = 0;               ///< 0 = CH1, 1 = CH2
        double voltsPerDiv = 1.0;      ///< escala elegida en el núcleo
        uint32_t clippedSamples = 0;   ///< muestras en el tope del ADC (0 o 1023)
        std::vector< double > samples; ///< voltios
    };
    struct Frame {
        uint32_t frameNumber = 0;
        double samplerate = 0.0;
        std::vector< Channel > channels; ///< solo los canales activos
    };

    /// No abre nada todavía; ver open().
    Dso5200Core( libusb_device *device, long vendorID, long productID, const std::string &name, bool debugLog );
    ~Dso5200Core();
    Dso5200Core( const Dso5200Core & ) = delete;
    Dso5200Core &operator=( const Dso5200Core & ) = delete;

    /// Abre su propia conexión USB al dispositivo (ya con firmware) y arranca el hilo de captura del
    /// núcleo. La conexión de la GUI a ese dispositivo debe estar cerrada antes: el interfaz USB solo
    /// puede reclamarlo una conexión.
    bool open( std::string &errorMessage );
    bool isConnected() const;

    void start();
    void stop();
    bool isSampling() const;

    bool setTimebase( double secondsPerScreen );
    bool setVoltsPerDiv( unsigned channel, double voltsPerDiv );
    bool setChannelEnabled( unsigned channel, bool enabled );
    bool setTriggerSlopeRising( bool rising );
    bool setTriggerLevel( double volts ); ///< fuente fija CH1 en el núcleo
    std::string statusText() const;

    /// Se llaman desde el hilo de captura del núcleo, no desde el hilo de la GUI.
    std::function< void( const Frame & ) > onFrame;
    std::function< void() > onDisconnected;

  private:
    struct Impl;
    std::unique_ptr< Impl > impl;
};
