/*
 *  Copyright (C) 2020-2021 Jean-Luc Barrière
 *  Copyright (C) 2020 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "settings.h"

#include "pvrclient-mythtv.h"

#include <regex>

bool        CMythSettings::m_bExtraDebug             = DEFAULT_EXTRA_DEBUG;              ///< Output extensive debug information to the log
std::string CMythSettings::m_szMythHostname          = DEFAULT_HOST;                     ///< The Host name or IP of the mythtv server
std::string CMythSettings::m_szMythHostEther         = "";                               ///< The Host MAC address of the mythtv server
int         CMythSettings::m_iProtoPort              = DEFAULT_PROTO_PORT;               ///< The mythtv protocol port (default is 6543)
int         CMythSettings::m_iWSApiPort              = DEFAULT_WSAPI_PORT;               ///< The mythtv sevice API port (default is 6544)
std::string CMythSettings::m_szWSSecurityPin         = DEFAULT_WSAPI_SECURITY_PIN;       ///< The default security pin for the mythtv wsapi
bool        CMythSettings::m_bLiveTV                 = DEFAULT_LIVETV;                   ///< LiveTV support (or recordings only)
bool        CMythSettings::m_bLiveTVPriority         = DEFAULT_LIVETV_PRIORITY;          ///< MythTV Backend setting to allow live TV to move scheduled shows
int         CMythSettings::m_iLiveTVConflictStrategy = DEFAULT_LIVETV_CONFLICT_STRATEGY; ///< Conflict resolving strategy (0=
bool        CMythSettings::m_bChannelIcons           = DEFAULT_CHANNEL_ICONS;            ///< Load Channel Icons
bool        CMythSettings::m_bRecordingIcons         = DEFAULT_RECORDING_ICONS;          ///< Load Recording Icons (Fanart/Thumbnails)
bool        CMythSettings::m_bLiveTVRecordings       = DEFAULT_LIVETV_RECORDINGS;        ///< Show LiveTV recordings
int         CMythSettings::m_iRecTemplateType        = DEFAULT_RECORD_TEMPLATE;          ///< Template type for new record (0=Internal, 1=MythTV)
bool        CMythSettings::m_bRecAutoMetadata        = true;
bool        CMythSettings::m_bRecAutoCommFlag        = false;
bool        CMythSettings::m_bRecAutoTranscode       = false;
bool        CMythSettings::m_bRecAutoRunJob1         = false;
bool        CMythSettings::m_bRecAutoRunJob2         = false;
bool        CMythSettings::m_bRecAutoRunJob3         = false;
bool        CMythSettings::m_bRecAutoRunJob4         = false;
bool        CMythSettings::m_bRecAutoExpire          = false;
int         CMythSettings::m_iRecTranscoder          = 0;
bool        CMythSettings::m_bDemuxing               = DEFAULT_HANDLE_DEMUXING;
int         CMythSettings::m_iTuneDelay              = DEFAULT_TUNE_DELAY;
int         CMythSettings::m_iGroupRecordings        = GROUP_RECORDINGS_ALWAYS;
bool        CMythSettings::m_bUseAirdate             = DEFAULT_USE_AIRDATE;
int         CMythSettings::m_iEnableEDL              = ENABLE_EDL_ALWAYS;
bool        CMythSettings::m_bAllowMythShutdown      = DEFAULT_ALLOW_SHUTDOWN;
bool        CMythSettings::m_bLimitTuneAttempts      = DEFAULT_LIMIT_TUNE_ATTEMPTS;
bool        CMythSettings::m_bShowNotRecording       = DEFAULT_SHOW_NOT_RECORDING;
bool        CMythSettings::m_bUseBackendBookmarks    = DEFAULT_BACKEND_BOOKMARKS;
bool        CMythSettings::m_bRootDefaultGroup       = DEFAULT_ROOT_DEFAULT_GROUP;
std::string CMythSettings::m_szDamagedColor          = DEFAULT_DAMAGED_COLOR;

bool CMythSettings::Load()
{
  // Read settings
  kodi::Log(ADDON_LOG_DEBUG, "Loading settings...");

  /* Read setting "host" from settings.xml */
  if (!kodi::addon::CheckSettingString("host", m_szMythHostname))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'host' setting, falling back to '%s' as default", DEFAULT_HOST);
    m_szMythHostname = DEFAULT_HOST;
  }

  /* Read setting "port" from settings.xml */
  if (!kodi::addon::CheckSettingInt("port", m_iProtoPort))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'port' setting, falling back to '%d' as default", DEFAULT_PROTO_PORT);
    m_iProtoPort = DEFAULT_PROTO_PORT;
  }

  /* Read setting "wsport" from settings.xml */
  if (!kodi::addon::CheckSettingInt("wsport", m_iWSApiPort))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'wsport' setting, falling back to '%d' as default", DEFAULT_WSAPI_PORT);
    m_iWSApiPort = DEFAULT_WSAPI_PORT;
  }

  /* Read setting "wssecuritypin" from settings.xml */
  if (!kodi::addon::CheckSettingString("wssecuritypin", m_szWSSecurityPin))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'wssecuritypin' setting, falling back to '%s' as default", DEFAULT_WSAPI_SECURITY_PIN);
    m_szWSSecurityPin = DEFAULT_WSAPI_SECURITY_PIN;
  }

  /* Read setting "extradebug" from settings.xml */
  if (!kodi::addon::CheckSettingBoolean("extradebug", m_bExtraDebug))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'extradebug' setting, falling back to '%u' as default", DEFAULT_EXTRA_DEBUG);
    m_bExtraDebug = DEFAULT_EXTRA_DEBUG;
  }

  /* Read setting "LiveTV" from settings.xml */
  if (!kodi::addon::CheckSettingBoolean("livetv", m_bLiveTV))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'livetv' setting, falling back to '%u' as default", DEFAULT_LIVETV);
    m_bLiveTV = DEFAULT_LIVETV;
  }

  /* Read settings "Record livetv_conflict_method" from settings.xml */
  if (!kodi::addon::CheckSettingInt("livetv_conflict_strategy", m_iLiveTVConflictStrategy))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'livetv_conflict_method' setting, falling back to '%i' as default", DEFAULT_RECORD_TEMPLATE);
    m_iLiveTVConflictStrategy = DEFAULT_LIVETV_CONFLICT_STRATEGY;
  }

  /* Read settings "Record template" from settings.xml */
  if (!kodi::addon::CheckSettingInt("rec_template_provider", m_iRecTemplateType))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'rec_template_provider' setting, falling back to '%i' as default", DEFAULT_RECORD_TEMPLATE);
    m_iRecTemplateType = DEFAULT_RECORD_TEMPLATE;
  }
  /* Get internal template settings */
  m_bRecAutoMetadata = kodi::addon::GetSettingBoolean("rec_autometadata", true);
  m_bRecAutoCommFlag = kodi::addon::GetSettingBoolean("rec_autocommflag", false);
  m_bRecAutoTranscode = kodi::addon::GetSettingBoolean("rec_autotranscode", false);
  m_bRecAutoRunJob1 = kodi::addon::GetSettingBoolean("rec_autorunjob1", false);
  m_bRecAutoRunJob2 = kodi::addon::GetSettingBoolean("rec_autorunjob2", false);
  m_bRecAutoRunJob3 = kodi::addon::GetSettingBoolean("rec_autorunjob3", false);
  m_bRecAutoRunJob4 = kodi::addon::GetSettingBoolean("rec_autorunjob4", false);
  m_bRecAutoExpire = kodi::addon::GetSettingBoolean("rec_autoexpire", false);
  m_iRecTranscoder = kodi::addon::GetSettingInt("rec_transcoder", 0);

  /* Read setting "demuxing" from settings.xml */
  if (!kodi::addon::CheckSettingBoolean("demuxing", m_bDemuxing))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'demuxing' setting, falling back to '%b' as default", DEFAULT_HANDLE_DEMUXING);
    m_bDemuxing = DEFAULT_HANDLE_DEMUXING;
  }

  /* Read setting "tunedelay" from settings.xml */
  if (!kodi::addon::CheckSettingInt("tunedelay", m_iTuneDelay))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'tunedelay' setting, falling back to '%d' as default", DEFAULT_TUNE_DELAY);
    m_iTuneDelay = DEFAULT_TUNE_DELAY;
  }

  /* Read setting "host_ether" from settings.xml */
  m_szMythHostEther = kodi::addon::GetSettingString("host_ether", "");

  /* Read settings "group_recordings" from settings.xml */
  if (!kodi::addon::CheckSettingInt("group_recordings", m_iGroupRecordings))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'group_recordings' setting, falling back to '%i' as default", GROUP_RECORDINGS_ALWAYS);
    m_iGroupRecordings = GROUP_RECORDINGS_ALWAYS;
  }

  /* Read setting "use_airdate" from settings.xml */
  if (!kodi::addon::CheckSettingBoolean("use_airdate", m_bUseAirdate))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'use_airdate' setting, falling back to '%u' as default", DEFAULT_USE_AIRDATE);
    m_bUseAirdate = DEFAULT_USE_AIRDATE;
  }

  /* Read setting "enable_edl" from settings.xml */
  if (!kodi::addon::CheckSettingInt("enable_edl", m_iEnableEDL))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'enable_edl' setting, falling back to '%i' as default", ENABLE_EDL_ALWAYS);
    m_iEnableEDL = ENABLE_EDL_ALWAYS;
  }

  /* Read setting "allow_shutdown" from settings.xml */
  if (!kodi::addon::CheckSettingBoolean("allow_shutdown", m_bAllowMythShutdown))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'allow_shutdown' setting, falling back to '%u' as default", DEFAULT_ALLOW_SHUTDOWN);
    m_bAllowMythShutdown = DEFAULT_ALLOW_SHUTDOWN;
  }

  /* Read setting "channel_icons" from settings.xml */
  if (!kodi::addon::CheckSettingBoolean("channel_icons", m_bChannelIcons))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'channel_icons' setting, falling back to '%u' as default", DEFAULT_CHANNEL_ICONS);
    m_bChannelIcons = DEFAULT_CHANNEL_ICONS;
  }

  /* Read setting "recording_icons" from settings.xml */
  if (!kodi::addon::CheckSettingBoolean("recording_icons", m_bRecordingIcons))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'recording_icons' setting, falling back to '%u' as default", DEFAULT_RECORDING_ICONS);
    m_bRecordingIcons = DEFAULT_RECORDING_ICONS;
  }

  /* Read setting "limit_tune_attempts" from settings.xml */
  if (!kodi::addon::CheckSettingBoolean("limit_tune_attempts", m_bLimitTuneAttempts))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'limit_tune_attempts' setting, falling back to '%u' as default", DEFAULT_LIMIT_TUNE_ATTEMPTS);
    m_bLimitTuneAttempts = DEFAULT_LIMIT_TUNE_ATTEMPTS;
  }

  /* Read setting "inactive_upcomings" from settings.xml */
  if (!kodi::addon::CheckSettingBoolean("inactive_upcomings", m_bShowNotRecording))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'inactive_upcomings' setting, falling back to '%u' as default", DEFAULT_SHOW_NOT_RECORDING);
    m_bShowNotRecording = DEFAULT_SHOW_NOT_RECORDING;
  }

  /* Read setting "livetv_recordings" from settings.xml */
  if (!kodi::addon::CheckSettingBoolean("livetv_recordings", m_bLiveTVRecordings))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'livetv_recordings' setting, falling back to '%u' as default", DEFAULT_LIVETV_RECORDINGS);
    m_bLiveTVRecordings = DEFAULT_LIVETV_RECORDINGS;
  }

  /* Read setting "backend_bookmarks" from settings.xml */
  if (!kodi::addon::CheckSettingBoolean("backend_bookmarks", m_bUseBackendBookmarks))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'backend_bookmarks' setting, falling back to '%u' as default", DEFAULT_BACKEND_BOOKMARKS);
    m_bUseBackendBookmarks = DEFAULT_BACKEND_BOOKMARKS;
  }

  /* Read setting "root_default_group" from settings.xml */
  if (!kodi::addon::CheckSettingBoolean("root_default_group", m_bRootDefaultGroup))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'root_default_group' setting, falling back to '%u' as default", DEFAULT_ROOT_DEFAULT_GROUP);
    m_bRootDefaultGroup = DEFAULT_ROOT_DEFAULT_GROUP;
  }

  /* Read setting "damaged_color" from settings.xml */
  std::string buffer;
  if (kodi::addon::CheckSettingString("damaged_color", buffer))
  {
    std::regex rgx("^\\[COLOR\\ .*\\](.*)\\[\\/COLOR\\]");
    std::smatch match;
    if (std::regex_search(buffer, match, rgx))
      m_szDamagedColor = match[1].str();
    else
      m_szDamagedColor = "";
  }
  else
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'damaged_color' setting, falling back to '%s' as default", DEFAULT_DAMAGED_COLOR);
    m_szDamagedColor = DEFAULT_DAMAGED_COLOR;
  }
  buffer[0] = 0;

  kodi::Log(ADDON_LOG_DEBUG, "Loading settings...done");
  return true;
}

