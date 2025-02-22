/*
 *  Copyright (C) 2020-2021 Jean-Luc Barrière
 *  Copyright (C) 2020 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <kodi/AddonBase.h>

#define LIVETV_CONFLICT_STRATEGY_HASLATER   0
#define LIVETV_CONFLICT_STRATEGY_STOPTV     1
#define LIVETV_CONFLICT_STRATEGY_CANCELREC  2

#define DEFAULT_HOST                        "127.0.0.1"
#define DEFAULT_EXTRA_DEBUG                 false
#define DEFAULT_LIVETV_PRIORITY             true
#define DEFAULT_LIVETV_CONFLICT_STRATEGY    LIVETV_CONFLICT_STRATEGY_HASLATER
#define DEFAULT_LIVETV                      true
#define DEFAULT_PROTO_PORT                  6543
#define DEFAULT_WSAPI_PORT                  6544
#define DEFAULT_WSAPI_SECURITY_PIN          "0000"
#define DEFAULT_CHANNEL_ICONS               true
#define DEFAULT_RECORDING_ICONS             true
#define DEFAULT_RECORD_TEMPLATE             1

#define MENUHOOK_REC_DELETE_AND_RERECORD    1
#define MENUHOOK_KEEP_RECORDING             2
#define MENUHOOK_TIMER_BACKEND_INFO         3
#define MENUHOOK_SHOW_HIDE_NOT_RECORDING    4
#define MENUHOOK_TRIGGER_CHANNEL_UPDATE     6
#define MENUHOOK_INFO_RECORDING             7
#define MENUHOOK_INFO_EPG                   8

#define DEFAULT_HANDLE_DEMUXING             false
#define DEFAULT_TUNE_DELAY                  5
#define GROUP_RECORDINGS_ALWAYS             0
#define GROUP_RECORDINGS_ONLY_FOR_SERIES    1
#define GROUP_RECORDINGS_NEVER              2
#define DEFAULT_USE_AIRDATE                 false
#define ENABLE_EDL_ALWAYS                   0
#define ENABLE_EDL_DIALOG                   1
#define ENABLE_EDL_NEVER                    2
#define ENABLE_EDL_SCENE                    3
#define DEFAULT_ALLOW_SHUTDOWN              true
#define DEFAULT_LIMIT_TUNE_ATTEMPTS         true
#define DEFAULT_SHOW_NOT_RECORDING          true
#define DEFAULT_LIVETV_RECORDINGS           true
#define DEFAULT_BACKEND_BOOKMARKS           true
#define DEFAULT_ROOT_DEFAULT_GROUP          false
#define DEFAULT_DAMAGED_COLOR               "yellow"

class PVRClientMythTV;

class CMythSettings
{
public:
  CMythSettings() = default;

  bool Load();
  ADDON_STATUS SetSetting(PVRClientMythTV& client, const std::string& settingName, const kodi::addon::CSettingValue& settingValue);

  static bool GetExtraDebug() { return m_bExtraDebug; }
  static const std::string& GetMythHostname() { return m_szMythHostname; }
  static const std::string& GetMythHostEther() { return m_szMythHostEther; }
  static int GetProtoPort() { return m_iProtoPort; }
  static int GetWSApiPort() { return m_iWSApiPort; }
  static const std::string& GetWSSecurityPin() { return m_szWSSecurityPin; }
  static bool GetLiveTV() { return m_bLiveTV; }
  static void SetLiveTVPriority(bool liveTVPriority) { m_bLiveTVPriority = liveTVPriority; }
  static bool GetLiveTVPriority() { return m_bLiveTVPriority; }
  static int GetLiveTVConflictStrategy() { return m_iLiveTVConflictStrategy; }
  static bool GetChannelIcons() { return m_bChannelIcons; }
  static bool GetRecordingIcons() { return m_bRecordingIcons; }
  static bool GetLiveTVRecordings() { return m_bLiveTVRecordings; }
  static int GetRecTemplateType() { return m_iRecTemplateType; }
  static bool GetRecAutoMetadata() { return m_bRecAutoMetadata; }
  static bool GetRecAutoCommFlag() { return m_bRecAutoCommFlag; }
  static bool GetRecAutoTranscode() { return m_bRecAutoTranscode; }
  static bool GetRecAutoRunJob1() { return m_bRecAutoRunJob1; }
  static bool GetRecAutoRunJob2() { return m_bRecAutoRunJob2; }
  static bool GetRecAutoRunJob3() { return m_bRecAutoRunJob3; }
  static bool GetRecAutoRunJob4() { return m_bRecAutoRunJob4; }
  static bool GetRecAutoExpire() { return m_bRecAutoExpire; }
  static int GetRecTranscoder() { return m_iRecTranscoder; }
  static bool GetDemuxing() { return m_bDemuxing; }
  static int GetTuneDelay() { return m_iTuneDelay; }
  static int GetGroupRecordings() { return m_iGroupRecordings; }
  static bool GetUseAirdate() { return m_bUseAirdate; }
  static int GetEnableEDL() { return m_iEnableEDL; }
  static bool GetAllowMythShutdown() { return m_bAllowMythShutdown; }
  static bool GetLimitTuneAttempts() { return m_bLimitTuneAttempts; }
  static bool GetShowNotRecording() { return m_bShowNotRecording; }
  static void ToogleShowNotRecording() { m_bShowNotRecording ^= true; }
  static bool GetUseBackendBookmarks() { return m_bUseBackendBookmarks; }
  static bool GetRootDefaultGroup() { return m_bRootDefaultGroup; }
  static const std::string& GetDamagedColor() { return m_szDamagedColor; }

private:
  static bool          m_bExtraDebug;
  static std::string   m_szMythHostname;
  static std::string   m_szMythHostEther;
  static int           m_iProtoPort;
  static int           m_iWSApiPort;
  static std::string   m_szWSSecurityPin;
  static bool          m_bLiveTV;
  static bool          m_bLiveTVPriority;
  static int           m_iLiveTVConflictStrategy;
  static bool          m_bChannelIcons;
  static bool          m_bRecordingIcons;
  static bool          m_bLiveTVRecordings;
  static int           m_iRecTemplateType;
  static bool          m_bRecAutoMetadata;
  static bool          m_bRecAutoCommFlag;
  static bool          m_bRecAutoTranscode;
  static bool          m_bRecAutoRunJob1;
  static bool          m_bRecAutoRunJob2;
  static bool          m_bRecAutoRunJob3;
  static bool          m_bRecAutoRunJob4;
  static bool          m_bRecAutoExpire;
  static int           m_iRecTranscoder;
  static bool          m_bDemuxing;
  static int           m_iTuneDelay;
  static int           m_iGroupRecordings;
  static bool          m_bUseAirdate;
  static int           m_iEnableEDL;
  static bool          m_bAllowMythShutdown;
  static bool          m_bLimitTuneAttempts;
  static bool          m_bShowNotRecording;
  static bool          m_bUseBackendBookmarks;
  static bool          m_bRootDefaultGroup;
  static std::string   m_szDamagedColor;
};
