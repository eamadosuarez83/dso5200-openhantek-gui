// SPDX-License-Identifier: GPL-2.0-or-later
//
// DSO-5200/5200A device control, implementing the IDsoControl interface so
// this GUI's mainwindow/docks/exporters/post-processing can drive it exactly
// like the original HantekDsoControl (see idsocontrol.h).
//
// This is NOT a HantekDsoControl subclass and does not use its control[]
// pending-command dispatcher, its CapturingThread collaborator, or the 6022
// hantekprotocol structs - the DSO-5200 speaks a structurally different
// protocol (split CSETTRIGGERORSAMPLERATE/ESETTRIGGERORSAMPLERATE bulk
// commands, on-demand GETCAPTURESTATE polling + GETDATA bulk reads, 10-bit
// split-byte sample encoding) that doesn't fit that machinery. Instead, the
// core state-machine/protocol logic below is ported near-verbatim from
// dso5200basic's Dso5200Control (see that project's src/device/dso5200.h/.cpp),
// which was reconstructed and validated against real DSO-5200A hardware over
// an extended debugging session (see dso5200basic/docs/GUIA_ESTUDIO.md) -
// preserving that logic untouched was judged lower-risk than re-deriving it
// against HantekDsoControl's shape.
//
// Deliberate simplifications versus HantekDsoControl, left as follow-ups:
//  - No roll mode (the ported Dso5200Control never implemented it either).
//  - No math channel, no live/EEPROM calibration, no calibration-frequency
//    generator - none exist on this hardware in the way the 6022 has them.
//  - Capture polling runs on a plain std::thread owned by this class
//    (started in the constructor, like Dso5200Control), not on a separate
//    CapturingThread/QThread - see main.cpp's isDso5200 branch.
//  - setProbe()/setChannelInverted()/setTriggerSmooth() are accepted and
//    stored but don't change any device behavior yet (the DSO-5200 protocol
//    has no smoothing register, and the ported class never scaled for probe
//    attenuation beyond bookkeeping) - safe no-ops, not silently wrong.

#pragma once

#include <atomic>
#include <mutex>
#include <thread>

#include "idsocontrol.h"
#include "protocol/bulkStructs.h"
#include "protocol/controlStructs.h"
#include "protocol/definitions.h"

#include "hantekdso/dsosamples.h"
#include "scopesettings.h"

class ScopeDevice;
class DSOModel;

class Dso5200DsoControl : public IDsoControl {
    Q_OBJECT

  public:
    /// \param scopeDevice Must already be connected. Not owned.
    explicit Dso5200DsoControl( ScopeDevice *scopeDevice, const DSOModel *model, int verboseLevel );
    ~Dso5200DsoControl() override;

    void stateMachine() override {} // all real work happens on the internal capture thread, see threadMain()
    void stopStateMachine() override {}

    double getSamplerate() const override { return samplerateCurrent; }
    unsigned getSamplesize() const override { return getRecordLength(); }
    bool isSamplingUI() const override { return samplingUI; }
    const ScopeDevice *getDevice() const override;
    bool isDeviceAvailable() const override;
    const DSOModel *getModel() const override { return model; }

    Dso::ErrorCode stringCommand( const QString &commandString ) override;

    void quitSampling() override;
    void prepareForShutdown() override {} // nothing to persist: no EEPROM/calibration file on this device

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
    Dso::ErrorCode setTriggerSmooth( int smooth ) override;
    Dso::ErrorCode setTriggerLevel( ChannelID channel, double level ) override;
    Dso::ErrorCode setTriggerSlope( Dso::Slope slope ) override;
    Dso::ErrorCode setTriggerPosition( double position ) override;
    Dso::ErrorCode setCalFreq( double calfreq = 0.0 ) override; // unsupported: no cal-freq generator, returns UNSUPPORTED
    void applySettings( const DsoSettingsScope *scope ) override;
    void parkForReconnect() override;
    void rebindDevice( ScopeDevice *newScopeDevice ) override;
    void restartSampling() override;
    void calibrateOffset( bool enable ) override; // unsupported: no live/EEPROM calibration on this device