ADDON_STATUS CMythSettings::SetSetting(PVRClientMythTV& client,
                                       const std::string& settingName,
                                       const kodi::addon::CSettingValue& settingValue)
{
  if (settingName == "host")
  {
    std::string tmp_sHostname;
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'host' from %s to %s", m_szMythHostname.c_str(), settingValue.GetString().c_str());
    tmp_sHostname = m_szMythHostname;
    m_szMythHostname = settingValue.GetString();
    if (tmp_sHostname != m_szMythHostname)
    {
      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (settingName == "port")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'port' from %u to %u", m_iProtoPort, settingValue.GetInt());
    if (m_iProtoPort != settingValue.GetInt())
    {
      m_iProtoPort = settingValue.GetInt();
      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (settingName == "wsport")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'wsport' from %u to %u", m_iWSApiPort, settingValue.GetInt());
    if (m_iWSApiPort != settingValue.GetInt())
    {
      m_iWSApiPort = settingValue.GetInt();
      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (settingName == "wssecuritypin")
  {
    std::string tmp_sWSSecurityPin;
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'wssecuritypin' from %s to %s", m_szWSSecurityPin.c_str(), settingValue.GetString().c_str());
    tmp_sWSSecurityPin = m_szWSSecurityPin;
    m_szWSSecurityPin = settingValue.GetString();
    if (tmp_sWSSecurityPin != m_szWSSecurityPin)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (settingName == "demuxing")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'demuxing' from %u to %u", m_bDemuxing, settingValue.GetBoolean());
    if (m_bDemuxing != settingValue.GetBoolean())
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (settingName == "channel_icons")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'channel_icons' from %u to %u", m_bChannelIcons, settingValue.GetBoolean());
    if (m_bChannelIcons != settingValue.GetBoolean())
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (settingName == "recording_icons")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'recording_icons' from %u to %u", m_bRecordingIcons, settingValue.GetBoolean());
    if (m_bRecordingIcons != settingValue.GetBoolean())
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (settingName == "backend_bookmarks")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'backend_bookmarks' from %u to %u", m_bUseBackendBookmarks, settingValue.GetBoolean());
    if (m_bUseBackendBookmarks != settingValue.GetBoolean())
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (settingName == "host_ether")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'host_ether' from %s to %s", m_szMythHostEther.c_str(), settingValue.GetString().c_str());
    m_szMythHostEther = settingValue.GetString();
  }
  else if (settingName == "extradebug")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'extra debug' from %u to %u", m_bExtraDebug, settingValue.GetBoolean());
    if (m_bExtraDebug != settingValue.GetBoolean())
    {
      m_bExtraDebug = settingValue.GetBoolean();
      client.SetDebug();
    }
  }
  else if (settingName == "livetv")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'livetv' from %u to %u", m_bLiveTV, settingValue.GetBoolean());
    if (m_bLiveTV != settingValue.GetBoolean())
      m_bLiveTV = settingValue.GetBoolean();
  }
  else if (settingName == "livetv_priority")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'extra debug' from %u to %u", m_bLiveTVPriority, settingValue.GetBoolean());
    if (m_bLiveTVPriority != settingValue.GetBoolean() && client.GetConnectionError() == PVRClientMythTV::CONN_ERROR_NO_ERROR)
    {
      m_bLiveTVPriority = settingValue.GetBoolean();
      client.SetLiveTVPriority(m_bLiveTVPriority);
    }
  }
  else if (settingName == "rec_template_provider")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'rec_template_provider' from %u to %u", m_iRecTemplateType, settingValue.GetInt());
    if (m_iRecTemplateType != settingValue.GetInt())
      m_iRecTemplateType = settingValue.GetInt();
  }
  else if (settingName == "rec_autometadata")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'rec_autometadata' from %u to %u", m_bRecAutoMetadata, settingValue.GetBoolean());
    if (m_bRecAutoMetadata != settingValue.GetBoolean())
      m_bRecAutoMetadata = settingValue.GetBoolean();
  }
  else if (settingName == "rec_autocommflag")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'rec_autocommflag' from %u to %u", m_bRecAutoCommFlag, settingValue.GetBoolean());
    if (m_bRecAutoCommFlag != settingValue.GetBoolean())
      m_bRecAutoCommFlag = settingValue.GetBoolean();
  }
  else if (settingName == "rec_autotranscode")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'rec_autotranscode' from %u to %u", m_bRecAutoTranscode, settingValue.GetBoolean());
    if (m_bRecAutoTranscode != settingValue.GetBoolean())
      m_bRecAutoTranscode = settingValue.GetBoolean();
  }
  else if (settingName == "rec_transcoder")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'rec_transcoder' from %u to %u", m_iRecTranscoder, settingValue.GetInt());
    if (m_iRecTranscoder != settingValue.GetInt())
      m_iRecTranscoder = settingValue.GetInt();
  }
  else if (settingName == "rec_autorunjob1")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'rec_autorunjob1' from %u to %u", m_bRecAutoRunJob1, settingValue.GetBoolean());
    if (m_bRecAutoRunJob1 != settingValue.GetBoolean())
      m_bRecAutoRunJob1 = settingValue.GetBoolean();
  }
  else if (settingName == "rec_autorunjob2")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'rec_autorunjob2' from %u to %u", m_bRecAutoRunJob2, settingValue.GetBoolean());
    if (m_bRecAutoRunJob2 != settingValue.GetBoolean())
      m_bRecAutoRunJob2 = settingValue.GetBoolean();
  }
  else if (settingName == "rec_autorunjob3")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'rec_autorunjob3' from %u to %u", m_bRecAutoRunJob3, settingValue.GetBoolean());
    if (m_bRecAutoRunJob3 != settingValue.GetBoolean())
      m_bRecAutoRunJob3 = settingValue.GetBoolean();
  }
  else if (settingName == "rec_autorunjob4")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'rec_autorunjob4' from %u to %u", m_bRecAutoRunJob4, settingValue.GetBoolean());
    if (m_bRecAutoRunJob4 != settingValue.GetBoolean())
      m_bRecAutoRunJob4 = settingValue.GetBoolean();
  }
  else if (settingName == "rec_autoexpire")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'rec_autoexpire' from %u to %u", m_bRecAutoExpire, settingValue.GetBoolean());
    if (m_bRecAutoExpire != settingValue.GetBoolean())
      m_bRecAutoExpire = settingValue.GetBoolean();
  }
  else if (settingName == "tunedelay")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'tunedelay' from %d to %d", m_iTuneDelay, settingValue.GetInt());
    if (m_iTuneDelay != settingValue.GetInt())
      m_iTuneDelay = settingValue.GetInt();
  }
  else if (settingName == "group_recordings")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'group_recordings' from %u to %u", m_iGroupRecordings, settingValue.GetInt());
    if (m_iGroupRecordings != settingValue.GetInt())
    {
      m_iGroupRecordings = settingValue.GetInt();
      client.TriggerRecordingUpdate();
    }
  }
  else if (settingName == "use_airdate")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'use_airdate' from %u to %u", m_bUseAirdate, settingValue.GetBoolean());
    if (m_bUseAirdate != settingValue.GetBoolean())
    {
      m_bUseAirdate = settingValue.GetBoolean();
      client.TriggerRecordingUpdate();
    }
  }
  else if (settingName == "enable_edl")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'enable_edl' from %u to %u", m_iEnableEDL, settingValue.GetInt());
    if (m_iEnableEDL != settingValue.GetInt())
      m_iEnableEDL = settingValue.GetInt();
  }
  else if (settingName == "allow_shutdown")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'allow_shutdown' from %u to %u", m_bAllowMythShutdown, settingValue.GetBoolean());
    if (m_bAllowMythShutdown != settingValue.GetBoolean())
      m_bAllowMythShutdown = settingValue.GetBoolean();
  }
  else if (settingName == "limit_tune_attempts")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'limit_tune_attempts' from %u to %u", m_bLimitTuneAttempts, settingValue.GetBoolean());
    if (m_bLimitTuneAttempts != settingValue.GetBoolean())
      m_bLimitTuneAttempts = settingValue.GetBoolean();
  }
  else if (settingName == "inactive_upcomings")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'inactive_upcomings' from %u to %u", m_bShowNotRecording, settingValue.GetBoolean());
    if (m_bShowNotRecording != settingValue.GetBoolean())
    {
      m_bShowNotRecording = settingValue.GetBoolean();
      client.HandleScheduleChange();
    }
  }
  else if (settingName == "livetv_recordings")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'livetv_recordings' from %u to %u", m_bLiveTVRecordings, settingValue.GetBoolean());
    if (m_bLiveTVRecordings != settingValue.GetBoolean())
    {
      m_bLiveTVRecordings = settingValue.GetBoolean();
      client.TriggerRecordingUpdate();
    }
  }
  else if (settingName == "root_default_group")
  {
    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'root_default_group' from %u to %u", m_bRootDefaultGroup, settingValue.GetBoolean());
    if (m_bRootDefaultGroup != settingValue.GetBoolean())
    {
      m_bRootDefaultGroup = settingValue.GetBoolean();
      client.TriggerRecordingUpdate();
    }
  }
  else if (settingName == "damaged_color")
  {
    std::string tmp_sDamagedColor = m_szDamagedColor;

    std::regex rgx("^\\[COLOR\\ .*\\](.*)\\[\\/COLOR\\]");
    std::smatch match;
    if (std::regex_search(m_szDamagedColor, match, rgx))
      m_szDamagedColor = match[1].str();
    else
      m_szDamagedColor = "";

    kodi::Log(ADDON_LOG_INFO, "Changed Setting 'damaged_color' from %s to %s", tmp_sDamagedColor.c_str(), m_szDamagedColor.c_str());
    if (tmp_sDamagedColor != m_szDamagedColor)
    {
      client.TriggerRecordingUpdate();
    }
  }

  return ADDON_STATUS_OK;
}
