/*
 * This file is part of the DSView project.
 * DSView is based on PulseView.
 * 
 * Copyright (C) 2021 DreamSourceLab <support@dreamsourcelab.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#pragma once

#include <string>
#include <vector>
#include <QString>
#include <QByteArray>
#include <QColor>

#define LAN_CN  25
#define LAN_EN  31

#define THEME_STYLE_DARK   "dark"
#define THEME_STYLE_LIGHT  "light"

#define APP_NAME  "DSView"
  
//--------------------api---
QString GetIconPath();
QString GetAppDataDir();
QString GetFirmwareDir();
QString GetUserDataDir();
QString GetDecodeScriptDir();
QString GetProfileDir();

//------------------class
  
class StringPair
{
public:
   StringPair(const std::string &key, const std::string &value);
   std::string m_key;
   std::string m_value;
};


#define APP_CONFIG_VERSION  3
#define NO_POINT_VALUE  -10000

struct AppOptions
{   
    int   version;
    bool  quickScroll;
    bool  warnofMultiTrig;
    bool  originalData;
    bool  ableSaveLog;
    bool  appendLogMode;
    int   logLevel;
    bool  transDecoderDlg;
    bool  trigPosDisplayInMid;
    bool  displayProfileInBar;
    bool  swapBackBufferAlways;
    bool  autoScrollLatestData;
    float fontSize;
    double scrollSpeed;

    std::vector<StringPair> m_protocolFormats;
};
 
 // The dock pannel open status.
 struct DockOptions
 {
  bool        decodeDock;
  bool        triggerDock;
  bool        measureDock;
  bool        searchDock;
};

struct FrameOptions
{ 
  QString     style;
  int         language; 
  int         left; //frame region
  int         top;
  int         right;
  int         bottom;
  int         x;
  int         y;
  int         ox;
  int         oy;
  bool        isMax;
  QString     displayName;
  QByteArray  windowState;

  DockOptions   _logicDock;
  DockOptions   _analogDock;
  DockOptions   _dsoDock;
};

struct UserHistory
{ 
  QString   exportDir;
  QString   saveDir;
  bool      showDocuments;
  QString   screenShotPath;
  QString   sessionDir;
  QString   openDir;
  QString   protocolExportPath;
  QString   exportFormat;
};

struct FontParam
{
  QString   name;
  float     size;
};

struct FontOptions
{
  FontParam toolbar;
  FontParam channelLabel;
  FontParam channelBody;
  FontParam ruler;
  FontParam title;
  FontParam other;
};

class AppConfig
{
private:
  AppConfig();
  ~AppConfig();
  AppConfig(AppConfig &o);

public:
  static AppConfig &Instance();

  void LoadAll();
  void SaveApp();  
  void SaveHistory();
  void SaveFrame();
  
  void SetProtocolFormat(const std::string &protocolName, const std::string &value);
  std::string GetProtocolFormat(const std::string &protocolName); 

  inline bool IsLangCn()
  {
    return frameOptions.language == LAN_CN;
  }

  static void GetFontSizeRange(float *minSize, float *maxSize);

  bool IsDarkStyle();

  QColor GetStyleColor();

public:
  AppOptions    appOptions;
  UserHistory   userHistory;
  FrameOptions  frameOptions;
};
