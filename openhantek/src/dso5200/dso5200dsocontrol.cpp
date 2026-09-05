// SPDX-License-Identifier: GPL-2.0-or-later
//
// See dso5200dsocontrol.h. The samplerate/gain/trigger arithmetic and the
// 10-bit sample unpacking below are ported near-verbatim from dso5200basic's
// Dso5200Control (src/device/dso5200.cpp), validated against real DSO-5200A
// hardware; see that project's docs/GUIA_ESTUDIO.md for the reverse
// engineering history behind each constant and threshold below.

#include "dso5200dsocontrol.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef Q_OS_FREEBSD
#include <libusb.h>
#else
#include <libusb-1.0/libusb.h>
#endif

#include "hantekdso/dsomodel.h"
#include "protocol/states.h"
#include "usb/scopedevice.h"
#include "viewconstants.h"

using namespace Hantek5200;

namespace {
struct TimebaseEntry {
    uint8_t fast;
    uint16_t slow;
};

// See dso5200basic/src/device/dso5200.cpp for the full derivation: these register pairs were
// cross-checked against sigrok's hantek-dso driver and github.com/Blokkendoos/HantekDSO, then
// corrected (fast=2, not 4) after testing against real hardware showed the mathematically
// equivalent (fast=4) encoding produces only a single captured edge instead of a full window.
constexpr TimebaseEntry kSlowTimebaseTable[] = {
    { 3, 0xFFFF }, { 2, 0xFFFD }, { 2, 0xFFF8 }, { 2, 0xFFE9 }, { 2, 0xFFCF }, { 2, 0xFF9D },
    { 2, 0xFF08 }, { 2, 0xFE0E }, { 2, 0xFC1A }, { 2, 0xF63E }, { 2, 0xEC7A }, { 2, 0xD8F2 },
};
constexpr unsigned kSlowTimebaseTableSize = sizeof( kSlowTimebaseTable ) / sizeof( kSlowTimebaseTable[ 0 ] );
} // namespace

// gainId%3 pattern (with the gainId==0 exception) confirmed against a 2010 OpenHantek6022 fix
// (commit b935f4e, "Mostly DSO-5200 bugfixes, thanks to Ash"); voltsPerDiv values reconstructed
// from real relay click points + the official Windows software's USB traffic (dso5200basic
// docs/GUIA_ESTUDIO.md, "Octavo caso real").
const Dso5200DsoControl::GainStep Dso5200DsoControl::kGain[ 9 ] = {
    { 1, 0.02 }, { 1, 0.05 }, { 0, 0.1 }, { 0, 0.2 }, { 1, 0.5 }, { 0, 1.0 }, { 0, 2.0 }, { 1, 5.0 }, { 0, 10.0 },
};

const unsigned short Dso5200DsoControl::kVoltageLimit[ 9 ] = { 184, 368, 454, 908, 368, 454, 908, 368, 454 };

Dso5200DsoControl::Dso5200DsoControl( ScopeDevice *scopeDevice, const DSOModel *model, int verboseLevel )
    : device( scopeDevice ), model( model ), verboseLevel( verboseLevel ) {
    result.data.resize( kChannels + 1 ); // CH1, CH2, (empty) MATH slot - no math channel on this device

    retrieveChannelLevelData();

    // The DSO-5200's ETsrBits.triggerSource field uses an inverted channel mapping - left at its
    // zero-initialized default the trigger comparator watches CH2 instead of CH1. trigger.source
    // is fixed to channel 0 here at startup, same as dso5200basic.
    triggerCommand.setTriggerSource( (uint8_t)( 1 - trigger.source ) );
    bulkCommand( triggerCommand );

    doSetChannelUsed( 0, true );
    doSetChannelUsed( 1, true );
    doSetGain( 0, kGain[ voltage[ 0 ].gain ].voltsPerDiv );
    doSetGain( 1, kGain[ voltage[ 1 ].gain ].voltsPerDiv );
    triggerCommand.setTriggerSlope( trigger.slope );
    bulkCommand( triggerCommand );

    bufferCommand.setUsedPre( DTriggerPositionUsed::ON );
    bufferCommand.setUsedPost( DTriggerPositionUsed::ON );
    bufferCommand.setRecordLength( 1 );
    bulkCommand( bufferCommand );

    doSetRecordTime( targetTimebaseSeconds );

    running = true;
    worker = std::thread( &Dso5200DsoControl::threadMain, this );
}

