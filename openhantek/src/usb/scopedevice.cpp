// SPDX-License-Identifier: GPL-2.0-or-later

#include <QCoreApplication>
#include <QDebug>
#include <QList>
#include <iostream>

#include "scopedevice.h"

#include "hantekdso/dsomodel.h"
#include "modelregistry.h"
#include "dso5200/modelDSO5200.h"

#include <QCoreApplication>

// Returns a constant QString with a short description of the given error code,
// this description is intended for displaying to the end user and will be
// in the language set by libusb_setlocale().
// Supported languages:
// libusb-1.0.21 (Win):    "en", "nl", "fr", "ru"
// libusb-1.0.22 (Linux):  "en", "nl", "fr", "ru"
// libusb-1.0.23 (MacOSX): "en", "nl", "fr", "ru", "de", "hu"
const QString libUsbErrorString( int error ) { return QString( libusb_strerror( libusb_error( error ) ) ); }


UniqueUSBid ScopeDevice::computeUSBdeviceID( libusb_device *device ) {
    // Returns a 64-bit value that uniquely identifies a device on the bus
    // bus/ports define a constant plug position
    // VID/FW changes with FW upload
    // bpppppppVVVVFFFF
    //             ^^^^-- Firmware version (16bit)(can change after upload of new FW over old FW)
    //         ^^^^------ Vendor ID (16 bit)(changes with FW upload to device w/o FW)
    //  ^^^^^^^---------- USB ports tree (as shown by "lsusb -t"), max 7 ports, port = 1..15, 0 = none
    // ^----------------- USB bus, bus = 1..15

    // Get device descriptor
    struct libusb_device_descriptor descriptor;
    libusb_get_device_descriptor( device, &descriptor );
    // collect values and arrange them
    UniqueUSBid uid = libusb_get_bus_number( device ) & 0x0F; // typically no more than 15 busses
    const int treeSize = 7;                                   // port tree max size is 7
    uint8_t ports[ treeSize ];
    int nPorts = libusb_get_port_numbers( device, ports, sizeof( ports ) );
    for ( int iii = 0; iii < treeSize; ++iii ) {
        uid <<= 4;
        if ( iii < nPorts )
            uid |= ports[ iii ] & 0x0F;
    }
    uid <<= 16;
    uid |= descriptor.idVendor;
    uid <<= 16;
    uid |= descriptor.bcdDevice;
    return uid;
}


ScopeDevice::ScopeDevice( DSOModel *model, libusb_device *device, unsigned findIteration )
    : model( model ), device( device ), findIteration( findIteration ), uniqueUSBdeviceID( computeUSBdeviceID( device ) ) {
    libusb_ref_device( device );
    libusb_get_device_descriptor( device, &descriptor );
}


// Modo demo: un DSO-5200A simulado (el simulador de dso5200basic, ver Dso5200Core).
static DSOModel *demoModel() {
    for ( DSOModel *model : ModelRegistry::get()->models() )
        if ( model->ID == ModelDSO5200A::ID )
            return model;
    return nullptr;
}

ScopeDevice::ScopeDevice() : model( demoModel() ), device( nullptr ), uniqueUSBdeviceID( 0 ), realHW( false ) {}


