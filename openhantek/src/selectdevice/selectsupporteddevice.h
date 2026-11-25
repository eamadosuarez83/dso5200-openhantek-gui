// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "ui_selectsupporteddevice.h"

#include <QDialog>
#include <QPushButton>

#include "usb/scopedevice.h"
#include <memory>

struct libusb_context;

/**
 * Ventana de arranque: busca el DSO-5200/5200A, le carga el firmware y se conecta sola cuando está
 * listo. Sin aparato ofrece el modo demo. Los métodos -Modal bloquean hasta que se cierra la ventana.
 */
class SelectSupportedDevice : public QDialog {
    Q_OBJECT

  public:
    explicit SelectSupportedDevice( QWidget *parent = nullptr );
    std::unique_ptr< ScopeDevice > showSelectDeviceModal( libusb_context *context, int verboseLevel = 0, bool autoConnect = true );
    void showLibUSBFailedDialogModel( int error );

  private:
    std::unique_ptr< Ui::SelectSupportedDevice > ui;
    UniqueUSBid selectedDevice = 0;
    bool demoModeClicked = false;
    int verboseLevel = 0;
    QPushButton *btnDemoMode;
};