Dso5200DsoControl::~Dso5200DsoControl() {
    running = false;
    if ( worker.joinable() )
        worker.join();
}

const ScopeDevice *Dso5200DsoControl::getDevice() const { return device; }

bool Dso5200DsoControl::isDeviceAvailable() const { return device != nullptr && device->isConnected(); }

void Dso5200DsoControl::setLastError( const QString &message ) {
    std::lock_guard< std::mutex > lock( statusMutex );
    lastError = message;
    emit statusMessage( message, 5000 );
}

Dso::ErrorCode Dso5200DsoControl::stringCommand( const QString & ) {
    // No raw hex command console for this device yet (the DSO-5200's command shape - split
    // bulk+control commands with a mandatory CONTROL_BEGINCOMMAND prelude - doesn't map onto
    // HantekDsoControl's single "cc <hex code> <hex data>" control-only syntax).
    return Dso::ErrorCode::UNSUPPORTED;
}

void Dso5200DsoControl::quitSampling() {
    std::lock_guard< std::mutex > lock( ioMutex );
    sampling = false;
}

void Dso5200DsoControl::enableSamplingUI( bool enabled ) {
    samplingUI = enabled;
    emit showSamplingStatus( enabled );
    std::lock_guard< std::mutex > lock( ioMutex );
    sampling = enabled;
}

void Dso5200DsoControl::restartSampling() {
    std::lock_guard< std::mutex > lock( ioMutex );
    samplingStarted = false; // forces threadMain() to re-issue BulkCaptureStart on its next cycle
}

Dso::ErrorCode Dso5200DsoControl::setCalFreq( double ) { return Dso::ErrorCode::UNSUPPORTED; }

void Dso5200DsoControl::calibrateOffset( bool ) {
    emit statusMessage( tr( "The DSO-5200/5200A has no live EEPROM calibration." ), 5000 );
}

void Dso5200DsoControl::parkForReconnect() {
    std::lock_guard< std::mutex > lock( ioMutex );
    device = nullptr;
}

void Dso5200DsoControl::rebindDevice( ScopeDevice *newScopeDevice ) {
    std::lock_guard< std::mutex > lock( ioMutex );
    device = newScopeDevice;
    samplingStarted = false;
    captureState = 0; // Hantek::CAPTURE_WAITING
    cycleCounter = 0;
}

Dso::ErrorCode Dso5200DsoControl::setRecordTime( double duration ) {
    if ( duration <= 0.0 )
        duration = targetTimebaseSeconds;
    std::lock_guard< std::mutex > lock( ioMutex );
    targetTimebaseSeconds = duration;
    return doSetRecordTime( duration ) ? Dso::ErrorCode::NONE : Dso::ErrorCode::PARAMETER;
}

Dso::ErrorCode Dso5200DsoControl::setSamplerate( double samplerate ) {
    if ( samplerate <= 0.0 )
        return setRecordTime( targetTimebaseSeconds );
    std::lock_guard< std::mutex > lock( ioMutex );
    unsigned downsamplerValue = 0;
    getBestSamplerate( samplerate, usedChannels <= 1, false, &downsamplerValue );
    bool ok = updateSamplerate( downsamplerValue, usedChannels <= 1 );
    emit samplerateCalculated( samplerateCurrent, 1 );
    return ok ? Dso::ErrorCode::NONE : Dso::ErrorCode::PARAMETER;
}

