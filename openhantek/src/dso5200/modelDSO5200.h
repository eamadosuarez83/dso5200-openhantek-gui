// SPDX-License-Identifier: GPL-2.0-or-later
//
// Modelos DSO-5200/5200A: los únicos que soporta esta GUI. Solo describen el aparato (VID/PID,
// firmware, especificación); el protocolo lo maneja Dso5200DsoControl a través de dso5200basic.

#pragma once

#include "dsomodel.h"

struct ModelDSO5200 : public DSOModel {
    static const int ID = 0x5200;
    ModelDSO5200();
};

struct ModelDSO5200A : public DSOModel {
    static const int ID = 0x520a;
    ModelDSO5200A();
};
