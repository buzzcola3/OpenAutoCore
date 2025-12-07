/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
*
*  openauto is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.

*  openauto is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with openauto. If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <QApplication>
#include <f1x/openauto/autoapp/UI/MainWindow.hpp>
#include <QFileInfo>
#include <QFile>
#include "ui_mainwindow.h"
#include <QTimer>
#include <QDateTime>
#include <QMessageBox>
#include <QTextStream>
#include <QFontDatabase>
#include <QFont>
#include <QScreen>
#include <QRect>
#include <QVideoWidget>
#include <QNetworkInterface>
#include <QStandardItemModel>
#include <QAbstractSocket>
#include <QIODevice>
#include <QStringList>
#include <QByteArray>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <unistd.h>
#include <f1x/openauto/Common/Log.hpp>

namespace {
QStringList collectNetworkSummaries() {
    QStringList summaries;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto &iface : interfaces) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning) ||
            (flags & QNetworkInterface::IsLoopBack)) {
            continue;
        }

        QString address;
        const auto entries = iface.addressEntries();
        for (const auto &entry : entries) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                address = entry.ip().toString();
                break;
            }
        }

        if (address.isEmpty() && !entries.isEmpty()) {
            address = entries.front().ip().toString();
        }

        summaries << QStringLiteral("%1: %2")
                         .arg(iface.humanReadableName(), address.isEmpty() ? QStringLiteral("(no address)") : address);
    }

    return summaries;
}

bool writeValueToFile(const QString &path, const QByteArray &value) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    if (!value.isEmpty()) {
        file.write(value);
    }

    file.close();
    return true;
}
}

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace ui
{

MainWindow::MainWindow(configuration::IConfiguration::Pointer configuration, QWidget *parent)
    : QMainWindow(parent)
    , ui_(new Ui::MainWindow)
    , localDevice(new QBluetoothLocalDevice)
{
    // set default bg color to black
    this->setStyleSheet("QMainWindow {background-color: rgb(0,0,0);}");

    // Set default font and size
    int id = QFontDatabase::addApplicationFont(":/Roboto-Regular.ttf");
    QString family = QFontDatabase::applicationFontFamilies(id).at(0);
    QFont _font(family, 11);
    qApp->setFont(_font);

    this->configuration_ = configuration;

    // trigger files
    this->nightModeEnabled = check_file_exist(this->nightModeFile);
    this->devModeEnabled = check_file_exist(this->devModeFile);
    this->wifiButtonForce = check_file_exist(this->wifiButtonFile);
    this->brightnessButtonForce = check_file_exist(this->brightnessButtonFile);
    this->systemDebugmode = check_file_exist(this->debugModeFile);
    this->c1ButtonForce = check_file_exist(this->custom_button_file_c1);
    this->c2ButtonForce = check_file_exist(this->custom_button_file_c2);
    this->c3ButtonForce = check_file_exist(this->custom_button_file_c3);
    this->c4ButtonForce = check_file_exist(this->custom_button_file_c4);
    this->c5ButtonForce = check_file_exist(this->custom_button_file_c5);
    this->c6ButtonForce = check_file_exist(this->custom_button_file_c6);

    // wallpaper stuff
    this->wallpaperDayFileExists = check_file_exist("wallpaper.png");
    this->wallpaperNightFileExists = check_file_exist("wallpaper-night.png");
    this->wallpaperClassicDayFileExists = check_file_exist("wallpaper-classic.png");
    this->wallpaperClassicNightFileExists = check_file_exist("wallpaper-classic-night.png");
    this->wallpaperEQFileExists = check_file_exist("wallpaper-eq.png");

    ui_->setupUi(this);

    connect(ui_->pushButtonSettings, &QPushButton::clicked, this, &MainWindow::openSettings);
    connect(ui_->pushButtonExit, &QPushButton::clicked, this, &MainWindow::toggleExit);
    connect(ui_->pushButtonShutdown, &QPushButton::clicked, this, &MainWindow::exit);
    connect(ui_->pushButtonReboot, &QPushButton::clicked, this, &MainWindow::reboot);
    connect(ui_->pushButtonCancel, &QPushButton::clicked, this, &MainWindow::toggleExit);

    ui_->horizontalWidgetPower->hide();

    this->oldGUIStyle = false;
    ui_->menuWidget->show();
}

MainWindow::~MainWindow()
{
    delete ui_;
}

}
}
}
}


