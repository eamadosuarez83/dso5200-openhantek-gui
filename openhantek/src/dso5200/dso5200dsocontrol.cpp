// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ver dso5200dsocontrol.h. Toda la lógica de protocolo está en dso5200basic (vía Dso5200Core).

#include "dso5200dsocontrol.h"

#include <QReadLocker>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <QWriteLocker>

#include "dsomodel.h"
#include "usb/scopedevice.h"

Dso5200DsoControl::Dso5200DsoControl( ScopeDevice *scopeDevice, const DSOModel *model, int verboseLevel )
    : device( scopeDevice ), model( model ), verboseLevel( verboseLevel ) {
    result.data.resize( kChannels + 1 ); // CH1, CH2 y el hueco del canal MATH (no existe en este aparato)
    openCore();
}

Dso5200DsoControl::~Dso5200DsoControl() { core.reset(); }

bool Dso5200DsoControl::openCore() {
    core.reset();
    if ( !device || !model )
        return false;

    // El núcleo abre su propia conexión al mismo dispositivo USB; la de la GUI se cierra antes porque
    // el interfaz solo puede reclamarlo una conexión. expected=true: no es una desconexión real, el
    // supervisor de reconexión la ignora.
    libusb_device *usbDevice = device->getUSBDevice();
    device->disconnectFromDevice( true );

    core.reset( new Dso5200Core( usbDevice, (long)model->vendorID, (long)model->productID, model->name.toStdString(),
                                 verboseLevel > 2 ) );
    core->onFrame = [ this ]( const Dso5200Core::Frame &frame ) { onCoreFrame( frame ); };
    core->onDisconnected = [ this ]() { emit communicationError(); };

    std::string errorMessage;
    if ( !core->open( errorMessage ) ) {
        emit statusMessage( tr( "DSO-5200: no se pudo abrir el dispositivo: %1" ).arg( QString::fromStdString( errorMessage ) ),
                            0 );
        core.reset();
        return false;
    }

    // Reaplicar el estado conocido (al arrancar son los valores por omisión; tras una reconexión, lo
    // último que pidió la GUI).
    core->setTimebase( timebaseSeconds );
    for ( ChannelID ch = 0; ch < kChannels; ++ch ) {
        core->setChannelEnabled( ch, channel[ ch ].used );
        applyCoupling( ch );
        applyChannel( ch );
    }
    // Depuración: DSO5200_CAL=1|2 vuelca a stderr, por frame, las estadísticas crudas del ADC del
    // núcleo y las de las muestras que se entregan a la GUI, para comparar con la CLI.
    if ( const char *cal = std::getenv( "DSO5200_CAL" ) ) {
        debugChannel = std::atoi( cal ) - 1;
        if ( debugChannel >= 0 && debugChannel < (int)kChannels )
            core->setRawStats( true, (unsigned)debugChannel );
    }
    if ( samplingUI )
        core->start();
    return true;
}

bool Dso5200DsoControl::isDeviceAvailable() const { return coreReady(); }

void Dso5200DsoControl::applyChannel( ChannelID ch ) {
    if ( !coreReady() || ch >= kChannels )
        return;
    double voltsPerDivAtInput;
    {
        std::lock_guard< std::mutex > lock( settingsMutex );
        voltsPerDivAtInput = channel[ ch ].gain / channel[ ch ].probeAttn;
    }
    core->setVoltsPerDiv( ch, voltsPerDivAtInput );
    if ( ch == 0 )
        applyTriggerLevel(); // el nivel del núcleo va en voltios en la entrada: depende de la sonda de CH1
}

void Dso5200DsoControl::applyCoupling( ChannelID ch ) {
    if ( !coreReady() || ch >= kChannels )
        return;
    Dso::Coupling coupling;
    {
        std::lock_guard< std::mutex > lock( settingsMutex );
        coupling = channel[ ch ].coupling;
    }
    Dso5200Core::Coupling mode = Dso5200Core::Coupling::DC;
    if ( coupling == Dso::Coupling::AC )
        mode = Dso5200Core::Coupling::AC;
    else if ( coupling == Dso::Coupling::GND )
        mode = Dso5200Core::Coupling::GND;
    core->setCoupling( ch, mode );
}

