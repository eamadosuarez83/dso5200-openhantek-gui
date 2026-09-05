// SPDX-License-Identifier: GPL-2.0-or-later
//
// Common interface extracted from HantekDsoControl's public surface so the
// GUI (mainwindow, main.cpp, devicereconnectionsupervisor) can drive either
// the original 6022-family control class or the new Dso5200DsoControl
// without caring which one is actually connected. Every slot/signal name and
// signature below is copied verbatim from hantekdso/hantekdsocontrol.h so
// that existing connect() calls throughout the GUI keep working unchanged
// (Qt signal inheritance means `connect(obj, &IDsoControl::foo, ...)` still
// binds correctly on a HantekDsoControl* or Dso5200DsoControl* instance).
//
// Slots that make no sense for a given concrete device (e.g. setCalFreq()
// and calibrateOffset() on the DSO-5200, which has neither a cal-frequency
// generator nor an EEPROM-based live calibration workflow) are still part of
// the interface - the GUI wires them unconditionally - but such
// implementations are simple no-ops returning Dso::ErrorCode::UNSUPPORTED.

#pragma once

#include "hantekdso/errorcodes.h"
#include "hantekdso/dsosamples.h"
#include "hantekdso/enums.h"
#include "hantekdso/controlspecification.h"
#include "hantekprotocol/types.h"

#include <QObject>
#include <QString>

class DSOModel;
class ScopeDevice;
class HantekDsoControl;
struct DsoSettingsScope;

class IDsoControl : public QObject {
    Q_OBJECT

  public:
    explicit IDsoControl( QObject *parent = nullptr ) : QObject( parent ) {}
    ~IDsoControl() override = default;

    virtual void stateMachine() = 0;
    virtual void stopStateMachine() = 0;

    virtual double getSamplerate() const = 0;
    virtual unsigned getSamplesize() const = 0;
    virtual bool isSamplingUI() const = 0;
    virtual const ScopeDevice *getDevice() const = 0;
    virtual bool isDeviceAvailable() const = 0;
    virtual const DSOModel *getModel() const = 0;

    virtual Dso::ErrorCode stringCommand( const QString &commandString ) = 0;

    virtual void quitSampling() = 0;
    virtual void prepareForShutdown() = 0;

    /// RTTI-free substitute for dynamic_cast<HantekDsoControl*> (this build compiles Qt/moc code
    /// with -fno-rtti): returns this object cast to HantekDsoControl if that's the concrete type,
    /// nullptr otherwise. Only HantekDsoControl overrides it to return `this`; used by main.cpp to
    /// decide whether a CapturingThread is needed (Dso5200DsoControl runs its own capture thread).
    virtual HantekDsoControl *asHantekDsoControl() { return nullptr; }

  public slots:
    virtual void enableSamplingUI( bool enabled = true ) = 0;
    virtual Dso::ErrorCode setSamplerate( double samplerate = 0.0 ) = 0;
    virtual Dso::ErrorCode setRecordTime( double duration = 0.0 ) = 0;
    virtual Dso::ErrorCode setChannelUsed( ChannelID channel, bool used ) = 0;
    virtual Dso::ErrorCode setChannelInverted( ChannelID channel, bool inverted ) = 0;
    virtual Dso::ErrorCode setProbe( ChannelID channel, double probeAttn ) = 0;
    virtual Dso::ErrorCode setGain( ChannelID channel, double gain ) = 0;
    virtual Dso::ErrorCode setCoupling( ChannelID channel, Dso::Coupling coupling ) = 0;
    virtual Dso::ErrorCode setTriggerMode( Dso::TriggerMode mode ) = 0;
    virtual Dso::ErrorCode setTriggerSource( int channel ) = 0;
    virtual Dso::ErrorCode setTriggerSmooth( int smooth ) = 0;
    virtual Dso::ErrorCode setTriggerLevel( ChannelID channel, double level ) = 0;
    virtual Dso::ErrorCode setTriggerSlope( Dso::Slope slope ) = 0;
    virtual Dso::ErrorCode setTriggerPosition( double position ) = 0;
    virtual Dso::ErrorCode setCalFreq( double calfreq = 0.0 ) = 0;
    virtual void applySettings( const DsoSettingsScope *scope ) = 0;
    virtual void parkForReconnect() = 0;
    virtual void rebindDevice( ScopeDevice *newScopeDevice ) = 0;
    virtual void restartSampling() = 0;
    virtual void calibrateOffset( bool enable ) = 0;

  signals:
    void showSamplingStatus( bool enabled );
    void statusMessage( const QString &message, int timeout );
    void samplesAvailable( const DSOsamples *samples );
    void samplerateLimitsChanged( double minimum, double maximum );
    void samplerateSet( int mode, QList< double > sampleSteps );
    void samplerateCalculated( double samplerate, unsigned oversampling );
    void communicationError() const;
    void deviceAvailabilityChanged( bool available );
    void liveCalibrationError() const;
};