Dso::ErrorCode Dso5200DsoControl::setChannelUsed( ChannelID channel, bool used ) {
    if ( channel >= kChannels )
        return Dso::ErrorCode::PARAMETER;
    std::lock_guard< std::mutex > lock( ioMutex );
    return doSetChannelUsed( channel, used ) ? Dso::ErrorCode::NONE : Dso::ErrorCode::PARAMETER;
}

Dso::ErrorCode Dso5200DsoControl::setChannelInverted( ChannelID channel, bool inverted ) {
    if ( channel >= kChannels )
        return Dso::ErrorCode::PARAMETER;
    voltage[ channel ].inverted = inverted; // applied in convertRawDataToSamples()
    return Dso::ErrorCode::NONE;
}

Dso::ErrorCode Dso5200DsoControl::setProbe( ChannelID channel, double probeAttn ) {
    if ( channel >= kChannels )
        return Dso::ErrorCode::PARAMETER;
    voltage[ channel ].probeAttn = probeAttn; // applied in convertRawDataToSamples()
    return Dso::ErrorCode::NONE;
}

Dso::ErrorCode Dso5200DsoControl::setGain( ChannelID channel, double gain ) {
    if ( channel >= kChannels || gain <= 0.0 )
        return Dso::ErrorCode::PARAMETER;
    std::lock_guard< std::mutex > lock( ioMutex );
    return doSetGain( channel, gain ) ? Dso::ErrorCode::NONE : Dso::ErrorCode::PARAMETER;
}

Dso::ErrorCode Dso5200DsoControl::setCoupling( ChannelID channel, Dso::Coupling coupling ) {
    if ( channel >= kChannels )
        return Dso::ErrorCode::PARAMETER;
    std::lock_guard< std::mutex > lock( ioMutex );
    voltage[ channel ].coupling = coupling;
    applyRelays( channel );
    return doSetOffset( channel, voltage[ channel ].offset ) ? Dso::ErrorCode::NONE : Dso::ErrorCode::PARAMETER;
}

Dso::ErrorCode Dso5200DsoControl::setTriggerMode( Dso::TriggerMode mode ) {
    std::lock_guard< std::mutex > lock( ioMutex );
    trigger.mode = mode;
    return Dso::ErrorCode::NONE;
}

Dso::ErrorCode Dso5200DsoControl::setTriggerSource( int channel ) {
    if ( channel < 0 || channel >= (int)kChannels )
        return Dso::ErrorCode::PARAMETER;
    std::lock_guard< std::mutex > lock( ioMutex );
    trigger.source = (unsigned)channel;
    triggerCommand.setTriggerSource( (uint8_t)( 1 - trigger.source ) ); // inverted mapping, see constructor
    bulkCommand( triggerCommand );
    return Dso::ErrorCode::NONE;
}

Dso::ErrorCode Dso5200DsoControl::setTriggerSmooth( int ) {
    return Dso::ErrorCode::UNSUPPORTED; // no smoothing register in this protocol
}

Dso::ErrorCode Dso5200DsoControl::setTriggerLevel( ChannelID channel, double level ) {
    if ( channel >= kChannels )
        return Dso::ErrorCode::PARAMETER;
    std::lock_guard< std::mutex > lock( ioMutex );
    return doSetTriggerLevel( channel, level ) ? Dso::ErrorCode::NONE : Dso::ErrorCode::PARAMETER;
}

Dso::ErrorCode Dso5200DsoControl::setTriggerSlope( Dso::Slope slope ) {
    std::lock_guard< std::mutex > lock( ioMutex );
    trigger.slope = slope == Dso::Slope::Negative ? 1 : 0;
    triggerCommand.setTriggerSlope( trigger.slope );
    bulkCommand( triggerCommand );
    return Dso::ErrorCode::NONE;
}

Dso::ErrorCode Dso5200DsoControl::setTriggerPosition( double position ) {
    std::lock_guard< std::mutex > lock( ioMutex );
    return doSetPretriggerPosition( position ) ? Dso::ErrorCode::NONE : Dso::ErrorCode::PARAMETER;
}

