// SPDX-License-Identifier: GPL-2.0-or-later

#include "selectsupporteddevice.h"

#include <QDebug>
#include <QDesktopServices>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>
#include <algorithm>

#include "devicelistentry.h"
#include "documents.h"
#include "dsomodel.h"
#include "usb/finddevices.h"
#include "usb/uploadFirmware.h"


namespace {
/// Revisa los aparatos encontrados: carga el firmware a los que no lo tienen y prueba la conexión
/// con los demás.
std::vector< DeviceListEntry > scanDevices( FindDevices *findDevices, int verboseLevel ) {
    std::vector< DeviceListEntry > entries;
    for ( auto &i : *findDevices->getDevices() ) {
        DeviceListEntry entry;
        entry.name = i.second->getModel()->name;
        entry.id = i.first;
        if ( i.second->needsFirmware() ) {
            if ( verboseLevel > 2 )
                qDebug() << "  scanDevices()" << entry.name << "upload firmware";
            UploadFirmware uf;
            if ( !uf.startUpload( i.second.get() ) )
                entry.errorMessage = uf.getErrorMessage();
            else
                entry.needFirmware = true;
        } else if ( i.second->connectDevice( entry.errorMessage ) ) {
            if ( verboseLevel > 2 )
                qDebug() << "  scanDevices()" << entry.name << "can connect";
            entry.canConnect = true;
            i.second->disconnectFromDevice();
        }
        entries.push_back( entry );
    }
    return entries;
}
} // namespace


SelectSupportedDevice::SelectSupportedDevice( QWidget *parent ) : QDialog( parent ), ui( new Ui::SelectSupportedDevice ) {
    ui->setupUi( this );
    ui->buttonBox->button( QDialogButtonBox::Ok )->setEnabled( false );
    btnDemoMode = new QPushButton( tr( "Demo Mode" ) );
    ui->buttonBox->addButton( btnDemoMode, QDialogButtonBox::AcceptRole );
    qRegisterMetaType< UniqueUSBid >( "UniqueUSBid" );

    connect( ui->buttonBox, &QDialogButtonBox::accepted, this, []() { QCoreApplication::instance()->quit(); } );

    connect( ui->buttonBox, &QDialogButtonBox::helpRequested, []() {
        QUrl url;
        if ( QFile( DocPath + UserManualName ).exists() )
            url = QUrl::fromLocalFile( QFileInfo( DocPath + UserManualName ).absoluteFilePath() );
        else
            url = QUrl( DocUrl + UserManualName );
        qDebug() << "open" << url;
        QDesktopServices::openUrl( url );
    } );

    connect( btnDemoMode, &QPushButton::clicked, this, [ this ]() { demoModeClicked = true; } );
}


/// Busca el DSO-5200/5200A, le carga el firmware si hace falta y, en cuanto está listo, se conecta
/// solo (salvo --noAutoConnect). Si hay varios aparatos se usa el primero que esté listo.
std::unique_ptr< ScopeDevice > SelectSupportedDevice::showSelectDeviceModal( libusb_context *context, int verboseLevel,
                                                                             bool autoConnect ) {
    std::unique_ptr< FindDevices > findDevices = std::unique_ptr< FindDevices >( new FindDevices( context, verboseLevel ) );

    QString userManualPath;
    if ( QFile( DocPath + UserManualName ).exists() )
        userManualPath = DocPath + UserManualName;
    else
        userManualPath = DocUrl + UserManualName;

    const QString messageDeviceReady = tr( "<p><br/><b>%1 is ready for use.</b></p><p>Please observe the "
                                           "<a href='%2'>"
                                           "user manual</a> for safe operation.</p>" );
    const QString messageUpload = tr( "<p>Uploading the firmware to the %1 ...</p>"
                                      "<p><b>If the upload takes more than 30 s, please close this window "
                                      "<br/>and restart the program!</b></p>" );

    QString messageNoDevices = tr( "<p>Searching for a DSO-5200/5200A ...</p>"
                                   "<p>Check that the oscilloscope is switched on and its USB cable is connected.</p>" );
#if defined( Q_OS_LINUX )
    QFile libRules( "/usr/lib/udev/rules.d/60-dso5200.rules" );
    QFile etcRules( "/etc/udev/rules.d/60-dso5200.rules" );
    if ( !libRules.exists() && !etcRules.exists() ) {
        messageNoDevices += tr( "<p>Please make sure you have copied the udev rules file to<br/>"
                                "<b>%1</b> or<br/><b>%2</b><br/>"
                                "for correct USB access permissions.</p>" )
                                .arg( etcRules.fileName(), libRules.fileName() );
    }
#endif
    messageNoDevices += tr( "<hr/><p>Even without a device you can explore the program's function. "
                            "Just press the <b>Demo Mode</b> button below.</p>" );

    std::vector< DeviceListEntry > entries;
    QTimer timer;
    timer.setInterval( 1000 );
    connect( &timer, &QTimer::timeout, this, [ & ]() {
        if ( findDevices->updateDeviceList() ) // cambió algo en el bus (p. ej. reenumeración tras el firmware)
            entries = scanDevices( findDevices.get(), verboseLevel );

        QPushButton *ok = ui->buttonBox->button( QDialogButtonBox::Ok );
        auto ready = std::find_if( entries.begin(), entries.end(), []( const DeviceListEntry &e ) { return e.canConnect; } );
        if ( ready != entries.end() ) {
            selectedDevice = ready->id;
            ok->setEnabled( true );
            ui->labelReadyState->setText( messageDeviceReady.arg( ready->name, userManualPath ) );
            if ( autoConnect )
                ok->click(); // listo: arrancar sin que el usuario tenga que hacer nada
            return;
        }
        ok->setEnabled( false );
        auto uploading =
            std::find_if( entries.begin(), entries.end(), []( const DeviceListEntry &e ) { return e.needFirmware; } );
        auto failed =
            std::find_if( entries.begin(), entries.end(), []( const DeviceListEntry &e ) { return !e.errorMessage.isEmpty(); } );
        if ( uploading != entries.end() )
            ui->labelReadyState->setText( messageUpload.arg( uploading->name ) );
        else if ( failed != entries.end() )
            ui->labelReadyState->setText( tr( "<p><br/><b>Connection failed!</b></p>" ) + failed->errorMessage );
        else
            ui->labelReadyState->setText( messageNoDevices );
    } );
    timer.start();
    QCoreApplication::sendEvent( &timer, new QTimerEvent( timer.timerId() ) ); // immediate timer event

    show();
    QCoreApplication::instance()->exec();
    timer.stop();
    close();
    if ( demoModeClicked )
        return std::unique_ptr< ScopeDevice >( new ScopeDevice() );
    if ( !selectedDevice )
        return nullptr;
    return findDevices->takeDevice( selectedDevice );
}


void SelectSupportedDevice::showLibUSBFailedDialogModel( int error ) {
    ui->labelReadyState->setText( tr( "Can't initialize USB: %1" ).arg( libUsbErrorString( error ) ) );
    ui->buttonBox->button( QDialogButtonBox::Ok )->setEnabled( false );
    show();
    QCoreApplication::instance()->exec();
    close();
}
