// SPDX-License-Identifier: GPL-2.0-or-later
//
// DSO-5200/5200A: adaptador entre la GUI (interfaz IDsoControl, ver idsocontrol.h) y el núcleo del
// protocolo de dso5200basic (Dso5200Core, ver ../../dso5200core/dso5200core.h).
//
// Aquí NO hay lógica de protocolo: tablas de base de tiempo, relés, ganancias, cero, conversión a
// voltios, disparo y la máquina de estados de captura viven en dso5200basic, que se compila sin
// modificar (es la referencia validada contra el aparato). Este adaptador solo:
//  - traduce las llamadas de la GUI (V/div y nivel en la punta de la sonda) a unidades del núcleo
//    (voltios en la entrada del aparato), dividiendo por la atenuación de la sonda;
//  - convierte cada captura del núcleo en DSOsamples (multiplicando por la sonda, inversión de canal,
//    bit de recorte y posición del disparo por hardware);
//  - dispara CH2 por software (el aparato solo dispara desde CH1);
//  - calcula el canal MATH (MathChannel de la GUI);
//  - en modo demo usa el simulador de dso5200basic en vez del aparato.

#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include "dso5200core.h"
#include "idsocontrol.h"

#include "hantekdso/dsosamples.h"
#include "hantekdso/mathchannel.h"
#include "scopesettings.h"

class ScopeDevice;
class DSOModel;

class Dso5200DsoControl : public IDsoControl {
    Q_OBJECT

  public:
    /// \param scopeDevice Ya con firmware y conectado por la GUI. No se toma su propiedad: el adaptador
    /// cierra esa conexión y el núcleo abre la suya sobre el mismo dispositivo USB.
    explicit Dso5200DsoControl( ScopeDevice *scopeDevice, const DSOModel *model, int verboseLevel );
    ~Dso5200DsoControl() override;

    void stateMachine() override {} // la captura corre en el hilo propio del núcleo
    void stopStateMachine() override {}

    double getSamplerate() const override { return samplerate; }
    unsigned getSamplesize() const override { return kRecordLength; }
    bool isSamplingUI() const override { return samplingUI; }
    const ScopeDevice *getDevice() const override { return device; }
    bool isDeviceAvailable() const override;
    const DSOModel *getModel() const override { return model; }

    void quitSampling() override;
    void prepareForShutdown() override {}

  public slots:
    void enableSamplingUI( bool enabled = true ) override;
    Dso::ErrorCode setSamplerate( double samplerate = 0.0 ) override;
    Dso::ErrorCode setRecordTime( double duration = 0.0 ) override;
    Dso::ErrorCode setChannelUsed( ChannelID channel, bool used ) override;
    Dso::ErrorCode setChannelInverted( ChannelID channel, bool inverted ) override;
    Dso::ErrorCode setProbe( ChannelID channel, double probeAttn ) override;
    Dso::ErrorCode setGain( ChannelID channel, double gain ) override;
    Dso::ErrorCode setCoupling( ChannelID channel, Dso::Coupling coupling ) override;
    Dso::ErrorCode setTriggerMode( Dso::TriggerMode mode ) override;
    Dso::ErrorCode setTriggerSource( int channel ) override;
    Dso::ErrorCode setTriggerLevel( ChannelID channel, double level ) override;
    Dso::ErrorCode setTriggerSlope( Dso::Slope slope ) override;
    Dso::ErrorCode setTriggerPosition( double position ) override;
    void applySettings( const DsoSettingsScope *scope ) override;
    void parkForReconnect() override;
    void rebindDevice( ScopeDevice *newScopeDevice ) override;
    void restartSampling() override;

  private:
    static constexpr unsigned kChannels = 2;
    static constexpr unsigned kRecordLength = 10240; // muestras por canal en modo de dos canales

    struct ChannelSettings {
        bool used = true;
        bool inverted = false;
        double probeAttn = 1.0;
        double gain = 0.5; ///< V/div en la punta de la sonda, tal como lo pide la GUI
        Dso::Coupling coupling = Dso::Coupling::DC; ///< igual que el arranque del núcleo
    };

    bool openCore();
    void applyChannel( ChannelID channel ); ///< reenvía V/div (y nivel si es CH1) al núcleo
    void applyTriggerLevel();
    void applyCoupling( ChannelID channel );
    void onCoreFrame( const Dso5200Core::Frame &frame ); ///< hilo de captura del núcleo
    /// Índice del cruce de `level` con la pendiente pedida más cercano al centro del registro, o -1.
    static int findSoftwareTrigger( const std::vector< double > &samples, double level, double hysteresis,
                                    bool rising );
    bool coreReady() const { return core && core->isConnected(); }

    ScopeDevice *device;
    const DSOModel *model;
    int verboseLevel;

    std::unique_ptr< Dso5200Core > core;
    mutable std::mutex settingsMutex; ///< protege channel[]/triggerLevel entre la GUI y el hilo del núcleo
    ChannelSettings channel[ kChannels ];
    double triggerLevel[ kChannels ] = { 0.0, 0.0 }; ///< voltios en la punta de la sonda, uno por canal
    /// 0 = CH1, disparo por hardware. 1 = CH2, disparo por SOFTWARE: el aparato solo dispara desde CH1
    /// (dso5200basic 6a7da56), así que se busca el flanco en las muestras de CH2 (findSoftwareTrigger).
    int triggerSource = 0;
    bool triggerRising = true;
    double timebaseSeconds = 0.01;
    double samplerate = 0.0;
    /// La GUI recalcula por su cuenta la frecuencia de muestreo al cambiar la base de tiempo (con la
    /// especificación genérica, que no conoce la tabla del núcleo). Tras cada cambio se reenvía la real
    /// con el siguiente frame.
    std::atomic< bool > samplerateDirty{ true };
    bool samplingUI = true;
    int debugChannel = -1; ///< DSO5200_CAL

    DSOsamples result;
    const DsoSettingsScope *scopeSettings = nullptr; ///< de la GUI; lo usa el canal MATH
    std::unique_ptr< MathChannel > mathChannel;
};
