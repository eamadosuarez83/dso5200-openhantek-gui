// SPDX-License-Identifier: GPL-2.0-or-later
//
// ControlSpecification values below are ported from dso5200basic's
// Dso5200Control (src/device/dso5200.h/.cpp in the dso5200basic tree), which
// was reconstructed and validated against real DSO-5200A hardware:
//  - the V/div table (10V..10mV, standard 1-2-5 sequence) via USB capture of
//    the official Windows software's relay clicks, see kGain there;
//  - the samplerate limits (single/multi base+max) from the protocol docs
//    (bulkcode.h) confirmed against hardware;
//  - acoplamiento DC/AC: relé real por canal (dso5200basic a072f1b, medido con la CAL). GND no
//    existe en el hardware (no hay relé de masa; la vieja "firma GND" era el grupo de 2-10 V): el
//    núcleo deja el relé en DC y entrega 0 V en ese canal.
//
// specification.gain[].gainValue here is display-only (no code in
// Dso5200DsoControl reads it back) - it records the within-decade multiplier
// (x1/x2/x5) for any dock UI that wants to show it.

#include "modelDSO5200.h"

static ModelDSO5200 modelInstance_5200;
static ModelDSO5200A modelInstance_5200a;

static void initSpecifications( Dso::ControlSpecification &specification ) {
    // Real DSO-5200/5200A V/div steps (see dso5200basic docs/GUIA_ESTUDIO.md, "Octavo caso real").
    // 10 mV has no calibrated step of its own on this unit and reuses the 20 mV calibration -
    // matches Dso5200Control::kGain exactly (9 entries, not 10).
    specification.gain = { { 1, 20e-3 }, { 2, 50e-3 }, { 1, 100e-3 }, { 2, 200e-3 }, { 5, 500e-3 },
                           { 1, 1.0 },   { 2, 2.0 },   { 5, 5.0 },    { 1, 10.0 } };

    // DSO-5200: 100 MS/s base / 125 MS/s max (1 channel = "fast rate" doubles both).
    specification.samplerate.single.base = 100e6;
    specification.samplerate.single.max = 125e6;
    specification.samplerate.single.recordLengths = { 10240 };
    specification.samplerate.multi.base = 200e6;
    specification.samplerate.multi.max = 250e6;
    specification.samplerate.multi.recordLengths = { 20480 };

    // Sin tabla de frecuencias fijas: el núcleo elige el divisor (y la tabla de las bases lentas).

    specification.couplings = { Dso::Coupling::DC, Dso::Coupling::AC, Dso::Coupling::GND };
    // Sin esto el panel de tensión solo ofrece DC (la opción existe para el 6022, que no tiene AC).
    specification.hasACcoupling = true;
    // El núcleo solo ofrece AUTO (dispara o, si no llega flanco, fuerza la captura).
    specification.triggerModes = { Dso::TriggerMode::AUTO };
}

// VID/PID confirmed against real hardware (dso5200basic/src/usb/finddevice.cpp): the product ID
// (0x5200 vs 0x520a) already identifies the variant before firmware is loaded; only the vendor ID
// changes (0x04b4 -> 0x04b5) once firmware is running.
ModelDSO5200::ModelDSO5200()
    : DSOModel( ID, 0x04b5, 0x5200, 0x04b4, 0x5200, 0, "dso5200x86", "DSO-5200", Dso::ControlSpecification( 2 ),
               /*twoStageFirmware=*/true ) {
    initSpecifications( specification );
}

ModelDSO5200A::ModelDSO5200A()
    : DSOModel( ID, 0x04b5, 0x520a, 0x04b4, 0x520a, 0, "dso5200ax86", "DSO-5200A", Dso::ControlSpecification( 2 ),
               /*twoStageFirmware=*/true ) {
    initSpecifications( specification );
}
