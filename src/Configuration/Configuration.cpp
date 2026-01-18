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

#include <Configuration/Configuration.hpp>
#include <Common/Log.hpp>
#include <algorithm>




namespace f1x::openauto::autoapp::configuration
{

const std::string Configuration::cConfigFileName = "openauto.ini";

const std::string Configuration::cGeneralShowClockKey = "General.ShowClock";

const std::string Configuration::cGeneralOldGUIKey = "General.OldGUI";
const std::string Configuration::cGeneralHideMenuToggleKey = "General.HideMenuToggle";
const std::string Configuration::cGeneralShowCursorKey = "General.ShowCursor";
const std::string Configuration::cGeneralHideBrightnessControlKey = "General.HideBrightnessControl";
const std::string Configuration::cGeneralShowNetworkinfoKey = "General.ShowNetworkinfo";

const std::string Configuration::cVideoFPSKey = "Video.FPS";
const std::string Configuration::cVideoResolutionKey = "Video.Resolution";
const std::string Configuration::cVideoScreenDPIKey = "Video.ScreenDPI";
const std::string Configuration::cVideoMarginWidth = "Video.MarginWidth";
const std::string Configuration::cVideoMarginHeight = "Video.MarginHeight";

const std::string Configuration::cAudioChannelMediaEnabled = "AudioChannel.MediaEnabled";
const std::string Configuration::cAudioChannelGuidanceEnabled = "AudioChannel.GuidanceEnabled";
const std::string Configuration::cAudioChannelSystemEnabled = "AudioChannel.SystemEnabled";
const std::string Configuration::cAudioChannelTelephonyEnabled = "AudioChannel.TelephonyEnabled";

const std::string Configuration::cAudioOutputBackendType = "Audio.OutputBackendType";

const std::string Configuration::cBluetoothAdapterTypeKey = "Bluetooth.AdapterType";
const std::string Configuration::cBluetoothAdapterAddressKey = "Bluetooth.AdapterAddress";
const std::string Configuration::cBluetoothWirelessProjectionEnabledKey = "Bluetooth.WirelessProjectionEnabled";

const std::string Configuration::cInputEnableTouchscreenKey = "Input.EnableTouchscreen";
const std::string Configuration::cInputEnablePlayerControlKey = "Input.EnablePlayerControl";
const std::string Configuration::cInputPlayButtonKey = "Input.PlayButton";
const std::string Configuration::cInputPauseButtonKey = "Input.PauseButton";
const std::string Configuration::cInputTogglePlayButtonKey = "Input.TogglePlayButton";
const std::string Configuration::cInputNextTrackButtonKey = "Input.NextTrackButton";
const std::string Configuration::cInputPreviousTrackButtonKey = "Input.PreviousTrackButton";
const std::string Configuration::cInputHomeButtonKey = "Input.HomeButton";
const std::string Configuration::cInputPhoneButtonKey = "Input.PhoneButton";
const std::string Configuration::cInputCallEndButtonKey = "Input.CallEndButton";
const std::string Configuration::cInputVoiceCommandButtonKey = "Input.VoiceCommandButton";
const std::string Configuration::cInputLeftButtonKey = "Input.LeftButton";
const std::string Configuration::cInputRightButtonKey = "Input.RightButton";
const std::string Configuration::cInputUpButtonKey = "Input.UpButton";
const std::string Configuration::cInputDownButtonKey = "Input.DownButton";
const std::string Configuration::cInputScrollWheelButtonKey = "Input.ScrollWheelButton";
const std::string Configuration::cInputBackButtonKey = "Input.BackButton";
const std::string Configuration::cInputEnterButtonKey = "Input.EnterButton";
const std::string Configuration::cInputNavButtonKey = "Input.NavButton";

Configuration::Configuration()
{
    this->load();
}

void Configuration::load()
{
    boost::property_tree::ptree iniConfig;

    try
    {
        boost::property_tree::ini_parser::read_ini(cConfigFileName, iniConfig);

        oldGUI_ = iniConfig.get<bool>(cGeneralOldGUIKey, false);
        hideMenuToggle_ = iniConfig.get<bool>(cGeneralHideMenuToggleKey, false);
        showCursor_ = iniConfig.get<bool>(cGeneralShowCursorKey, false);
        showNetworkinfo_ = iniConfig.get<bool>(cGeneralShowNetworkinfoKey, false);


        videoFPS_ = static_cast<aap_protobuf::service::media::sink::message::VideoFrameRateType>(iniConfig.get<uint32_t>(cVideoFPSKey,
                                                                                             aap_protobuf::service::media::sink::message::VideoFrameRateType::VIDEO_FPS_30));

        videoResolution_ = static_cast<aap_protobuf::service::media::sink::message::VideoCodecResolutionType>(iniConfig.get<uint32_t>(cVideoResolutionKey,
                                                                                                           aap_protobuf::service::media::sink::message::VideoCodecResolutionType::VIDEO_800x480));
        screenDPI_ = iniConfig.get<size_t>(cVideoScreenDPIKey, 140);

        videoMargins_ = VideoMargins{
            iniConfig.get<int32_t>(cVideoMarginWidth, 0),
            iniConfig.get<int32_t>(cVideoMarginHeight, 0)
        };

        enableTouchscreen_ = iniConfig.get<bool>(cInputEnableTouchscreenKey, true);
        enablePlayerControl_ = iniConfig.get<bool>(cInputEnablePlayerControlKey, false);
        this->readButtonCodes(iniConfig);

        bluetoothAdapterType_ = static_cast<BluetoothAdapterType>(iniConfig.get<uint32_t>(cBluetoothAdapterTypeKey,
                                                                                          static_cast<uint32_t>(BluetoothAdapterType::NONE)));

        wirelessProjectionEnabled_ = iniConfig.get<bool>(cBluetoothWirelessProjectionEnabledKey, true);

        bluetoothAdapterAddress_ = iniConfig.get<std::string>(cBluetoothAdapterAddressKey, "");

        _audioChannelEnabledMedia = iniConfig.get<bool>(cAudioChannelMediaEnabled, true);
        _audioChannelEnabledGuidance = iniConfig.get<bool>(cAudioChannelGuidanceEnabled, true);
        _audioChannelEnabledSystem = iniConfig.get<bool>(cAudioChannelSystemEnabled, true);
        _audioChannelEnabledTelephony = iniConfig.get<bool>(cAudioChannelTelephonyEnabled, true);

    }
    catch(const boost::property_tree::ini_parser_error& e)
    {
        OPENAUTO_LOG(warning) << "[Configuration] failed to read configuration file: " << cConfigFileName
                            << ", error: " << e.what()
                            << ". Using default configuration.";
        this->reset();
    }
}

void Configuration::reset()
{
    handednessOfTrafficType_ = HandednessOfTrafficType::LEFT_HAND_DRIVE;
    showClock_ = true;
    oldGUI_ = false;
    alphaTrans_ = 50;
    hideMenuToggle_ = false;
    hideAlpha_ = false;
    showLux_ = false;
    showCursor_ = true;
    hideBrightnessControl_ = false;
    hideWarning_ = false;
    showNetworkinfo_ = false;
    mp3MasterPath_ = "/media/MYMEDIA";
    mp3SubFolder_ = "/";
    mp3Track_ = 0;
    mp3AutoPlay_ = false;
    showAutoPlay_ = false;
    instantPlay_ = false;
    videoFPS_ = aap_protobuf::service::media::sink::message::VideoFrameRateType::VIDEO_FPS_30;
    videoResolution_ = aap_protobuf::service::media::sink::message::VideoCodecResolutionType::VIDEO_800x480;
    screenDPI_ = 140;
    videoMargins_ = VideoMargins{};
    enableTouchscreen_ = true;
    enablePlayerControl_ = false;
    buttonCodes_.clear();
    bluetoothAdapterType_ = BluetoothAdapterType::NONE;
    bluetoothAdapterAddress_ = "";

   _audioChannelEnabledMedia = true;
   _audioChannelEnabledGuidance = true;
   _audioChannelEnabledSystem = true;
   _audioChannelEnabledTelephony = true;

    audioOutputBackendType_ = AudioOutputBackendType::QT;
    wirelessProjectionEnabled_ = true;
}

void Configuration::save()
{
    boost::property_tree::ptree iniConfig;

    iniConfig.put<bool>(cGeneralOldGUIKey, oldGUI_);
    iniConfig.put<bool>(cGeneralHideMenuToggleKey, hideMenuToggle_);
    iniConfig.put<bool>(cGeneralShowCursorKey, showCursor_);
    iniConfig.put<bool>(cGeneralHideBrightnessControlKey, hideBrightnessControl_);
    iniConfig.put<bool>(cGeneralShowNetworkinfoKey, showNetworkinfo_);

    iniConfig.put<uint32_t>(cVideoFPSKey, static_cast<uint32_t>(videoFPS_));
    iniConfig.put<uint32_t>(cVideoResolutionKey, static_cast<uint32_t>(videoResolution_));
    iniConfig.put<size_t>(cVideoScreenDPIKey, screenDPI_);
    iniConfig.put<uint32_t>(cVideoMarginWidth, static_cast<uint32_t>(videoMargins_.width));
    iniConfig.put<uint32_t>(cVideoMarginHeight, static_cast<uint32_t>(videoMargins_.height));

    iniConfig.put<bool>(cInputEnableTouchscreenKey, enableTouchscreen_);
    iniConfig.put<bool>(cInputEnablePlayerControlKey, enablePlayerControl_);
    this->writeButtonCodes(iniConfig);

    iniConfig.put<uint32_t>(cBluetoothAdapterTypeKey, static_cast<uint32_t>(bluetoothAdapterType_));
    iniConfig.put<std::string>(cBluetoothAdapterAddressKey, bluetoothAdapterAddress_);
    iniConfig.put<bool>(cBluetoothWirelessProjectionEnabledKey, wirelessProjectionEnabled_);

    iniConfig.put<bool>(cAudioChannelMediaEnabled, _audioChannelEnabledMedia);
    iniConfig.put<bool>(cAudioChannelGuidanceEnabled, _audioChannelEnabledGuidance);
    iniConfig.put<bool>(cAudioChannelSystemEnabled, _audioChannelEnabledSystem);
    iniConfig.put<bool>(cAudioChannelTelephonyEnabled, _audioChannelEnabledTelephony);

  iniConfig.put<uint32_t>(cAudioOutputBackendType, static_cast<uint32_t>(audioOutputBackendType_));
    boost::property_tree::ini_parser::write_ini(cConfigFileName, iniConfig);
}

bool Configuration::hasTouchScreen() const
{
    return enableTouchscreen_;
}

void Configuration::hideMenuToggle(bool value)
{
    hideMenuToggle_ = value;
}

bool Configuration::hideMenuToggle() const
{
    return hideMenuToggle_;
}

void Configuration::showCursor(bool value)
{
    showCursor_ = value;
}

bool Configuration::showCursor() const
{
    return showCursor_;
}

void Configuration::showNetworkinfo(bool value)
{
    showNetworkinfo_ = value;
}

bool Configuration::showNetworkinfo() const
{
    return showNetworkinfo_;
}

aap_protobuf::service::media::sink::message::VideoFrameRateType Configuration::getVideoFPS() const
{
    return videoFPS_;
}

void Configuration::setVideoFPS(aap_protobuf::service::media::sink::message::VideoFrameRateType value)
{
    videoFPS_ = value;
}

aap_protobuf::service::media::sink::message::VideoCodecResolutionType Configuration::getVideoResolution() const
{
    return videoResolution_;
}

void Configuration::setVideoResolution(aap_protobuf::service::media::sink::message::VideoCodecResolutionType value)
{
    videoResolution_ = value;
}

size_t Configuration::getScreenDPI() const
{
    return screenDPI_;
}

void Configuration::setScreenDPI(size_t value)
{
    screenDPI_ = value;
}

void Configuration::setVideoMargins(const VideoMargins& value)
{
    videoMargins_ = value;
}

Configuration::VideoMargins Configuration::getVideoMargins() const
{
    return videoMargins_;
}

bool Configuration::getTouchscreenEnabled() const
{
    return enableTouchscreen_;
}

void Configuration::setTouchscreenEnabled(bool value)
{
    enableTouchscreen_ = value;
}

bool Configuration::playerButtonControl() const
{
    return enablePlayerControl_;
}

void Configuration::playerButtonControl(bool value)
{
    enablePlayerControl_ = value;
}

Configuration::ButtonCodes Configuration::getButtonCodes() const
{
    return buttonCodes_;
}

void Configuration::setButtonCodes(const ButtonCodes& value)
{
    buttonCodes_ = value;
}

BluetoothAdapterType Configuration::getBluetoothAdapterType() const
{
    return bluetoothAdapterType_;
}

void Configuration::setBluetoothAdapterType(BluetoothAdapterType value)
{
    bluetoothAdapterType_ = value;
}

std::string Configuration::getBluetoothAdapterAddress() const
{
    return bluetoothAdapterAddress_;
}

void Configuration::setBluetoothAdapterAddress(const std::string& value)
{
    bluetoothAdapterAddress_ = value;
}

bool Configuration::getWirelessProjectionEnabled() const {
    return wirelessProjectionEnabled_;
}

void Configuration::setWirelessProjectionEnabled(bool value) {
    wirelessProjectionEnabled_ = value;
}

bool Configuration::musicAudioChannelEnabled() const
{
  return _audioChannelEnabledMedia;
}

void Configuration::setMusicAudioChannelEnabled(bool value)
{
  _audioChannelEnabledMedia = value;
}

bool Configuration::guidanceAudioChannelEnabled() const
{
    return _audioChannelEnabledGuidance;
}

void Configuration::setGuidanceAudioChannelEnabled(bool value)
{
  _audioChannelEnabledGuidance = value;
}

