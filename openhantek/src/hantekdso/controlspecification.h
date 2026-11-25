// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "enums.h"
#include "hantekprotocol/types.h"
#include <QList>

namespace Dso {


/// \brief Stores the samplerate limits for calculations.
struct ControlSamplerateLimits {
    double base;                           ///< The base for sample rate calculations
    double max;                            ///< The maximum sample rate
    std::vector< unsigned > recordLengths; ///< Available record lengths, UINT_MAX means rolling
};

/// \brief Stores the samplerate limits.
struct ControlSpecificationSamplerate {
    ControlSamplerateLimits single = { 50e6, 50e6, std::vector< unsigned >() };  ///< The limits for single channel mode
    ControlSamplerateLimits multi = { 100e6, 100e6, std::vector< unsigned >() }; ///< The limits for multi channel mode
};

struct ControlSpecificationGainLevel {
    /// The index of the selected gain on the hardware
    unsigned char gainValue; // 1, 2, 5, 10, ..
    /// Available voltage steps in V/div
    double Vdiv;
    // double gainError;
};


/// \brief Stores the specifications of the currently connected device.
struct ControlSpecification {
    explicit ControlSpecification( unsigned channels );
    const ChannelID channels;

    // Limits
    ControlSpecificationSamplerate samplerate; ///< The samplerate specifications



    /// Gain levels
    std::vector< ControlSpecificationGainLevel > gain;

    // Features
    std::vector< Coupling > couplings = { Dso::Coupling::DC, Dso::Coupling::AC };
    std::vector< TriggerMode > triggerModes = { TriggerMode::AUTO };
    bool hasACcoupling = false;

};
} // namespace Dso