void Dso5200DsoControl::applyTriggerLevel() {
    if ( !coreReady() )
        return;
    double levelAtInput;
    {
        std::lock_guard< std::mutex > lock( settingsMutex );
        levelAtInput = triggerLevel / channel[ 0 ].probeAttn;
    }
    core->setTriggerLevel( levelAtInput );
}

void Dso5200DsoControl::onCoreFrame( const Dso5200Core::Frame &frame ) {
    ChannelSettings settings[ kChannels ];
    {
        std::lock_guard< std::mutex > lock( settingsMutex );
        for ( unsigned ch = 0; ch < kChannels; ++ch )
            settings[ ch ] = channel[ ch ];
    }

    const bool samplerateChanged = samplerateDirty.exchange( false ) || frame.samplerate != samplerate;
    {
        QWriteLocker locker( &result.lock );
        for ( auto &v : result.data )
            v.clear();
        result.samplerate = frame.samplerate;
        result.clipped = 0;
        result.liveTrigger = true;
        result.freeRunning = false;
        result.tag = frame.frameNumber;
        size_t samplesPerChannel = 0;
        for ( const Dso5200Core::Channel &in : frame.channels ) {
            if ( in.id >= kChannels )
                continue;
            const double factor = ( settings[ in.id ].inverted ? -1.0 : 1.0 ) * settings[ in.id ].probeAttn;
            std::vector< double > &out = result.data[ in.id ];
            out.resize( in.samples.size() );
            for ( size_t i = 0; i < in.samples.size(); ++i )
                out[ i ] = in.samples[ i ] * factor;
            if ( in.clippedSamples )
                result.clipped |= (unsigned char)( 1u << in.id );
            samplesPerChannel = std::max( samplesPerChannel, in.samples.size() );
        }
        // El núcleo dispara por hardware con el pre-disparo fijo al 50 % y devuelve la captura ya
        // rotada desde el triggerPoint, así que el disparo queda a mitad del registro (medido: el
        // flanco de la CAL cae en la muestra ~5114 de 10240).
        result.triggeredPosition = (int)( samplesPerChannel / 2 );
        if ( debugChannel >= 0 && !result.data[ debugChannel ].empty() ) {
            const std::vector< double > &v = result.data[ debugChannel ];
            double mn = v[ 0 ], mx = v[ 0 ], sum = 0.0;
            for ( double x : v ) {
                mn = std::min( mn, x );
                mx = std::max( mx, x );
                sum += x;
            }
            const double mean = sum / (double)v.size();
            double ac2 = 0.0;
            for ( double x : v )
                ac2 += ( x - mean ) * ( x - mean );
            fprintf( stderr, "[gui] ch%d entregado n=%zu min=%.3f max=%.3f pp=%.3f mean=%.3f acrms=%.3f sonda=%.1f gain=%.3f\n",
                     debugChannel + 1, v.size(), mn, mx, mx - mn, mean, std::sqrt( ac2 / (double)v.size() ),
                     settings[ debugChannel ].probeAttn, settings[ debugChannel ].gain );
        }
    }
    samplerate = frame.samplerate;
    if ( samplerateChanged )
        emit samplerateCalculated( samplerate, 1 );
    if ( samplingUI )
        emit samplesAvailable( &result );
}

Dso::ErrorCode Dso5200DsoControl::stringCommand( const QString & ) {
    // Sin consola de comandos crudos: se usa para mostrar el estado del núcleo (útil para depurar).
    if ( coreReady() )
        emit statusMessage( QString::fromStdString( core->statusText() ), 10000 );
    return Dso::ErrorCode::NONE;
}

void Dso5200DsoControl::quitSampling() {
    if ( core )
        core->stop();
}

void Dso5200DsoControl::enableSamplingUI( bool enabled ) {
    samplingUI = enabled;
    emit showSamplingStatus( enabled );
    if ( !core )
        return;
    if ( enabled )
        core->start();
    else
        core->stop();
}

