// SPDX-License-Identifier: GPL-2.0-or-later
//
// DSO-5200/5200A model definition, replacing the 6022 family's models for
// this GUI build. Unlike the 6022 models, applyRequirements() is a no-op: the
// 6022 code path wires per-model ControlCommand objects into
// HantekDsoControl's generic control[]/addCommand() dispatcher, but
// Dso5200DsoControl does not use that dispatcher at all (see its header for
// why) - it sends its own protocol structs directly.

#pragma once

#include "dsomodel.h"

class HantekDsoControl;

struct ModelDSO5200 : public DSOModel {
    static const int ID = 0x5200;
    ModelDSO5200();
    void applyRequirements( HantekDsoControl * ) const override {}
};

struct ModelDSO5200A : public DSOModel {
    static const int ID = 0x520a;
    ModelDSO5200A();
    void applyRequirements( HantekDsoControl * ) const override {}
};