void Dso5200DsoControl::applySettings( const DsoSettingsScope *newScope ) {
    std::lock_guard< std::mutex > lock( ioMutex );
    scope = newScope;
    if ( !scope )
        return;
    for ( ChannelID ch = 0; ch < kChannels && ch < scope->voltage.size(); ++ch ) {
        doSetChannelUsed( ch, scope->voltage[ ch ].used );
        doSetGain( ch, scope->gain( ch ) );
        voltage[ ch ].inverted = scope->voltage[ ch ].inverted;
        voltage[ ch ].probeAttn = scope->voltage[ ch ].probeAttn;
    }
    trigger.mode = scope->trigger.mode;
    trigger.slope = scope->trigger.slope == Dso::Slope::Negative ? 1 : 0;
    triggerCommand.setTriggerSlope( trigger.slope );
    bulkCommand( triggerCommand );
    if ( scope->trigger.source >= 0 && scope->trigger.source < (int)kChannels ) {
        trigger.source = (unsigned)scope->trigger.source;
        triggerCommand.setTriggerSource( (uint8_t)( 1 - trigger.source ) );
        bulkCommand( triggerCommand );
    }
    doSetPretriggerPosition( scope->trigger.position );
    doSetRecordTime( scope->horizontal.timebase * DIVS_TIME );
}

// --- Ported device logic ----------------------------------------------------

double Dso5200DsoControl::getBestSamplerate( double samplerate, bool fastRate, bool maximum,
                                             unsigned *downsamplerOut ) const {
    if ( samplerate <= 0.0 )
        return 0.0;

    const double base = fastRate ? kMultiBase : kSingleBase;
    const double max = fastRate ? kMultiMax : kSingleMax;

    double bestDownsampler = base / kBufferDivider / samplerate;
    double bestSamplerate;

    if ( bestDownsampler < 1.0 && ( samplerate <= max / kBufferDivider || !maximum ) ) {
        bestDownsampler = 0.0;
        bestSamplerate = max / kBufferDivider;
    } else {
        bestDownsampler = maximum ? std::ceil( bestDownsampler ) : std::floor( bestDownsampler );
        if ( bestDownsampler > kMaxDownsampler )
            bestDownsampler = kMaxDownsampler;
        bestSamplerate = base / bestDownsampler / kBufferDivider;
    }

    if ( downsamplerOut )
        *downsamplerOut = (unsigned)bestDownsampler;
    return bestSamplerate;
}

bool Dso5200DsoControl::updateSamplerate( unsigned downsamplerValue, bool fastRate ) {
    const double base = fastRate ? kMultiBase : kSingleBase;
    const double max = fastRate ? kMultiMax : kSingleMax;

    const long int valueSlow = std::max( ( (long int)downsamplerValue - 3 ) / 2, (long int)0 );
    const unsigned char valueFast = (unsigned char)( (long int)downsamplerValue - valueSlow * 2 );

    samplerateCommand.setSamplerateFast( (uint8_t)( 4 - valueFast ) );
    samplerateCommand.setSamplerateSlow( valueSlow == 0 ? 0 : (uint16_t)( 0xffff - valueSlow ) );
    triggerCommand.setFastRate( fastRate );

    bulkCommand( samplerateCommand );
    bulkCommand( triggerCommand );

    fastRateActive = fastRate;
    downsampler = downsamplerValue;
    if ( downsamplerValue )
        samplerateCurrent = base / kBufferDivider / downsamplerValue;
    else
        samplerateCurrent = max / kBufferDivider;

    doSetPretriggerPosition( trigger.position );
    return true;
}