void f1x::openauto::autoapp::ui::MainWindow::toggleExit()
{
    if (!this->exitMenuVisible) {
        ui_->horizontalWidgetPower->show();
        this->exitMenuVisible = true;
    } else {
        ui_->horizontalWidgetPower->hide();
        this->exitMenuVisible = false;
    }
}

void f1x::openauto::autoapp::ui::MainWindow::toggleGUI()
{
    if (!this->oldGUIStyle) {
        ui_->menuWidget->hide();
        this->oldGUIStyle = true;
    } else {
        ui_->menuWidget->show();
        this->oldGUIStyle = false;
    }
    f1x::openauto::autoapp::ui::MainWindow::updateBG();
    f1x::openauto::autoapp::ui::MainWindow::tmpChanged();
}

void f1x::openauto::autoapp::ui::MainWindow::updateBG()
{
    if (this->date_text == "12/24") {
        this->setStyleSheet("QMainWindow { background: url(:/wallpaper-christmas.png); background-repeat: no-repeat; background-position: center; }");
        this->holidaybg = true;
    }
    else if (this->date_text == "12/31") {
        this->setStyleSheet("QMainWindow { background: url(:/wallpaper-firework.png); background-repeat: no-repeat; background-position: center; }");
        this->holidaybg = true;
    }
}

void f1x::openauto::autoapp::ui::MainWindow::on_StateChanged(QMediaPlayer::State state)
{
    if (state == QMediaPlayer::StoppedState || state == QMediaPlayer::PausedState) {
        std::remove("/tmp/media_playing");
    } else {
        std::ofstream("/tmp/media_playing");
    }
}


bool f1x::openauto::autoapp::ui::MainWindow::check_file_exist(const char *fileName)
{
    std::ifstream ifile(fileName, std::ios::in);
    // file not ok - checking if symlink
    if (!ifile.good()) {
        QFileInfo linkFile = QString(fileName);
        if (linkFile.isSymLink()) {
            QFileInfo linkTarget(linkFile.symLinkTarget());
            return linkTarget.exists();
        } else {
            return ifile.good();
        }
    } else {
        return ifile.good();
    }
}

void f1x::openauto::autoapp::ui::MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return) {
        QApplication::postEvent (QApplication::focusWidget(), new QKeyEvent ( QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier));
        QApplication::postEvent (QApplication::focusWidget(), new QKeyEvent ( QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier));
    }
    if (event->key() == Qt::Key_1) {
        QApplication::postEvent (QApplication::focusWidget(), new QKeyEvent ( QEvent::KeyPress, Qt::Key_Tab, Qt::ShiftModifier));
    }
    if (event->key() == Qt::Key_2) {
        QApplication::postEvent (QApplication::focusWidget(), new QKeyEvent ( QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier));
    }
    if(event->key() == Qt::Key_Escape)
    {
        if (this->oldGUIStyle == true) {
            toggleGUI();            
        } else {
            toggleExit();
        }
    }    
}