void Dso5200DsoControl::restartSampling() {
    // El núcleo rearma la captura solo después de cada cambio de configuración.
}

Dso::ErrorCode Dso5200DsoControl::setRecordTime( double duration ) {
    if ( duration <= 0.0 )
        duration = timebaseSeconds;
    timebaseSeconds = duration;
    samplerateDirty = true;
    if ( !coreReady() )
        return Dso::ErrorCode::CONNECTION;
    return core->setTimebase( duration ) ? Dso::ErrorCode::NONE : Dso::ErrorCode::PARAMETER;
}

Dso::ErrorCode Dso5200DsoControl::setSamplerate( double newSamplerate ) {
    if ( newSamplerate <= 0.0 )
        return setRecordTime( timebaseSeconds );
    return setRecordTime( kRecordLength / newSamplerate );
}

Dso::ErrorCode Dso5200DsoControl::setChannelUsed( ChannelID ch, bool used ) {
    if ( ch >= kChannels )
        return Dso::ErrorCode::PARAMETER;
    {
        std::lock_guard< std::mutex > lock( settingsMutex );
        channel[ ch ].used = used;
    }
    if ( !coreReady() )
        return Dso::ErrorCode::CONNECTION;
    return core->setChannelEnabled( ch, used ) ? Dso::ErrorCode::NONE : Dso::ErrorCode::PARAMETER;
}

Dso::ErrorCode Dso5200DsoControl::setChannelInverted( ChannelID ch, bool inverted ) {
    if ( ch >= kChannels )
        return Dso::ErrorCode::PARAMETER;
    std::lock_guard< std::mutex > lock( settingsMutex );
    channel[ ch ].inverted = inverted; // se aplica en onCoreFrame()
    return Dso::ErrorCode::NONE;
}

Dso::ErrorCode Dso5200DsoControl::setProbe( ChannelID ch, double probeAttn ) {
    if ( ch >= kChannels || probeAttn <= 0.0 )
        return Dso::ErrorCode::PARAMETER;
    {
        std::lock_guard< std::mutex > lock( settingsMutex );
        channel[ ch ].probeAttn = probeAttn;
    }
    applyChannel( ch );
    return Dso::ErrorCode::NONE;
}

Dso::ErrorCode Dso5200DsoControl::setGain( ChannelID ch, double gain ) {
    if ( ch >= kChannels || gain <= 0.0 )
        return Dso::ErrorCode::PARAMETER;
    {
        std::lock_guard< std::mutex > lock( settingsMutex );
        channel[ ch ].gain = gain;
    }
    if ( !coreReady() )
        return Dso::ErrorCode::CONNECTION;
    applyChannel( ch );
    return Dso::ErrorCode::NONE;
}

Dso::ErrorCode Dso5200DsoControl::setCoupling( ChannelID ch, Dso::Coupling coupling ) {
    if ( ch >= kChannels )
        return Dso::ErrorCode::PARAMETER;
    {
        std::lock_guard< std::mutex > lock( settingsMutex );
        channel[ ch ].coupling = coupling;
    }
    if ( !coreReady() )
        return Dso::ErrorCode::CONNECTION;
    applyCoupling( ch );
    if ( coupling == Dso::Coupling::GND )
        emit statusMessage( tr( "DSO-5200: GND por software (el aparato no tiene relé de masa): CH%1 se muestra en 0 V." )
                                .arg( ch + 1 ),
                            5000 );
    return Dso::ErrorCode::NONE;
}

Dso::ErrorCode Dso5200DsoControl::setTriggerMode( Dso::TriggerMode mode ) {
    if ( mode == Dso::TriggerMode::AUTO )
        return Dso::ErrorCode::NONE;
    emit statusMessage( tr( "DSO-5200: solo el modo de disparo AUTO está disponible en el núcleo." ), 5000 );
    return Dso::ErrorCode::UNSUPPORTED;
}

