// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QMutex>
#include <QObject>
#include <QReadWriteLock>
#include <QStringList>

#ifdef Q_OS_FREEBSD
#include <libusb.h>
#else
#include <libusb-1.0/libusb.h>
#endif
#include <memory>

#include "dsomodel.h"
#include "usbdevicedefinitions.h"

class DSOModel;

typedef uint64_t UniqueUSBid;


/// \brief Returns string representation for libusb errors.
/// \param error The error code.
/// \return String explaining the error.
const QString libUsbErrorString( int error );


/// \brief This class handles the USB communication with an usb device that has
/// one in and one out endpoint.
class ScopeDevice : public QObject {
    Q_OBJECT

  public:
    explicit ScopeDevice( DSOModel *model, libusb_device *device, unsigned findIteration = 0 );
    explicit ScopeDevice();
    ScopeDevice( const ScopeDevice & ) = delete;
    ~ScopeDevice() override;
    bool connectDevice( QString &errorMessage );
    void disconnectFromDevice( bool expected = false );

    /// Mark future disconnects as part of intentional shutdown/ownership cleanup.
    void setExpectedDisconnect( bool expected = true ) { expectedDisconnect = expected; }
    bool wasExpectedDisconnect() const { return expectedDisconnect; }

    /// \brief Check if the oscilloscope is connected.
    /// \return true, if a connection is up.
    bool isConnected();

    /// \brief Distinguish between real hw or demo device
    bool isRealHW() const { return realHW; }
    bool isDemoDevice() const { return !realHW; }

    /**
     * @return Return true if this device needs a firmware first
     */
    bool needsFirmware();

    /**
     * @return Return device version as unsigned int
     */
    inline unsigned int getFwVersion() const { return descriptor.bcdDevice; }

    /**
     * @brief getSerialNumber
     * @return Return serial number string of the device
     */
    inline const QString getSerialNumber() const { return serialNumber; }


    /**
     * @brief readUSBdescriptor
     * @param handle The device handle
     * @param index of requested string
     * @return serial number string
     */
    QString readUSBdescriptor( libusb_device_handle *handle, uint8_t index );

    /**
     * Keep track of the find iteration on which this device was found
     * @param iteration The new iteration value
     */
    inline void setFindIteration( unsigned iteration ) { findIteration = iteration; }
    inline unsigned getFindIteration() const { return findIteration; }

    /**
     * @return Returns the raw libusb device
     */
    inline libusb_device *getUSBDevice() const { return device; }

    /**
     * @return Return the unique usb device id {@link USBDevice::computeUSBdeviceID()}.
     */
    inline UniqueUSBid getUniqueUSBDeviceID() const { return uniqueUSBdeviceID; }
    /**
     * ID built from bus, port, VID, PID and FW version
     */
    static UniqueUSBid computeUSBdeviceID( libusb_device *device );

    /// \brief Get the oscilloscope model.
    /// \return The ::Model of the connected Hantek DSO.
    inline const DSOModel *getModel() const { return model; }
  protected:
    int claimInterface( const libusb_interface_descriptor *interfaceDescriptor );

    // Device model data
    DSOModel *model;

    // Libusb specific variables
    struct libusb_device_descriptor descriptor;
    libusb_device *device; ///< The USB handle for the oscilloscope
    libusb_device_handle *handle = nullptr;
    unsigned findIteration;
    const UniqueUSBid uniqueUSBdeviceID;
    int nInterface;
    unsigned outPacketLength; ///< Packet length for the OUT endpoint
    unsigned inPacketLength;  ///< Packet length for the IN endpoint

  private:
    bool realHW = true;
    bool disconnected = true;
    bool expectedDisconnect = false;
    QString serialNumber = "0000";

  signals:
    void deviceDisconnected( bool expected ); ///< The device has been disconnected
};

extern int verboseLevel;