void f1x::openauto::autoapp::ui::MainWindow::tmpChanged()
{
    try {
        if (std::ifstream("/tmp/entityexit")) {
            MainWindow::TriggerAppStop();
            std::remove("/tmp/entityexit");
        }
    } catch (...) {
        OPENAUTO_LOG(error) << "[OpenAuto] Error in entityexit";
    }

    // check if system is in display off mode (tap2wake)
    if (std::ifstream("/tmp/blankscreen")) {
        if (ui_->centralWidget->isVisible() == true) {
            CloseAllDialogs();
            ui_->centralWidget->hide();
        }
    } else {
        if (ui_->centralWidget->isVisible() == false) {
            ui_->centralWidget->show();
        }
    }

    // check if custom command needs black background
    if (std::ifstream("/tmp/blackscreen")) {
        if (ui_->centralWidget->isVisible() == true) {
            ui_->centralWidget->hide();
            this->setStyleSheet("QMainWindow {background-color: rgb(0,0,0);}");
            this->background_set = false;
        }
    } else {
        if (this->background_set == false) {
            f1x::openauto::autoapp::ui::MainWindow::updateBG();
            this->background_set = true;
        }
    }

    // check if shutdown is external triggered and init clean app exit
    if (std::ifstream("/tmp/external_exit")) {
        f1x::openauto::autoapp::ui::MainWindow::MainWindow::exit();
    }

    this->hotspotActive = check_file_exist("/tmp/hotspot_active");

}

void f1x::openauto::autoapp::ui::MainWindow::updateNetworkInfo()
{
    const auto summaries = collectNetworkSummaries();
    const QString tooltip = summaries.isEmpty()
        ? tr("No active network interfaces detected.")
        : summaries.join(QLatin1Char('\n'));

    ui_->pushButtonSettings->setToolTip(tooltip);
    ui_->pushButtonExit->setToolTip(tooltip);
}

void f1x::openauto::autoapp::ui::MainWindow::setMute()
{
    if (toggleMute) {
        return;
    }

    toggleMute = true;
    if (!writeValueToFile(QStringLiteral("/tmp/openauto_muted"), QByteArray("1\n"))) {
        OPENAUTO_LOG(warning) << "[MainWindow] Unable to persist mute flag.";
    }
}

void f1x::openauto::autoapp::ui::MainWindow::setUnMute()
{
    if (!toggleMute) {
        return;
    }

    toggleMute = false;
    QFile::remove(QStringLiteral("/tmp/openauto_muted"));
}

void f1x::openauto::autoapp::ui::MainWindow::setPairable()
{
    if (localDevice == nullptr) {
        return;
    }

    localDevice->setHostMode(QBluetoothLocalDevice::HostDiscoverable);
    QTimer::singleShot(60000, this, [this]() {
        if (localDevice != nullptr) {
            localDevice->setHostMode(QBluetoothLocalDevice::HostConnectable);
        }
    });
}

void f1x::openauto::autoapp::ui::MainWindow::createDebuglog()
{
    const QString filePath = QStringLiteral("/tmp/openauto-debug-%1.log")
                                 .arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss"));
    QFile logFile(filePath);
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Debug log"), tr("Unable to create %1").arg(filePath));
        return;
    }

    QTextStream stream(&logFile);
    stream << "OpenAuto Debug Snapshot" << '\n';
    stream << "Timestamp: " << QDateTime::currentDateTime().toString(Qt::ISODate) << '\n';
    stream << "Night mode: " << (nightModeEnabled ? "yes" : "no") << '\n';
    stream << "Hotspot active: " << (hotspotActive ? "yes" : "no") << '\n';

    const auto summaries = collectNetworkSummaries();
    stream << "Network interfaces:" << '\n';
    for (const auto &line : summaries) {
        stream << "  - " << line << '\n';
    }

    logFile.close();
    QMessageBox::information(this, tr("Debug log"), tr("Saved snapshot to %1").arg(filePath));
}

void f1x::openauto::autoapp::ui::MainWindow::on_horizontalSliderBrightness_valueChanged(int value)
{
    const int clamped = std::max(0, std::min(255, value));
    const QByteArray payload = QByteArray::number(clamped) + '\n';

    if (!writeValueToFile(brightnessFilename, payload) && !writeValueToFile(brightnessFilenameAlt, payload)) {
        OPENAUTO_LOG(warning) << "[MainWindow] Unable to update brightness value.";
        return;
    }

    std::snprintf(brightness_str, sizeof(brightness_str), "%d", clamped);
}