bool Dso5200DsoControl::applyTimebaseTableEntry( unsigned index, double *windowOut ) {
    const TimebaseEntry &entry = kSlowTimebaseTable[ index ];
    const unsigned divider = 2u * ( 0x10000u - entry.slow ) + 4u - entry.fast;
    const double sr = kSingleMax / divider;

    samplerateCommand.setSamplerateFast( entry.fast );
    samplerateCommand.setSamplerateSlow( entry.slow );
    triggerCommand.setFastRate( false );

    bulkCommand( samplerateCommand );
    bulkCommand( triggerCommand );

    fastRateActive = false;
    downsampler = divider;
    samplerateCurrent = sr;
    if ( windowOut )
        *windowOut = (double)kRecordLengthSingle / sr;

    doSetPretriggerPosition( trigger.position );
    return true;
}

bool Dso5200DsoControl::doSetRecordTime( double duration ) {
    if ( duration <= 0.0 )
        return false;

    unsigned bestIndex = 0;
    double bestWindow = 0.0, bestError = 1e300;
    for ( unsigned i = 0; i < kSlowTimebaseTableSize; ++i ) {
        const TimebaseEntry &entry = kSlowTimebaseTable[ i ];
        const unsigned divider = 2u * ( 0x10000u - entry.slow ) + 4u - entry.fast;
        const double window = (double)kRecordLengthSingle * divider / kSingleMax;
        const double error = std::abs( std::log( window / duration ) );
        if ( error < bestError ) {
            bestError = error;
            bestIndex = i;
            bestWindow = window;
        }
    }

    if ( bestIndex == 0 && duration < bestWindow && usedChannels <= 1 ) {
        const double targetSamplerate = (double)kRecordLengthSingle / duration;
        unsigned downsamplerValue = 0;
        getBestSamplerate( targetSamplerate, true, false, &downsamplerValue );
        bool ok = updateSamplerate( downsamplerValue, true );
        emit samplerateCalculated( samplerateCurrent, 1 );
        return ok;
    }

    bool ok = applyTimebaseTableEntry( bestIndex, nullptr );
    emit samplerateCalculated( samplerateCurrent, 1 );
    return ok;
}

void Dso5200DsoControl::applyRelays( unsigned channel ) {
    if ( voltage[ channel ].coupling == Dso::Coupling::GND ) {
        // Isolated USB capture confirmed this exact signature (dso5200basic docs/GUIA_ESTUDIO.md):
        // grounding the input forces GainBits=0 and both decade relays open, independent of
        // whatever gain step was selected. The AC/DC coupling relay itself wasn't part of that
        // capture (GND and AC/DC are separate switches on the real front panel) - left at DC here
        // as the safer default; verify against hardware before relying on it.
        gainCommand.setGain( channel, 0 );
        relaysCommand.setBelow1V( channel, false );
        relaysCommand.setBelow100mV( channel, false );
        relaysCommand.setCoupling( channel, true );
    } else {
        const unsigned gainID = voltage[ channel ].gain;
        gainCommand.setGain( channel, kGain[ gainID ].index );
        relaysCommand.setBelow1V( channel, gainID >= 3 );
        relaysCommand.setBelow100mV( channel, gainID >= 3 && gainID <= 5 );
        relaysCommand.setCoupling( channel, voltage[ channel ].coupling == Dso::Coupling::DC );
    }
    bulkCommand( gainCommand );
    device->controlWrite( &relaysCommand );
}

bool Dso5200DsoControl::doSetGain( unsigned channel, double gain ) {
    unsigned gainID;
    for ( gainID = 0; gainID < 8; ++gainID )
        if ( kGain[ gainID ].voltsPerDiv >= gain )
            break;

    voltage[ channel ].gain = gainID;
    applyRelays( channel );

    return doSetOffset( channel, voltage[ channel ].offset );
}