  private:
    struct VoltageSettings {
        double offset = 0.0;
        double offsetReal = 0.0;
        unsigned gain = 5; // index into kGain, defaults to the 1.00 V/div step
        bool used = true;
        bool inverted = false;
        double probeAttn = 1.0;
        Dso::Coupling coupling = Dso::Coupling::DC;
    };
    struct TriggerSettings {
        double level[ 2 ] = { 0.0, 0.0 };
        double position = 0.5; // pretrigger position, 0..1 screen fraction (0.5 = centered)
        unsigned point = 0;
        Dso::TriggerMode mode = Dso::TriggerMode::AUTO;
        uint8_t slope = 0; // 0 = rising, 1 = falling
        unsigned source = 0; // 0 = CH1, 1 = CH2
    };

    // --- Ported from Dso5200Control (dso5200basic/src/device/dso5200.{h,cpp}) ---
    bool isFastRate() const { return fastRateActive; }
    unsigned getRecordLength() const { return fastRateActive ? kRecordLengthMulti : kRecordLengthSingle; }
    unsigned getSampleCount() const { return isFastRate() ? getRecordLength() : getRecordLength() * kChannels; }
    double getBestSamplerate( double samplerate, bool fastRate, bool maximum, unsigned *downsamplerOut ) const;
    bool updateSamplerate( unsigned downsampler, bool fastRate );
    bool doSetRecordTime( double duration );
    bool applyTimebaseTableEntry( unsigned index, double *windowOut );
    void applyRelays( unsigned channel ); // recomputes GainBits/below1V/below100mV/coupling from voltage[channel]
    bool doSetGain( unsigned channel, double gain );
    bool doSetOffset( unsigned channel, double offset );
    bool doSetTriggerLevel( unsigned channel, double level );
    bool doSetChannelUsed( unsigned channel, bool used );
    bool doSetPretriggerPosition( double position );
    bool retrieveChannelLevelData();
    std::pair< int, unsigned > getCaptureState();
    std::vector< unsigned char > getSamples( unsigned &previousSampleCount );
    void convertRawDataToSamples( const std::vector< unsigned char > &rawData );
    int bulkCommand( const std::vector< uint8_t > &command, int attempts = 3 );
    void setLastError( const QString &message );

    void threadMain();

    ScopeDevice *device;
    const DSOModel *model;
    int verboseLevel;
    mutable std::mutex ioMutex; // serializes USB access between GUI-triggered setters and the polling thread

    static constexpr unsigned kChannels = 2;
    static constexpr unsigned char kSampleSize = 10;
    static constexpr double kSingleBase = 100e6, kSingleMax = 125e6;
    static constexpr double kMultiBase = 200e6, kMultiMax = 250e6;
    static constexpr unsigned kMaxDownsampler = 131072;
    static constexpr unsigned kBufferDivider = 1;
    static constexpr unsigned kRecordLengthSingle = 10240;
    static constexpr unsigned kRecordLengthMulti = 20480;
    struct GainStep {
        unsigned char index;
        double voltsPerDiv;
    };
    static const GainStep kGain[ 9 ];
    static const unsigned short kVoltageLimit[ 9 ];

    VoltageSettings voltage[ kChannels ];
    TriggerSettings trigger;
    unsigned usedChannels = kChannels;
    unsigned downsampler = 1;
    double samplerateCurrent = 1e8;
    bool fastRateActive = false;
    Hantek5200::OffsetsPerGainStep offsetLimit[ kChannels ];

    Hantek5200::ControlBeginCommand beginCommand;
    Hantek5200::ControlGetLimits getLimitsCommand{ kChannels };
    Hantek5200::BulkSetSamplerate5200 samplerateCommand;
    Hantek5200::BulkSetBuffer5200 bufferCommand;
    Hantek5200::BulkSetTrigger5200 triggerCommand;
    Hantek5200::BulkSetGain gainCommand;
    Hantek5200::ControlSetOffset offsetCommand;
    Hantek5200::ControlSetRelays relaysCommand;

    std::atomic< bool > sampling{ false }; // hardware capture cycle armed (start/stop of the actual acquisition)
    bool samplingUI = true;                // GUI-level enable (enableSamplingUI()); gates samplesAvailable() emission
    std::atomic< bool > running{ false };
    std::thread worker;

    unsigned expectedSampleCount = 0;
    int captureState = 0; // Hantek5200::CAPTURE_WAITING
    bool samplingStarted = false;
    int cycleCounter = 0;
    int startCycle = 0;
    int cycleTimeMs = 100;
    uint32_t frameCounter = 0;
    double targetTimebaseSeconds = 0.001;

    const DsoSettingsScope *scope = nullptr;
    DSOsamples result;

    mutable std::mutex statusMutex;
    QString lastError;
};
