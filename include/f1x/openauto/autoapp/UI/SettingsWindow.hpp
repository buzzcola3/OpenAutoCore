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

#pragma once

#include <QWidget>
#include <f1x/openauto/autoapp/Configuration/IConfiguration.hpp>
#include <QFileDialog>
#include <QComboBox>
#include <QKeyEvent>
#ifdef MAC_OS
#else
  #include <sys/sysinfo.h>
#endif

class QCheckBox;
class QTimer;

namespace Ui
{
class SettingsWindow;
}

namespace f1x::openauto::autoapp::ui
{

class SettingsWindow : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsWindow(configuration::IConfiguration::Pointer configuration, QWidget *parent = nullptr);
    ~SettingsWindow() override;
    void loadSystemValues();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onSave();
    void onResetToDefaults();
    void onUpdateScreenDPI(int value);
    void onUpdateSystemVolume(int value);
    void onUpdateSystemCapture(int value);
    void on_pushButtonAudioTest_clicked();
    void updateNetworkInfo();
    void on_pushButtonNetwork0_clicked();
    void on_pushButtonNetwork1_clicked();
    void updateSystemInfo();
    void updateInfo();

public slots:
    void show_tab2();
    void show_tab3();
    void show_tab4();
    void show_tab5();

private:
    void showEvent(QShowEvent* event) override;
    void load();
    void loadButtonCheckBoxes();
    void saveButtonCheckBoxes();
    void saveButtonCheckBox(const QCheckBox* checkBox, configuration::IConfiguration::ButtonCodes& buttonCodes, aap_protobuf::service::media::sink::message::KeyCode buttonCode);
    void setButtonCheckBoxes(bool value);
    void populateBluetoothComboBoxLinux(QComboBox *comboBoxBluetooth);
    Ui::SettingsWindow* ui_;
    configuration::IConfiguration::Pointer configuration_;

  void getMacMemoryInfo(QString &freeMemory);
};

}