bool Dso5200DsoControl::doSetOffset( unsigned channel, double offset ) {
    Offset &limit = offsetLimit[ channel ].step[ voltage[ channel ].gain ];
    const unsigned short minimum =
        ( (unsigned short)*( (unsigned char *)&limit.start ) << 8 ) + *( (unsigned char *)&limit.start + 1 );
    const unsigned short maximum =
        ( (unsigned short)*( (unsigned char *)&limit.end ) << 8 ) + *( (unsigned char *)&limit.end + 1 );
    const unsigned short offsetValue = (unsigned short)( offset * ( maximum - minimum ) + minimum + 0.5 );
    const double offsetReal = (double)( offsetValue - minimum ) / ( maximum - minimum );

    offsetCommand.setChannel( channel, offsetValue );
    voltage[ channel ].offsetReal = offsetReal;
    voltage[ channel ].offset = offset;

    if ( !device )
        return false;
    device->controlWrite( &offsetCommand );

    return doSetTriggerLevel( channel, trigger.level[ channel ] );
}

bool Dso5200DsoControl::doSetTriggerLevel( unsigned channel, double level ) {
    trigger.level[ channel ] = level;

    Offset &limit = offsetLimit[ channel ].step[ voltage[ channel ].gain ];
    const unsigned short minimum =
        ( (unsigned short)*( (unsigned char *)&limit.start ) << 8 ) + *( (unsigned char *)&limit.start + 1 );
    const unsigned short maximum =
        ( (unsigned short)*( (unsigned char *)&limit.end ) << 8 ) + *( (unsigned char *)&limit.end + 1 );

    const double offsetReal = voltage[ channel ].offsetReal;
    const double gainStep = kGain[ voltage[ channel ].gain ].voltsPerDiv;
    unsigned short levelValue =
        (unsigned short)( ( ( offsetReal + level / gainStep ) * ( maximum - minimum ) + 0.5 ) + minimum );
    levelValue = std::clamp( levelValue, minimum, maximum );

    if ( channel == trigger.source ) {
        offsetCommand.setTrigger( levelValue );
        if ( device )
            device->controlWrite( &offsetCommand );
    }
    return true;
}

bool Dso5200DsoControl::doSetChannelUsed( unsigned channel, bool used ) {
    voltage[ channel ].used = used;
    unsigned channelCount = ( voltage[ 0 ].used ? 1u : 0u ) + ( voltage[ 1 ].used ? 1u : 0u );

    UsedChannels usedChannelsValue = UsedChannels::USED_CH1;
    if ( voltage[ 1 ].used )
        usedChannelsValue = voltage[ 0 ].used ? UsedChannels::USED_CH1CH2 : UsedChannels::USED_CH2;

    triggerCommand.setUsedChannels( (uint8_t)usedChannelsValue );
    bulkCommand( triggerCommand );

    const bool fastRateChanged = ( usedChannels <= 1 ) != ( channelCount <= 1 );
    usedChannels = channelCount;

    if ( fastRateChanged )
        doSetRecordTime( targetTimebaseSeconds );

    return true;
}

bool Dso5200DsoControl::doSetPretriggerPosition( double position ) {
    const unsigned recordLength = getRecordLength();

    double positionSamples = position * recordLength;
    if ( isFastRate() )
        positionSamples /= kChannels;

    const unsigned positionPre = 0xffff - recordLength + (unsigned)positionSamples;
    const unsigned positionPost = 0xffff - (unsigned)positionSamples;

    bufferCommand.setTriggerPositionPre( (uint16_t)positionPre );
    bufferCommand.setTriggerPositionPost( (uint16_t)positionPost );
    bulkCommand( bufferCommand );

    trigger.position = position;
    return true;
}

bool Dso5200DsoControl::retrieveChannelLevelData() {
    if ( !device )
        return false;
    int errorCode = device->controlRead( &getLimitsCommand );
    if ( errorCode < 0 ) {
        setLastError( tr( "Could not read channel limits from the oscilloscope: %1" ).arg( errorCode ) );
        return false;
    }
    std::memcpy( offsetLimit, getLimitsCommand.data(), sizeof( OffsetsPerGainStep ) * kChannels );
    return true;
}