bool ScopeDevice::connectDevice( QString &errorMessage ) {
    if ( isDemoDevice() )
        return true;
    if ( !device ) {
        errorMessage = QCoreApplication::translate( "ScopeDevice", "Couldn't open device: device is no longer available" );
        return false;
    }
    if ( needsFirmware() )
        return false;
    if ( isConnected() )
        return true;

    // Open device
    int errorCode = libusb_open( device, &( handle ) );
    if ( errorCode != LIBUSB_SUCCESS ) {
        handle = nullptr;
        errorMessage =
            QCoreApplication::translate( "ScopeDevice", "Couldn't open device: %1" ).arg( libUsbErrorString( errorCode ) );
        return false;
    }
    serialNumber = readUSBdescriptor( handle, descriptor.iSerialNumber );
    // Find and claim interface
    errorCode = LIBUSB_ERROR_NOT_FOUND;
    libusb_config_descriptor *configDescriptor;
    libusb_get_config_descriptor( device, 0, &configDescriptor );
    for ( int interfaceIndex = 0; interfaceIndex < int( configDescriptor->bNumInterfaces ); ++interfaceIndex ) {
        const libusb_interface *pInterface = &configDescriptor->interface[ interfaceIndex ];
        if ( pInterface->num_altsetting < 1 )
            continue;

        const libusb_interface_descriptor *interfaceDescriptor = &pInterface->altsetting[ 0 ];
        if ( interfaceDescriptor->bInterfaceClass == LIBUSB_CLASS_VENDOR_SPEC && interfaceDescriptor->bInterfaceSubClass == 0 &&
             interfaceDescriptor->bInterfaceProtocol == 0 ) {
            errorCode = claimInterface( interfaceDescriptor );
            break;
        }
    }

    libusb_free_config_descriptor( configDescriptor );

    if ( errorCode != LIBUSB_SUCCESS ) {
        errorMessage = QString( "%1 (%2:%3)" )
                           .arg( libUsbErrorString( errorCode ) )
                           .arg( libusb_get_bus_number( device ), 3, 10, QLatin1Char( '0' ) )
                           .arg( libusb_get_device_address( device ), 3, 10, QLatin1Char( '0' ) );
        return false;
    }
    disconnected = false;
    return true;
}


ScopeDevice::~ScopeDevice() {
    disconnectFromDevice( true );
    if ( device != nullptr )
        libusb_unref_device( device );
    device = nullptr;
}


int ScopeDevice::claimInterface( const libusb_interface_descriptor *interfaceDescriptor ) {
    int errorCode = libusb_claim_interface( handle, interfaceDescriptor->bInterfaceNumber );
    if ( errorCode < 0 )
        return errorCode;

    nInterface = interfaceDescriptor->bInterfaceNumber;

    // Check the maximum endpoint packet size
    const libusb_endpoint_descriptor *endpointDescriptor;
    outPacketLength = 0;
    inPacketLength = 0;
    for ( int endpoint = 0; endpoint < interfaceDescriptor->bNumEndpoints; ++endpoint ) {
        endpointDescriptor = &( interfaceDescriptor->endpoint[ endpoint ] );
        if ( endpointDescriptor->bEndpointAddress == HANTEK_EP_OUT ) {
            outPacketLength = endpointDescriptor->wMaxPacketSize;
        } else if ( endpointDescriptor->bEndpointAddress == HANTEK_EP_IN ) {
            inPacketLength = endpointDescriptor->wMaxPacketSize;
        }
    }
    return LIBUSB_SUCCESS;
}


void ScopeDevice::disconnectFromDevice( bool expected ) {
    expectedDisconnect |= expected;
    if ( disconnected && !handle )
        return;

    disconnected = true;
    if ( !device )
        return;

    if ( handle ) {
        // Release claimed interface
        if ( nInterface != -1 )
            libusb_release_interface( handle, nInterface );
        nInterface = -1;

        // Close device handle
        libusb_close( handle );
    }
    handle = nullptr;

    emit deviceDisconnected( expectedDisconnect );
}


bool ScopeDevice::isConnected() { return isDemoDevice() || ( !disconnected && handle != nullptr ); }


bool ScopeDevice::needsFirmware() {
    return descriptor.idProduct != model->productID || descriptor.idVendor != model->vendorID ||
           descriptor.bcdDevice < model->firmwareVersion;
}



QString ScopeDevice::readUSBdescriptor( libusb_device_handle *handle, uint8_t index ) {
    unsigned char string[ 255 ];
    int ret = libusb_get_string_descriptor_ascii( handle, index, string, sizeof( string ) );
    if ( ret > 0 )
        return QString::fromLatin1( reinterpret_cast< char * >( string ), ret ).trimmed();
    else
        return QString();
}