  bool Configuration::systemAudioChannelEnabled() const
  {
    return _audioChannelEnabledSystem;
  }

  void Configuration::setSystemAudioChannelEnabled(bool value)
  {
    _audioChannelEnabledSystem = value;
  }

  bool Configuration::telephonyAudioChannelEnabled() const
  {
    return _audioChannelEnabledTelephony;
  }

  void Configuration::setTelephonyAudioChannelEnabled(bool value)
  {
    _audioChannelEnabledTelephony = value;
  }

AudioOutputBackendType Configuration::getAudioOutputBackendType() const
{
    return audioOutputBackendType_;
}

void Configuration::setAudioOutputBackendType(AudioOutputBackendType value)
{
    audioOutputBackendType_ = value;
}

std::string Configuration::getCSValue(const std::string& searchString) const
{
    using namespace std;
    ifstream inFile;
    ifstream inFile2;
    string line;
    const std::string searchKey = searchString + "=";
    inFile.open("/boot/crankshaft/crankshaft_env.sh");
    inFile2.open("/opt/crankshaft/crankshaft_default_env.sh");

    size_t pos;

    if(inFile) {
        while(inFile.good())
        {
            getline(inFile,line); // get line from file
            if (line[0] != '#') {
                pos=line.find(searchKey); // search
                if(pos!=std::string::npos) // string::npos is returned if string is not found
                {
                    int equalPosition = line.find("=");
                    std::string value = line.substr(static_cast<size_t>(equalPosition + 1));
                    value.erase(std::remove(value.begin(), value.end(), '"'), value.end());
                    OPENAUTO_LOG(debug) << "[Configuration] CS param found: " << searchKey << " Value:" << value;
                    return value;
                }
            }
        }
        OPENAUTO_LOG(warning) << "[Configuration] unable to find cs param: " << searchKey;
        OPENAUTO_LOG(warning) << "[Configuration] Fallback to /opt/crankshaft/crankshaft_default_env.sh)";
        while(inFile2.good())
        {
            getline(inFile2,line); // get line from file
            if (line[0] != '#') {
                pos=line.find(searchKey); // search
                if(pos!=std::string::npos) // string::npos is returned if string is not found
                {
                    int equalPosition = line.find("=");
                    std::string value = line.substr(static_cast<size_t>(equalPosition + 1));
                    value.erase(std::remove(value.begin(), value.end(), '"'), value.end());
                    OPENAUTO_LOG(debug) << "[Configuration] CS param found: " << searchKey << " Value:" << value;
                    return value;
                }
            }
        }
        return "";
    } else {
        OPENAUTO_LOG(warning) << "[Configuration] unable to open cs param file (/boot/crankshaft/crankshaft_env.sh)";
        OPENAUTO_LOG(warning) << "[Configuration] Fallback to /opt/crankshaft/crankshaft_default_env.sh)";

        while(inFile2.good())
        {
            getline(inFile2,line); // get line from file
            if (line[0] != '#') {
                pos=line.find(searchKey); // search
                if(pos!=std::string::npos) // string::npos is returned if string is not found
                {
                    int equalPosition = line.find("=");
                    std::string value = line.substr(static_cast<size_t>(equalPosition + 1));
                    value.erase(std::remove(value.begin(), value.end(), '"'), value.end());
                    OPENAUTO_LOG(debug) << "[Configuration] CS param found: " << searchKey << " Value:" << value;
                    return value;
                }
            }
        }
        return "";
    }
}

std::string Configuration::getParamFromFile(const std::string& fileName, const std::string& searchString) const
{
    OPENAUTO_LOG(debug) << "[Configuration] Request param from file: " << fileName << " param: " << searchString;
    using namespace std;
    ifstream inFile;
    string line;
    const bool needsEquals = searchString.find("dtoverlay") == std::string::npos;
    const std::string searchKey = needsEquals ? (searchString + "=") : searchString;
    inFile.open(fileName);

    size_t pos;

    if(inFile) {
        while(inFile.good())
        {
            getline(inFile,line); // get line from file
            if (line[0] != '#') {
                pos=line.find(searchKey); // search
                if(pos!=std::string::npos) // string::npos is returned if string is not found
                {
                    int equalPosition = line.find("=");
                    std::string value = line.substr(static_cast<size_t>(equalPosition + 1));
                    value.erase(std::remove(value.begin(), value.end(), '"'), value.end());
                    OPENAUTO_LOG(debug) << "[Configuration] Param from file: " << fileName << " found: " << searchKey << " Value:" << value;
                    return value;
                }
            }
        }
        return "";
    } else {
        return "";
    }
}

std::string Configuration::readFileContent(const std::string& fileName) const
{
    using namespace std;
    ifstream inFile;
    string line;
    inFile.open(fileName);
    string result = "";
    if(inFile) {
        while(inFile.good())
        {
            getline(inFile,line); // get line from file
            result.append(line);
        }
        return result;
    } else {
        return "";
    }
}

void Configuration::readButtonCodes(boost::property_tree::ptree& iniConfig)
{
    this->insertButtonCode(iniConfig, cInputPlayButtonKey, aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_MEDIA_PLAY);
    this->insertButtonCode(iniConfig, cInputPauseButtonKey, aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_MEDIA_PAUSE);
    this->insertButtonCode(iniConfig, cInputTogglePlayButtonKey, aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_MEDIA_PLAY_PAUSE);
    this->insertButtonCode(iniConfig, cInputNextTrackButtonKey, aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_MEDIA_NEXT);
    this->insertButtonCode(iniConfig, cInputPreviousTrackButtonKey, aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_MEDIA_PREVIOUS);
    this->insertButtonCode(iniConfig, cInputHomeButtonKey, aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_HOME);
    this->insertButtonCode(iniConfig, cInputPhoneButtonKey, aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_CALL);
    this->insertButtonCode(iniConfig, cInputCallEndButtonKey, aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_ENDCALL);
    this->insertButtonCode(iniConfig, cInputVoiceCommandButtonKey, aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_SEARCH);
    this->insertButtonCode(iniConfig, cInputLeftButtonKey, aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_DPAD_LEFT);
    this->insertButtonCode(iniConfig, cInputRightButtonKey, aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_DPAD_RIGHT);
    this->insertButtonCode(iniConfig, cInputUpButtonKey, aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_DPAD_UP);
    this->insertButtonCode(iniConfig, cInputDownButtonKey, aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_DPAD_DOWN);
    this->insertButtonCode(iniConfig, cInputScrollWheelButtonKey, aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_ROTARY_CONTROLLER);
    this->insertButtonCode(iniConfig, cInputBackButtonKey, aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_BACK);
    this->insertButtonCode(iniConfig, cInputEnterButtonKey, aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_DPAD_CENTER);
    this->insertButtonCode(iniConfig, cInputNavButtonKey, aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_NAVIGATION);
}

void Configuration::insertButtonCode(boost::property_tree::ptree& iniConfig, const std::string& buttonCodeKey, aap_protobuf::service::media::sink::message::KeyCode buttonCode)
{
    if(iniConfig.get<bool>(buttonCodeKey, false))
    {
        buttonCodes_.push_back(buttonCode);
    }
}

void Configuration::writeButtonCodes(boost::property_tree::ptree& iniConfig)
{
    iniConfig.put<bool>(cInputPlayButtonKey, std::find(buttonCodes_.begin(), buttonCodes_.end(), aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_MEDIA_PLAY) != buttonCodes_.end());
    iniConfig.put<bool>(cInputPauseButtonKey, std::find(buttonCodes_.begin(), buttonCodes_.end(), aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_MEDIA_PAUSE) != buttonCodes_.end());
    iniConfig.put<bool>(cInputTogglePlayButtonKey, std::find(buttonCodes_.begin(), buttonCodes_.end(), aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_MEDIA_PLAY_PAUSE) != buttonCodes_.end());
    iniConfig.put<bool>(cInputNextTrackButtonKey, std::find(buttonCodes_.begin(), buttonCodes_.end(), aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_MEDIA_NEXT) != buttonCodes_.end());
    iniConfig.put<bool>(cInputPreviousTrackButtonKey, std::find(buttonCodes_.begin(), buttonCodes_.end(), aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_MEDIA_PREVIOUS) != buttonCodes_.end());
    iniConfig.put<bool>(cInputHomeButtonKey, std::find(buttonCodes_.begin(), buttonCodes_.end(), aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_HOME) != buttonCodes_.end());
    iniConfig.put<bool>(cInputPhoneButtonKey, std::find(buttonCodes_.begin(), buttonCodes_.end(), aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_CALL) != buttonCodes_.end());
    iniConfig.put<bool>(cInputCallEndButtonKey, std::find(buttonCodes_.begin(), buttonCodes_.end(), aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_ENDCALL) != buttonCodes_.end());
    iniConfig.put<bool>(cInputVoiceCommandButtonKey, std::find(buttonCodes_.begin(), buttonCodes_.end(), aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_SEARCH) != buttonCodes_.end());
    iniConfig.put<bool>(cInputLeftButtonKey, std::find(buttonCodes_.begin(), buttonCodes_.end(), aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_DPAD_LEFT) != buttonCodes_.end());
    iniConfig.put<bool>(cInputRightButtonKey, std::find(buttonCodes_.begin(), buttonCodes_.end(), aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_DPAD_RIGHT) != buttonCodes_.end());
    iniConfig.put<bool>(cInputUpButtonKey, std::find(buttonCodes_.begin(), buttonCodes_.end(), aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_DPAD_UP) != buttonCodes_.end());
    iniConfig.put<bool>(cInputDownButtonKey, std::find(buttonCodes_.begin(), buttonCodes_.end(), aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_DPAD_DOWN) != buttonCodes_.end());
    iniConfig.put<bool>(cInputScrollWheelButtonKey, std::find(buttonCodes_.begin(), buttonCodes_.end(), aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_ROTARY_CONTROLLER) != buttonCodes_.end());
    iniConfig.put<bool>(cInputBackButtonKey, std::find(buttonCodes_.begin(), buttonCodes_.end(), aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_BACK) != buttonCodes_.end());
    iniConfig.put<bool>(cInputEnterButtonKey, std::find(buttonCodes_.begin(), buttonCodes_.end(), aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_DPAD_CENTER) != buttonCodes_.end());
    iniConfig.put<bool>(cInputNavButtonKey, std::find(buttonCodes_.begin(), buttonCodes_.end(), aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_NAVIGATION) != buttonCodes_.end());
}

}