Dso::ErrorCode Dso5200DsoControl::setTriggerSource( int ch ) {
    if ( ch == 0 )
        return Dso::ErrorCode::NONE;
    emit statusMessage( tr( "DSO-5200: el disparo solo puede venir de CH1 en el núcleo." ), 5000 );
    return Dso::ErrorCode::UNSUPPORTED;
}

Dso::ErrorCode Dso5200DsoControl::setTriggerSmooth( int ) { return Dso::ErrorCode::UNSUPPORTED; }

Dso::ErrorCode Dso5200DsoControl::setTriggerLevel( ChannelID ch, double level ) {
    if ( ch != 0 )
        return Dso::ErrorCode::UNSUPPORTED; // fuente fija CH1
    {
        std::lock_guard< std::mutex > lock( settingsMutex );
        triggerLevel = level;
    }
    if ( !coreReady() )
        return Dso::ErrorCode::CONNECTION;
    applyTriggerLevel();
    return Dso::ErrorCode::NONE;
}

Dso::ErrorCode Dso5200DsoControl::setTriggerSlope( Dso::Slope slope ) {
    if ( slope == Dso::Slope::Both )
        return Dso::ErrorCode::UNSUPPORTED;
    if ( !coreReady() )
        return Dso::ErrorCode::CONNECTION;
    return core->setTriggerSlopeRising( slope == Dso::Slope::Positive ) ? Dso::ErrorCode::NONE
                                                                        : Dso::ErrorCode::PARAMETER;
}

Dso::ErrorCode Dso5200DsoControl::setTriggerPosition( double ) {
    // El pre-disparo del hardware queda fijo al 50 % en el núcleo. La GUI usa su propia posición para
    // elegir qué parte del registro mostrar alrededor de triggeredPosition, así que esto basta.
    return Dso::ErrorCode::NONE;
}

Dso::ErrorCode Dso5200DsoControl::setCalFreq( double ) { return Dso::ErrorCode::UNSUPPORTED; }

void Dso5200DsoControl::calibrateOffset( bool ) {
    emit statusMessage( tr( "DSO-5200: sin calibración EEPROM/en vivo; el cero lo calcula el núcleo." ), 5000 );
}

void Dso5200DsoControl::applySettings( const DsoSettingsScope *scope ) {
    if ( !scope )
        return;
    {
        std::lock_guard< std::mutex > lock( settingsMutex );
        for ( ChannelID ch = 0; ch < kChannels && ch < scope->voltage.size(); ++ch ) {
            channel[ ch ].used = scope->voltage[ ch ].used;
            channel[ ch ].inverted = scope->voltage[ ch ].inverted;
            channel[ ch ].probeAttn = scope->voltage[ ch ].probeAttn > 0.0 ? scope->voltage[ ch ].probeAttn : 1.0;
            channel[ ch ].gain = scope->gain( ch );
        }
        if ( !scope->voltage.empty() )
            triggerLevel = scope->voltage[ 0 ].trigger;
    }
    timebaseSeconds = scope->horizontal.timebase * DIVS_TIME;
    samplerateDirty = true;
    if ( !coreReady() )
        return;
    core->setTimebase( timebaseSeconds );
    for ( ChannelID ch = 0; ch < kChannels; ++ch ) {
        core->setChannelEnabled( ch, channel[ ch ].used );
        applyChannel( ch );
    }
    if ( scope->trigger.slope != Dso::Slope::Both )
        core->setTriggerSlopeRising( scope->trigger.slope == Dso::Slope::Positive );
    setTriggerMode( scope->trigger.mode );
    setTriggerSource( scope->trigger.source );
    for ( ChannelID ch = 0; ch < kChannels && ch < scope->voltage.size(); ++ch )
        setCoupling( ch, scope->coupling( ch, model->spec() ) );
}

void Dso5200DsoControl::parkForReconnect() {
    core.reset();
    device = nullptr;
}

void Dso5200DsoControl::rebindDevice( ScopeDevice *newScopeDevice ) {
    device = newScopeDevice;
    openCore();
}