int Dso5200DsoControl::bulkCommand( const std::vector< uint8_t > &command, int attempts ) {
    if ( !device )
        return LIBUSB_ERROR_NO_DEVICE;
    int errorCode = device->controlWrite( &beginCommand );
    if ( errorCode < 0 )
        return errorCode;
    return device->bulkWrite( command.data(), (unsigned)command.size(), attempts );
}

std::pair< int, unsigned > Dso5200DsoControl::getCaptureState() {
    if ( !device )
        return { (int)CAPTURE_ERROR, 0 };
    int errorCode = bulkCommand( BulkGetCaptureState(), 1 );
    if ( errorCode < 0 )
        return { (int)CAPTURE_ERROR, 0 };

    BulkResponseGetCaptureState response;
    errorCode = device->bulkRead( &response );
    if ( errorCode < 0 )
        return { (int)CAPTURE_ERROR, 0 };

    return { (int)response.getCaptureState(), response.getTriggerPoint() };
}

std::vector< unsigned char > Dso5200DsoControl::getSamples( unsigned &previousSampleCount ) {
    if ( !device )
        return {};
    int errorCode = bulkCommand( BulkGetData(), 1 );
    if ( errorCode < 0 ) {
        setLastError( tr( "Reading sample data failed: %1" ).arg( errorCode ) );
        return {};
    }

    unsigned totalSampleCount = getSampleCount();
    if ( totalSampleCount < previousSampleCount )
        std::swap( totalSampleCount, previousSampleCount );
    else
        previousSampleCount = totalSampleCount;

    const unsigned dataLength = totalSampleCount * 2; // kSampleSize (10) > 8

    std::vector< unsigned char > data( dataLength );
    unsigned received = 0;
    int rc = device->bulkReadMulti( data.data(), dataLength, false, received );
    if ( rc < 0 ) {
        setLastError( tr( "Reading sample data failed: %1" ).arg( rc ) );
        return {};
    }
    data.resize( received );
    return data;
}

void Dso5200DsoControl::convertRawDataToSamples( const std::vector< unsigned char > &rawData ) {
    result.samplerate = samplerateCurrent;
    result.liveTrigger = true;
    result.triggeredPosition = 0;
    result.clipped = 0;
    for ( auto &v : result.data )
        v.clear();

    if ( rawData.size() < 2 )
        return;
    const size_t totalSampleCount = rawData.size() / 2; // kSampleSize (10) > 8

    const unsigned extraBitsSize = kSampleSize - 8; // 2
    const unsigned short extraBitsMask = ( 0x00ff << extraBitsSize ) & 0xff00;

    std::vector< std::vector< double > > data( kChannels );

    if ( isFastRate() ) {
        unsigned channel = 0;
        for ( ; channel < kChannels; ++channel )
            if ( voltage[ channel ].used )
                break;
        if ( channel >= kChannels )
            return;

        data[ channel ].resize( totalSampleCount );

        const unsigned gainID = voltage[ channel ].gain;
        const unsigned short limit = kVoltageLimit[ gainID ];
        const double offset = voltage[ channel ].offsetReal;
        const double gainStep = kGain[ gainID ].voltsPerDiv;

        unsigned bufferPosition = trigger.point * 2;
        for ( unsigned pos = 0; pos < totalSampleCount; ++pos, ++bufferPosition ) {
            if ( bufferPosition >= totalSampleCount )
                bufferPosition %= totalSampleCount;

            const unsigned short low = rawData[ bufferPosition ];
            const unsigned extraBitsPosition = bufferPosition % kChannels;
            const unsigned shift = 8 - ( kChannels - 1 - extraBitsPosition ) * extraBitsSize;
            const unsigned short high =
                ( (unsigned short)rawData[ totalSampleCount + bufferPosition - extraBitsPosition ] << shift ) &
                extraBitsMask;

            data[ channel ][ pos ] = ( (double)( low + high ) / limit - offset ) * gainStep;
        }
    } else {
        for ( unsigned channel = 0; channel < kChannels; ++channel ) {
            data[ channel ].resize( totalSampleCount / kChannels );

            const unsigned gainID = voltage[ channel ].gain;
            const unsigned short limit = kVoltageLimit[ gainID ];
            const double offset = voltage[ channel ].offsetReal;
            const double gainStep = kGain[ gainID ].voltsPerDiv;
            const unsigned extraBitsIndex = 8 - channel * 2;

            unsigned bufferPosition = trigger.point * 2;
            for ( unsigned realPosition = 0; realPosition < data[ channel ].size();
                 ++realPosition, bufferPosition += kChannels ) {
                if ( bufferPosition >= totalSampleCount )
                    bufferPosition %= totalSampleCount;

                const unsigned short low = rawData[ bufferPosition + kChannels - 1 - channel ];
                const unsigned short high =
                    ( (unsigned short)rawData[ totalSampleCount + bufferPosition ] << extraBitsIndex ) & extraBitsMask;

                data[ channel ][ realPosition ] = ( (double)( low + high ) / limit - offset ) * gainStep;
            }
        }
    }

    for ( unsigned ch = 0; ch < kChannels; ++ch ) {
        if ( !voltage[ ch ].used || data[ ch ].empty() )
            continue;
        const double sign = voltage[ ch ].inverted ? -1.0 : 1.0;
        const double probeAttn = voltage[ ch ].probeAttn;
        result.data[ ch ].resize( data[ ch ].size() );
        for ( size_t i = 0; i < data[ ch ].size(); ++i )
            result.data[ ch ][ i ] = sign * data[ ch ][ i ] * probeAttn;
    }
}

