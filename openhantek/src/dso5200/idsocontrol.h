// SPDX-License-Identifier: GPL-2.0-or-later
//
// Interfaz entre la GUI (mainwindow, main.cpp, devicereconnectionsupervisor) y el control del
// osciloscopio. Nació extrayendo la parte pública de HantekDsoControl (el control del 6022 del
// upstream) para poder enchufar Dso5200DsoControl; el 6022 ya se quitó y su única implementación es
// Dso5200DsoControl. Se mantiene la interfaz para no atar la GUI al adaptador.

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

    virtual void quitSampling() = 0;
    virtual void prepareForShutdown() = 0;

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
    virtual Dso::ErrorCode setTriggerLevel( ChannelID channel, double level ) = 0;
    virtual Dso::ErrorCode setTriggerSlope( Dso::Slope slope ) = 0;
    virtual Dso::ErrorCode setTriggerPosition( double position ) = 0;
    virtual void applySettings( const DsoSettingsScope *scope ) = 0;
    virtual void parkForReconnect() = 0;
    virtual void rebindDevice( ScopeDevice *newScopeDevice ) = 0;
    virtual void restartSampling() = 0;

  signals:
    void showSamplingStatus( bool enabled );
    void statusMessage( const QString &message, int timeout );
    void samplesAvailable( const DSOsamples *samples );
    void samplerateLimitsChanged( double minimum, double maximum );
    void samplerateSet( int mode, QList< double > sampleSteps );
    void samplerateCalculated( double samplerate, unsigned oversampling );
    void communicationError() const;
    void deviceAvailabilityChanged( bool available );
};
