// SPDX-License-Identifier: GPL-2.0-or-later
//
// Implementación del puente (ver dso5200core.h). Este archivo se compila dentro de la biblioteca
// dso5200core, con el directorio src/ de dso5200basic como raíz de includes y con las mismas macros
// de renombrado que las fuentes de dso5200basic, así que aquí sí se ven sus tipos.

#include "dso5200core.h"

#include "device/dso5200.h"
#include "usb/usbdevice.h"

struct Dso5200Core::Impl {
    std::unique_ptr< USBDevice > usb;
    std::unique_ptr< Dso5200Control > control;
    bool debugLog = false;
};

Dso5200Core::Dso5200Core( libusb_device *device, long vendorID, long productID, const std::string &name,
                          bool debugLog )
    : impl( new Impl ) {
    impl->usb.reset( new USBDevice( device, vendorID, productID, name ) );
    impl->debugLog = debugLog;
}

Dso5200Core::~Dso5200Core() {
    // Primero el control (detiene y une su hilo de captura), después la conexión USB.
    impl->control.reset();
    impl->usb.reset();
}

bool Dso5200Core::open( std::string &errorMessage ) {
    if ( impl->control )
        return true;
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
    impl->control.reset( new Dso5200Control( impl->usb.get(), impl->debugLog ) );
    impl->control->onFrame = [ this ]( const CaptureFrame &captured ) {
        if ( !onFrame )
            return;
        Frame frame;
        frame.frameNumber = captured.frameNumber;
        frame.samplerate = captured.samplerate;
        frame.channels.reserve( captured.channels.size() );
        for ( const ChannelFrame &in : captured.channels ) {
            Channel out;
            out.id = in.channelId;
            out.voltsPerDiv = in.voltsPerDiv;
            out.clippedSamples = in.clippedSamples;
            out.samples.assign( in.samples.begin(), in.samples.end() );
            frame.channels.push_back( std::move( out ) );
        }
        onFrame( frame );
    };
    return true;
}

bool Dso5200Core::isConnected() const { return impl->control && impl->usb->isConnected(); }

void Dso5200Core::start() {
    if ( impl->control )
        impl->control->start();
}

void Dso5200Core::stop() {
    if ( impl->control )
        impl->control->stop();
}

bool Dso5200Core::isSampling() const { return impl->control && impl->control->isSampling(); }

bool Dso5200Core::setTimebase( double secondsPerScreen ) {
    return impl->control && impl->control->setTimebase( secondsPerScreen );
}

bool Dso5200Core::setVoltsPerDiv( unsigned channel, double voltsPerDiv ) {
    return impl->control && impl->control->setVoltsPerDiv( (int)channel, voltsPerDiv );
}

bool Dso5200Core::setChannelEnabled( unsigned channel, bool enabled ) {
    return impl->control && impl->control->setChannelEnabled( (int)channel, enabled );
}

bool Dso5200Core::setTriggerSlopeRising( bool rising ) {
    return impl->control && impl->control->setTriggerSlope( rising ? TriggerSlope::Rising : TriggerSlope::Falling );
}

bool Dso5200Core::setTriggerLevel( double volts ) { return impl->control && impl->control->setTriggerLevel( volts ); }

std::string Dso5200Core::statusText() const { return impl->control ? impl->control->statusText() : std::string(); }
