// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "usb/scopedevice.h"
#include <QString>

/**
 * Estado de un aparato encontrado en la ventana de arranque (SelectSupportedDevice).
 */
struct DeviceListEntry {
    UniqueUSBid id;
    QString name;
    bool canConnect = false;
    bool needFirmware = false;
    QString errorMessage;
};