void Dso5200DsoControl::threadMain() {
    while ( running.load() ) {
        int sleepMs = 100;

        if ( sampling.load() ) {
            std::lock_guard< std::mutex > lock( ioMutex );

            if ( !device || !device->isConnected() ) {
                std::this_thread::sleep_for( std::chrono::milliseconds( sleepMs ) );
                continue;
            }

            unsigned triggerPoint;
            std::tie( captureState, triggerPoint ) = getCaptureState();
            trigger.point = triggerPoint; // raw value - no bit-decode needed on the DSO-5200(A), see dso5200basic

            if ( captureState == (int)CAPTURE_ERROR ) {
                setLastError( tr( "Could not read the capture state" ) );
                emit communicationError();
            } else {
                switch ( captureState ) {
                case CAPTURE_READY5200: {
                    std::vector< unsigned char > rawData = getSamples( expectedSampleCount );
                    if ( samplingStarted && !rawData.empty() ) {
                        convertRawDataToSamples( rawData );
                        if ( samplingUI )
                            emit samplesAvailable( &result );
                    }
                    samplingStarted = false;

                    if ( !sampling.load() )
                        break;
                    [[fallthrough]];
                }
                case CAPTURE_WAITING:
                case CAPTURE_SAMPLING:
                    expectedSampleCount = getSampleCount();

                    if ( samplingStarted ) {
                        ++cycleCounter;

                        if ( cycleCounter == startCycle ) {
                            bulkCommand( BulkTriggerEnabled() );
                        } else if ( cycleCounter >= 8 + startCycle ) {
                            if ( trigger.mode != Dso::TriggerMode::SINGLE )
                                bulkCommand( BulkForceTrigger() );
                        }

                        if ( cycleCounter < 20 || cycleCounter < 4000 / cycleTimeMs )
                            break;
                    }

                    bulkCommand( BulkCaptureStart() );
                    samplingStarted = true;
                    cycleCounter = 0;
                    startCycle = (int)( trigger.position *
                                            ( (double)getRecordLength() / samplerateCurrent * 1000.0 ) / cycleTimeMs +
                                        1.0 );
                    break;

                default:
                    break;
                }
            }

            cycleTimeMs = std::clamp( (int)( (double)getRecordLength() / samplerateCurrent * 250 ), 10, 1000 );
            sleepMs = cycleTimeMs;
        }

        std::this_thread::sleep_for( std::chrono::milliseconds( sleepMs ) );
    }
}
