// SPDX-License-Identifier: GPL-2.0-or-later
//
// Implementación del puente (ver dso5200core.h). Este archivo se compila dentro de la biblioteca
// dso5200core, con el directorio src/ de dso5200basic como raíz de includes y con las mismas macros
// de renombrado que las fuentes de dso5200basic, así que aquí sí se ven sus tipos.

#include "dso5200core.h"

#include "device/dso5200.h"
#include "device/simulator.h"
#include "usb/usbdevice.h"

struct Dso5200Core::Impl {
    std::unique_ptr< USBDevice > usb;   ///< nulo en modo demo
    std::unique_ptr< IDsoSource > source; ///< Dso5200Control (aparato) o Simulator (demo)
    Dso5200Control *control = nullptr;  ///< el mismo objeto que source si es el aparato real
    bool debugLog = false;
};

namespace {
Dso5200Core::Frame toFrame( const CaptureFrame &captured ) {
    Dso5200Core::Frame frame;
    frame.frameNumber = captured.frameNumber;
    frame.samplerate = captured.samplerate;
    frame.channels.reserve( captured.channels.size() );
    for ( const ChannelFrame &in : captured.channels ) {
        Dso5200Core::Channel out;
        out.id = in.channelId;
        out.voltsPerDiv = in.voltsPerDiv;
        out.clippedSamples = in.clippedSamples;
        out.samples.assign( in.samples.begin(), in.samples.end() );
        frame.channels.push_back( std::move( out ) );
    }
    return frame;
}
} // namespace

Dso5200Core::Dso5200Core( libusb_device *device, long vendorID, long productID, const std::string &name,
                          bool debugLog )
    : impl( new Impl ) {
    impl->usb.reset( new USBDevice( device, vendorID, productID, name ) );
    impl->debugLog = debugLog;
}

Dso5200Core::Dso5200Core() : impl( new Impl ) {}

bool Dso5200Core::isDemo() const { return !impl->usb; }

Dso5200Core::~Dso5200Core() {
    // Primero el control (detiene y une su hilo de captura), después la conexión USB.
    impl->control = nullptr;
    impl->source.reset();
    impl->usb.reset();
}

bool Dso5200Core::open( std::string &errorMessage ) {
    if ( impl->source )
        return true;
    if ( isDemo() ) {
        impl->source.reset( new Simulator );
        impl->source->onFrame = [ this ]( const CaptureFrame &captured ) {
            if ( onFrame )
                onFrame( toFrame( captured ) );
        };
        return true;
    }
    if ( impl->usb->needsFirmware() ) {
        errorMessage = "el dispositivo todavía no tiene el firmware cargado";
        return false;
    }
    if ( !impl->usb->connectDevice( errorMessage ) )
        return false;
    impl->usb->onDisconnected = [ this ]() {
        if ( onDisconnected )
            onDisconnected();
    };
    impl->control = new Dso5200Control( impl->usb.get(), impl->debugLog );
    impl->source.reset( impl->control );
    impl->source->onFrame = [ this ]( const CaptureFrame &captured ) {
        if ( onFrame )
            onFrame( toFrame( captured ) );
    };
    return true;
}

bool Dso5200Core::isConnected() const {
    if ( isDemo() )
        return bool( impl->source );
    return impl->control && impl->usb->isConnected();
}

void Dso5200Core::start() {
    if ( impl->source )
        impl->source->start();
}

void Dso5200Core::stop() {
    if ( impl->source )
        impl->source->stop();
}

bool Dso5200Core::isSampling() const { return impl->source && impl->source->isSampling(); }

bool Dso5200Core::setTimebase( double secondsPerScreen ) {
    return impl->source && impl->source->setTimebase( secondsPerScreen );
}

bool Dso5200Core::setVoltsPerDiv( unsigned channel, double voltsPerDiv ) {
    return impl->source && impl->source->setVoltsPerDiv( (int)channel, voltsPerDiv );
}

bool Dso5200Core::setChannelEnabled( unsigned channel, bool enabled ) {
    return impl->source && impl->source->setChannelEnabled( (int)channel, enabled );
}

bool Dso5200Core::setCoupling( unsigned channel, Coupling coupling ) {
    // `Coupling` a secas es el enum anidado del puente; ::Coupling es el de dso5200basic.
    ::Coupling mode = ::Coupling::DC;
    if ( coupling == Coupling::AC )
        mode = ::Coupling::AC;
    else if ( coupling == Coupling::GND )
        mode = ::Coupling::GND;
    return impl->source && impl->source->setCoupling( (int)channel, mode );
}

bool Dso5200Core::setTriggerSlopeRising( bool rising ) {
    return impl->source && impl->source->setTriggerSlope( rising ? TriggerSlope::Rising : TriggerSlope::Falling );
}

bool Dso5200Core::setTriggerLevel( double volts ) { return impl->source && impl->source->setTriggerLevel( volts ); }

void Dso5200Core::setRawStats( bool enabled, unsigned channel ) {
    if ( impl->control )
        impl->control->debugSetRawStats( enabled, channel );
}

std::string Dso5200Core::statusText() const { return impl->source ? impl->source->statusText() : std::string(); }
