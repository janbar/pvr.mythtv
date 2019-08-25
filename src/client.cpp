/*
 *      Copyright (C) 2005-2014 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301 USA
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "client.h"
#include "pvrclient-mythtv.h"
#include "pvrclient-launcher.h"

#include <kodi/xbmc_pvr_dll.h>

///* undefined constants in pvr API */
#define SEEK_POSSIBLE 0x10 ///< flag used to check if protocol allows seeks

using namespace ADDON;

/* User adjustable settings are saved here.
 * Default values are defined inside client.h
 * and exported to the other source files.
 */
std::string   g_szMythHostname          = DEFAULT_HOST;                     ///< The Host name or IP of the mythtv server
std::string   g_szMythHostEther         = "";                               ///< The Host MAC address of the mythtv server
int           g_iProtoPort              = DEFAULT_PROTO_PORT;               ///< The mythtv protocol port (default is 6543)
int           g_iWSApiPort              = DEFAULT_WSAPI_PORT;               ///< The mythtv sevice API port (default is 6544)
std::string   g_szWSSecurityPin         = DEFAULT_WSAPI_SECURITY_PIN;       ///< The default security pin for the mythtv wsapi
bool          g_bExtraDebug             = DEFAULT_EXTRA_DEBUG;              ///< Output extensive debug information to the log
bool          g_bLiveTV                 = DEFAULT_LIVETV;                   ///< LiveTV support (or recordings only)
bool          g_bLiveTVPriority         = DEFAULT_LIVETV_PRIORITY;          ///< MythTV Backend setting to allow live TV to move scheduled shows
int           g_iLiveTVConflictStrategy = DEFAULT_LIVETV_CONFLICT_STRATEGY; ///< Conflict resolving strategy (0=
bool          g_bChannelIcons           = DEFAULT_CHANNEL_ICONS;            ///< Load Channel Icons
bool          g_bRecordingIcons         = DEFAULT_RECORDING_ICONS;          ///< Load Recording Icons (Fanart/Thumbnails)
bool          g_bLiveTVRecordings       = DEFAULT_LIVETV_RECORDINGS;        ///< Show LiveTV recordings
int           g_iRecTemplateType        = DEFAULT_RECORD_TEMPLATE;          ///< Template type for new record (0=Internal, 1=MythTV)
bool          g_bRecAutoMetadata        = true;
bool          g_bRecAutoCommFlag        = false;
bool          g_bRecAutoTranscode       = false;
bool          g_bRecAutoRunJob1         = false;
bool          g_bRecAutoRunJob2         = false;
bool          g_bRecAutoRunJob3         = false;
bool          g_bRecAutoRunJob4         = false;
bool          g_bRecAutoExpire          = false;
int           g_iRecTranscoder          = 0;
int           g_iTuneDelay              = DEFAULT_TUNE_DELAY;
int           g_iGroupRecordings        = GROUP_RECORDINGS_ALWAYS;
bool          g_bUseAirdate             = DEFAULT_USE_AIRDATE;
int           g_iEnableEDL              = ENABLE_EDL_ALWAYS;
bool          g_bBlockMythShutdown      = DEFAULT_BLOCK_SHUTDOWN;
bool          g_bLimitTuneAttempts      = DEFAULT_LIMIT_TUNE_ATTEMPTS;
bool          g_bShowNotRecording       = DEFAULT_SHOW_NOT_RECORDING;
bool          g_bPromptDeleteAtEnd      = DEFAULT_PROMPT_DELETE;
bool          g_bUseBackendBookmarks    = DEFAULT_BACKEND_BOOKMARKS;
bool          g_bRootDefaultGroup       = DEFAULT_ROOT_DEFAULT_GROUP;
std::string   g_szDamagedColor          = DEFAULT_DAMAGED_COLOR;

///* Client member variables */
ADDON_STATUS  m_CurStatus               = ADDON_STATUS_UNKNOWN;
bool          g_bCreated                = false;
int           g_iClientID               = -1;
std::string   g_szUserPath              = "";
std::string   g_szClientPath            = "";

PVRClientMythTV         *g_client       = NULL;
PVRClientLauncher       *g_launcher     = NULL;

CHelper_libXBMC_addon   *XBMC           = NULL;
CHelper_libXBMC_pvr     *PVR            = NULL;
CHelper_libKODI_guilib  *GUI            = NULL;

extern "C" {

/***********************************************************
 * Standard AddOn related public library functions
 ***********************************************************/

ADDON_STATUS ADDON_Create(void *hdl, void *props)
{
  if (!hdl)
    return ADDON_STATUS_PERMANENT_FAILURE;
  SAFE_DELETE(g_launcher);

  // Register handles
  XBMC = new CHelper_libXBMC_addon;

  if (!XBMC->RegisterMe(hdl))
  {
    SAFE_DELETE(XBMC);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }
  XBMC->Log(LOG_NOTICE, "Creating MythTV PVR-Client");
  XBMC->Log(LOG_NOTICE, "Addon compiled with PVR API version %s", STR(ADDON_INSTANCE_VERSION_PVR));
  XBMC->Log(LOG_DEBUG, "Register handle @ libXBMC_addon...done");
  XBMC->Log(LOG_DEBUG, "Checking props...");
  if (!props)
  {
    SAFE_DELETE(XBMC);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }
  XBMC->Log(LOG_DEBUG, "Checking props...done");
  PVR_PROPERTIES* pvrprops = (PVR_PROPERTIES*)props;

  XBMC->Log(LOG_DEBUG, "Register handle @ libXBMC_pvr...");
  PVR = new CHelper_libXBMC_pvr;
  if (!PVR->RegisterMe(hdl))
  {
    SAFE_DELETE(PVR);
    SAFE_DELETE(XBMC);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }
  XBMC->Log(LOG_DEBUG, "Register handle @ libXBMC_pvr...done");

  XBMC->Log(LOG_DEBUG, "Register handle @ libXBMC_gui...");
  GUI = new CHelper_libKODI_guilib;
  if (!GUI->RegisterMe(hdl))
  {
    SAFE_DELETE(PVR);
    SAFE_DELETE(XBMC);
    SAFE_DELETE(GUI);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }
  XBMC->Log(LOG_DEBUG, "Register handle @ libXBMC_gui...done");

  m_CurStatus    = ADDON_STATUS_UNKNOWN;
  g_szUserPath   = pvrprops->strUserPath;
  g_szClientPath = pvrprops->strClientPath;

  // Read settings
  XBMC->Log(LOG_DEBUG, "Loading settings...");
  char *buffer = new char [1024];
  buffer[0] = 0; /* Set the end of string */

  /* Read setting "host" from settings.xml */
  if (XBMC->GetSetting("host", buffer))
    g_szMythHostname = buffer;
  else
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'host' setting, falling back to '%s' as default", DEFAULT_HOST);
    g_szMythHostname = DEFAULT_HOST;
  }
  buffer[0] = 0;

  /* Read setting "port" from settings.xml */
  if (!XBMC->GetSetting("port", &g_iProtoPort))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'port' setting, falling back to '%d' as default", DEFAULT_PROTO_PORT);
    g_iProtoPort = DEFAULT_PROTO_PORT;
  }

  /* Read setting "wsport" from settings.xml */
  if (!XBMC->GetSetting("wsport", &g_iWSApiPort))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'wsport' setting, falling back to '%d' as default", DEFAULT_WSAPI_PORT);
    g_iWSApiPort = DEFAULT_WSAPI_PORT;
  }

  /* Read setting "wssecuritypin" from settings.xml */
  if (XBMC->GetSetting("wssecuritypin", buffer))
    g_szWSSecurityPin = buffer;
  else
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'wssecuritypin' setting, falling back to '%s' as default", DEFAULT_WSAPI_SECURITY_PIN);
    g_szWSSecurityPin = DEFAULT_WSAPI_SECURITY_PIN;
  }
  buffer[0] = 0;

  /* Read setting "extradebug" from settings.xml */
  if (!XBMC->GetSetting("extradebug", &g_bExtraDebug))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'extradebug' setting, falling back to '%u' as default", DEFAULT_EXTRA_DEBUG);
    g_bExtraDebug = DEFAULT_EXTRA_DEBUG;
  }

  /* Read setting "LiveTV" from settings.xml */
  if (!XBMC->GetSetting("livetv", &g_bLiveTV))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'livetv' setting, falling back to '%u' as default", DEFAULT_LIVETV);
    g_bLiveTV = DEFAULT_LIVETV;
  }

  /* Read settings "Record livetv_conflict_method" from settings.xml */
  if (!XBMC->GetSetting("livetv_conflict_strategy", &g_iLiveTVConflictStrategy))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'livetv_conflict_method' setting, falling back to '%i' as default", DEFAULT_RECORD_TEMPLATE);
    g_iLiveTVConflictStrategy = DEFAULT_LIVETV_CONFLICT_STRATEGY;
  }

  /* Read settings "Record template" from settings.xml */
  if (!XBMC->GetSetting("rec_template_provider", &g_iRecTemplateType))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'rec_template_provider' setting, falling back to '%i' as default", DEFAULT_RECORD_TEMPLATE);
    g_iRecTemplateType = DEFAULT_RECORD_TEMPLATE;
  }
  /* Get internal template settings */
  if (!XBMC->GetSetting("rec_autometadata", &g_bRecAutoMetadata))
    g_bRecAutoMetadata = true;
  if (!XBMC->GetSetting("rec_autocommflag", &g_bRecAutoCommFlag))
    g_bRecAutoCommFlag = false;
  if (!XBMC->GetSetting("rec_autotranscode", &g_bRecAutoTranscode))
    g_bRecAutoTranscode = false;
  if (!XBMC->GetSetting("rec_autorunjob1", &g_bRecAutoRunJob1))
    g_bRecAutoRunJob1 = false;
  if (!XBMC->GetSetting("rec_autorunjob2", &g_bRecAutoRunJob2))
    g_bRecAutoRunJob2 = false;
  if (!XBMC->GetSetting("rec_autorunjob3", &g_bRecAutoRunJob3))
    g_bRecAutoRunJob3 = false;
  if (!XBMC->GetSetting("rec_autorunjob4", &g_bRecAutoRunJob4))
    g_bRecAutoRunJob4 = false;
  if (!XBMC->GetSetting("rec_autoexpire", &g_bRecAutoExpire))
    g_bRecAutoExpire = false;
  if (!XBMC->GetSetting("rec_transcoder", &g_iRecTranscoder))
    g_iRecTranscoder = 0;

  /* Read setting "tunedelay" from settings.xml */
  if (!XBMC->GetSetting("tunedelay", &g_iTuneDelay))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'tunedelay' setting, falling back to '%d' as default", DEFAULT_TUNE_DELAY);
    g_iTuneDelay = DEFAULT_TUNE_DELAY;
  }

  /* Read setting "host_ether" from settings.xml */
  if (XBMC->GetSetting("host_ether", buffer))
    g_szMythHostEther = buffer;
  else
  {
    /* If setting is unknown fallback to defaults */
    g_szMythHostEther = "";
  }
  buffer[0] = 0;

  /* Read settings "group_recordings" from settings.xml */
  if (!XBMC->GetSetting("group_recordings", &g_iGroupRecordings))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'group_recordings' setting, falling back to '%i' as default", GROUP_RECORDINGS_ALWAYS);
    g_iGroupRecordings = GROUP_RECORDINGS_ALWAYS;
  }

  /* Read setting "use_airdate" from settings.xml */
  if (!XBMC->GetSetting("use_airdate", &g_bUseAirdate))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'use_airdate' setting, falling back to '%u' as default", DEFAULT_USE_AIRDATE);
    g_bUseAirdate = DEFAULT_USE_AIRDATE;
  }

  /* Read setting "enable_edl" from settings.xml */
  if (!XBMC->GetSetting("enable_edl", &g_iEnableEDL))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'enable_edl' setting, falling back to '%i' as default", ENABLE_EDL_ALWAYS);
    g_iEnableEDL = ENABLE_EDL_ALWAYS;
  }

  /* Read setting "block_shutdown" from settings.xml */
  if (!XBMC->GetSetting("block_shutdown", &g_bBlockMythShutdown))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'block_shutdown' setting, falling back to '%u' as default", DEFAULT_BLOCK_SHUTDOWN);
    g_bBlockMythShutdown = DEFAULT_BLOCK_SHUTDOWN;
  }

  /* Read setting "channel_icons" from settings.xml */
  if (!XBMC->GetSetting("channel_icons", &g_bChannelIcons))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'channel_icons' setting, falling back to '%u' as default", DEFAULT_CHANNEL_ICONS);
    g_bChannelIcons = DEFAULT_CHANNEL_ICONS;
  }

  /* Read setting "recording_icons" from settings.xml */
  if (!XBMC->GetSetting("recording_icons", &g_bRecordingIcons))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'recording_icons' setting, falling back to '%u' as default", DEFAULT_RECORDING_ICONS);
    g_bRecordingIcons = DEFAULT_RECORDING_ICONS;
  }

  /* Read setting "limit_tune_attempts" from settings.xml */
  if (!XBMC->GetSetting("limit_tune_attempts", &g_bLimitTuneAttempts))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'limit_tune_attempts' setting, falling back to '%u' as default", DEFAULT_LIMIT_TUNE_ATTEMPTS);
    g_bLimitTuneAttempts = DEFAULT_LIMIT_TUNE_ATTEMPTS;
  }

  /* Read setting "inactive_upcomings" from settings.xml */
  if (!XBMC->GetSetting("inactive_upcomings", &g_bShowNotRecording))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'inactive_upcomings' setting, falling back to '%u' as default", DEFAULT_SHOW_NOT_RECORDING);
    g_bShowNotRecording = DEFAULT_SHOW_NOT_RECORDING;
  }

  /* Read setting "prompt_delete" from settings.xml */
  if (!XBMC->GetSetting("prompt_delete", &g_bPromptDeleteAtEnd))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'prompt_delete' setting, falling back to '%u' as default", DEFAULT_PROMPT_DELETE);
    g_bPromptDeleteAtEnd = DEFAULT_PROMPT_DELETE;
  }

  /* Read setting "livetv_recordings" from settings.xml */
  if (!XBMC->GetSetting("livetv_recordings", &g_bLiveTVRecordings))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'livetv_recordings' setting, falling back to '%u' as default", DEFAULT_LIVETV_RECORDINGS);
    g_bLiveTVRecordings = DEFAULT_LIVETV_RECORDINGS;
  }

  /* Read setting "backend_bookmarks" from settings.xml */
  if (!XBMC->GetSetting("backend_bookmarks", &g_bUseBackendBookmarks))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'backend_bookmarks' setting, falling back to '%u' as default", DEFAULT_BACKEND_BOOKMARKS);
    g_bUseBackendBookmarks = DEFAULT_BACKEND_BOOKMARKS;
  }

  /* Read setting "root_default_group" from settings.xml */
  if (!XBMC->GetSetting("root_default_group", &g_bRootDefaultGroup))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'root_default_group' setting, falling back to '%u' as default", DEFAULT_ROOT_DEFAULT_GROUP);
    g_bRootDefaultGroup = DEFAULT_ROOT_DEFAULT_GROUP;
  }

  /* Read setting "damaged_color" from settings.xml */
  if (XBMC->GetSetting("damaged_color", buffer))
  {
    char* p = buffer;
    while (*p != '\0' && *p != ']') ++p;
    std::string s = std::string(buffer, p - buffer);
    if (sscanf(s.c_str(), "[COLOR %s", buffer) == 1)
      g_szDamagedColor = buffer;
    else
      g_szDamagedColor = "";
  }
  else
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'damaged_color' setting, falling back to '%s' as default", DEFAULT_DAMAGED_COLOR);
    g_szDamagedColor = DEFAULT_DAMAGED_COLOR;
  }
  buffer[0] = 0;

  delete [] buffer;
  XBMC->Log(LOG_DEBUG, "Loading settings...done");

  // Create menu hooks
  XBMC->Log(LOG_DEBUG, "Creating menu hooks...");
  PVR_MENUHOOK menuHook;
  memset(&menuHook, 0, sizeof(PVR_MENUHOOK));
  menuHook.category = PVR_MENUHOOK_RECORDING;
  menuHook.iHookId = MENUHOOK_REC_DELETE_AND_RERECORD;
  menuHook.iLocalizedStringId = 30411;
  PVR->AddMenuHook(&menuHook);

  memset(&menuHook, 0, sizeof(PVR_MENUHOOK));
  menuHook.category = PVR_MENUHOOK_RECORDING;
  menuHook.iHookId = MENUHOOK_KEEP_RECORDING;
  menuHook.iLocalizedStringId = 30412;
  PVR->AddMenuHook(&menuHook);

  memset(&menuHook, 0, sizeof(PVR_MENUHOOK));
  menuHook.category = PVR_MENUHOOK_RECORDING;
  menuHook.iHookId = MENUHOOK_INFO_RECORDING;
  menuHook.iLocalizedStringId = 30425;
  PVR->AddMenuHook(&menuHook);

  memset(&menuHook, 0, sizeof(PVR_MENUHOOK));
  menuHook.category = PVR_MENUHOOK_TIMER;
  menuHook.iHookId = MENUHOOK_TIMER_BACKEND_INFO;
  menuHook.iLocalizedStringId = 30424;
  PVR->AddMenuHook(&menuHook);

  memset(&menuHook, 0, sizeof(PVR_MENUHOOK));
  menuHook.category = PVR_MENUHOOK_TIMER;
  menuHook.iHookId = MENUHOOK_SHOW_HIDE_NOT_RECORDING;
  menuHook.iLocalizedStringId = 30421;
  PVR->AddMenuHook(&menuHook);

  memset(&menuHook, 0, sizeof(PVR_MENUHOOK));
  menuHook.category = PVR_MENUHOOK_CHANNEL;
  menuHook.iHookId = MENUHOOK_TRIGGER_CHANNEL_UPDATE;
  menuHook.iLocalizedStringId = 30423;
  PVR->AddMenuHook(&menuHook);

  memset(&menuHook, 0, sizeof(PVR_MENUHOOK));
  menuHook.category = PVR_MENUHOOK_EPG;
  menuHook.iHookId = MENUHOOK_INFO_EPG;
  menuHook.iLocalizedStringId = 30426;
  PVR->AddMenuHook(&menuHook);

  XBMC->Log(LOG_DEBUG, "Creating menu hooks...done");

  // Create our addon
  XBMC->Log(LOG_DEBUG, "Starting the client...");
  g_client = new PVRClientMythTV();
  g_launcher = new PVRClientLauncher(g_client);
  g_bCreated = true;

  if (g_launcher->Start())
  {
    XBMC->Log(LOG_NOTICE, "Addon started successfully");
    return (m_CurStatus = ADDON_STATUS_OK);
  }

  XBMC->Log(LOG_ERROR, "Addon failed to start");
  ADDON_Destroy();
  return (m_CurStatus = ADDON_STATUS_PERMANENT_FAILURE);
}

void ADDON_Destroy()
{
  if (g_bCreated)
  {
    g_bCreated = false;
    SAFE_DELETE(g_launcher);
    SAFE_DELETE(g_client);
    XBMC->Log(LOG_NOTICE, "Addon destroyed.");
    SAFE_DELETE(PVR);
    SAFE_DELETE(XBMC);
    SAFE_DELETE(GUI);
  }
  m_CurStatus = ADDON_STATUS_UNKNOWN;
}

ADDON_STATUS ADDON_GetStatus()
{
  return m_CurStatus;
}

ADDON_STATUS ADDON_SetSetting(const char *settingName, const void *settingValue)
{
  if (!g_bCreated)
    return ADDON_STATUS_OK;

  std::string str = settingName;

  if (str == "host")
  {
    std::string tmp_sHostname;
    XBMC->Log(LOG_INFO, "Changed Setting 'host' from %s to %s", g_szMythHostname.c_str(), (const char*)settingValue);
    tmp_sHostname = g_szMythHostname;
    g_szMythHostname = (const char*)settingValue;
    if (tmp_sHostname != g_szMythHostname)
    {
      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (str == "port")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'port' from %u to %u", g_iProtoPort, *(int*)settingValue);
    if (g_iProtoPort != *(int*)settingValue)
    {
      g_iProtoPort = *(int*)settingValue;
      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (str == "wsport")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'wsport' from %u to %u", g_iWSApiPort, *(int*)settingValue);
    if (g_iWSApiPort != *(int*)settingValue)
    {
      g_iWSApiPort = *(int*)settingValue;
      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (str == "wssecuritypin")
  {
    std::string tmp_sWSSecurityPin;
    XBMC->Log(LOG_INFO, "Changed Setting 'wssecuritypin' from %s to %s", g_szWSSecurityPin.c_str(), (const char*)settingValue);
    tmp_sWSSecurityPin = g_szWSSecurityPin;
    g_szWSSecurityPin = (const char*)settingValue;
    if (tmp_sWSSecurityPin != g_szWSSecurityPin)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (str == "channel_icons")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'channel_icons' from %u to %u", g_bChannelIcons, *(bool*)settingValue);
    if (g_bChannelIcons != *(bool*)settingValue)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (str == "recording_icons")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'recording_icons' from %u to %u", g_bRecordingIcons, *(bool*)settingValue);
    if (g_bRecordingIcons != *(bool*)settingValue)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (str == "backend_bookmarks")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'backend_bookmarks' from %u to %u", g_bUseBackendBookmarks, *(bool*)settingValue);
    if (g_bUseBackendBookmarks != *(bool*)settingValue)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (str == "host_ether")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'host_ether' from %s to %s", g_szMythHostEther.c_str(), (const char*)settingValue);
    g_szMythHostEther = (const char*)settingValue;
  }
  else if (str == "extradebug")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'extra debug' from %u to %u", g_bExtraDebug, *(bool*)settingValue);
    if (g_bExtraDebug != *(bool*)settingValue)
    {
      g_bExtraDebug = *(bool*)settingValue;
      if (g_client)
        g_client->SetDebug();
    }
  }
  else if (str == "livetv")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'livetv' from %u to %u", g_bLiveTV, *(bool*)settingValue);
    if (g_bLiveTV != *(bool*)settingValue)
      g_bLiveTV = *(bool*)settingValue;
  }
  else if (str == "livetv_priority")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'extra debug' from %u to %u", g_bLiveTVPriority, *(bool*)settingValue);
    if (g_bLiveTVPriority != *(bool*) settingValue && m_CurStatus != ADDON_STATUS_LOST_CONNECTION)
    {
      g_bLiveTVPriority = *(bool*)settingValue;
      if (g_client)
        g_client->SetLiveTVPriority(g_bLiveTVPriority);
    }
  }
  else if (str == "rec_template_provider")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'rec_template_provider' from %u to %u", g_iRecTemplateType, *(int*)settingValue);
    if (g_iRecTemplateType != *(int*)settingValue)
      g_iRecTemplateType = *(int*)settingValue;
  }
  else if (str == "rec_autometadata")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'rec_autometadata' from %u to %u", g_bRecAutoMetadata, *(bool*)settingValue);
    if (g_bRecAutoMetadata != *(bool*)settingValue)
      g_bRecAutoMetadata = *(bool*)settingValue;
  }
  else if (str == "rec_autocommflag")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'rec_autocommflag' from %u to %u", g_bRecAutoCommFlag, *(bool*)settingValue);
    if (g_bRecAutoCommFlag != *(bool*)settingValue)
      g_bRecAutoCommFlag = *(bool*)settingValue;
  }
  else if (str == "rec_autotranscode")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'rec_autotranscode' from %u to %u", g_bRecAutoTranscode, *(bool*)settingValue);
    if (g_bRecAutoTranscode != *(bool*)settingValue)
      g_bRecAutoTranscode = *(bool*)settingValue;
  }
  else if (str == "rec_transcoder")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'rec_transcoder' from %u to %u", g_iRecTranscoder, *(int*)settingValue);
    if (g_iRecTranscoder != *(int*)settingValue)
      g_iRecTranscoder = *(int*)settingValue;
  }
  else if (str == "rec_autorunjob1")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'rec_autorunjob1' from %u to %u", g_bRecAutoRunJob1, *(bool*)settingValue);
    if (g_bRecAutoRunJob1 != *(bool*)settingValue)
      g_bRecAutoRunJob1 = *(bool*)settingValue;
  }
  else if (str == "rec_autorunjob2")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'rec_autorunjob2' from %u to %u", g_bRecAutoRunJob2, *(bool*)settingValue);
    if (g_bRecAutoRunJob2 != *(bool*)settingValue)
      g_bRecAutoRunJob2 = *(bool*)settingValue;
  }
  else if (str == "rec_autorunjob3")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'rec_autorunjob3' from %u to %u", g_bRecAutoRunJob3, *(bool*)settingValue);
    if (g_bRecAutoRunJob3 != *(bool*)settingValue)
      g_bRecAutoRunJob3 = *(bool*)settingValue;
  }
  else if (str == "rec_autorunjob4")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'rec_autorunjob4' from %u to %u", g_bRecAutoRunJob4, *(bool*)settingValue);
    if (g_bRecAutoRunJob4 != *(bool*)settingValue)
      g_bRecAutoRunJob4 = *(bool*)settingValue;
  }
  else if (str == "rec_autoexpire")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'rec_autoexpire' from %u to %u", g_bRecAutoExpire, *(bool*)settingValue);
    if (g_bRecAutoExpire != *(bool*)settingValue)
      g_bRecAutoExpire = *(bool*)settingValue;
  }
  else if (str == "tunedelay")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'tunedelay' from %d to %d", g_iTuneDelay, *(int*)settingValue);
    if (g_iTuneDelay != *(int*)settingValue)
      g_iTuneDelay = *(int*)settingValue;
  }
  else if (str == "group_recordings")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'group_recordings' from %u to %u", g_iGroupRecordings, *(int*)settingValue);
    if (g_iGroupRecordings != *(int*)settingValue)
    {
      g_iGroupRecordings = *(int*)settingValue;
      PVR->TriggerRecordingUpdate();
    }
  }
  else if (str == "use_airdate")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'use_airdate' from %u to %u", g_bUseAirdate, *(bool*)settingValue);
    if (g_bUseAirdate != *(bool*)settingValue)
    {
      g_bUseAirdate = *(bool*)settingValue;
      PVR->TriggerRecordingUpdate();
    }
  }
  else if (str == "enable_edl")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'enable_edl' from %u to %u", g_iEnableEDL, *(int*)settingValue);
    if (g_iEnableEDL != *(int*)settingValue)
      g_iEnableEDL = *(int*)settingValue;
  }
  else if (str == "block_shutdown")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'block_shutdown' from %u to %u", g_bBlockMythShutdown, *(bool*)settingValue);
    if (g_bBlockMythShutdown != *(bool*)settingValue)
    {
      g_bBlockMythShutdown = *(bool*)settingValue;
      if (g_client)
        g_bBlockMythShutdown ? g_client->BlockBackendShutdown() : g_client->AllowBackendShutdown();
    }
  }
  else if (str == "limit_tune_attempts")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'limit_tune_attempts' from %u to %u", g_bLimitTuneAttempts, *(bool*)settingValue);
    if (g_bLimitTuneAttempts != *(bool*)settingValue)
      g_bLimitTuneAttempts = *(bool*)settingValue;
  }
  else if (str == "inactive_upcomings")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'inactive_upcomings' from %u to %u", g_bShowNotRecording, *(bool*)settingValue);
    if (g_bShowNotRecording != *(bool*)settingValue)
    {
      g_bShowNotRecording = *(bool*)settingValue;
      if (g_client)
        g_client->HandleScheduleChange();
    }
  }
  else if (str == "prompt_delete")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'prompt_delete' from %u to %u", g_bPromptDeleteAtEnd, *(bool*)settingValue);
    if (g_bPromptDeleteAtEnd != *(bool*)settingValue)
      g_bPromptDeleteAtEnd = *(bool*)settingValue;
  }
  else if (str == "livetv_recordings")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'livetv_recordings' from %u to %u", g_bLiveTVRecordings, *(bool*)settingValue);
    if (g_bLiveTVRecordings != *(bool*)settingValue)
    {
      g_bLiveTVRecordings = *(bool*)settingValue;
      PVR->TriggerRecordingUpdate();
    }
  }
  else if (str == "root_default_group")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'root_default_group' from %u to %u", g_bRootDefaultGroup, *(bool*)settingValue);
    if (g_bRootDefaultGroup != *(bool*)settingValue)
    {
      g_bRootDefaultGroup = *(bool*)settingValue;
      PVR->TriggerRecordingUpdate();
    }
  }
  else if (str == "damaged_color")
  {
    std::string tmp_sDamagedColor = g_szDamagedColor;
    char *buffer = new char [1024];
    const char* v = (const char*)settingValue;
    const char* p = v;
    while (*p != '\0' && *p != ']') ++p;
    std::string s = std::string(v, p - v);
    if (sscanf(s.c_str(), "[COLOR %s", buffer) == 1)
      g_szDamagedColor = buffer;
    else
      g_szDamagedColor = "";
    delete [] buffer;
    XBMC->Log(LOG_INFO, "Changed Setting 'damaged_color' from %s to %s", tmp_sDamagedColor.c_str(), g_szDamagedColor.c_str());
    if (tmp_sDamagedColor != g_szDamagedColor)
    {
      PVR->TriggerRecordingUpdate();
    }
  }
  return ADDON_STATUS_OK;
}

/***********************************************************
 * PVR Client AddOn specific public library functions
 ***********************************************************/

PVR_ERROR GetAddonCapabilities(PVR_ADDON_CAPABILITIES *pCapabilities)
{
  if (g_client != NULL)
  {
    unsigned version = g_client->GetBackendAPIVersion();
    pCapabilities->bSupportsTV                    = g_bLiveTV;
    pCapabilities->bSupportsRadio                 = g_bLiveTV;
    pCapabilities->bSupportsChannelGroups         = true;
    pCapabilities->bSupportsChannelScan           = false;
    pCapabilities->bSupportsEPG                   = true;
    pCapabilities->bSupportsTimers                = true;

    pCapabilities->bHandlesInputStream            = true;
    pCapabilities->bHandlesDemuxing               = false;

    pCapabilities->bSupportsRecordings            = true;
    pCapabilities->bSupportsRecordingsUndelete    = true;
    pCapabilities->bSupportsRecordingPlayCount    = (version < 80 ? false : true);
    pCapabilities->bSupportsLastPlayedPosition    = (version < 88 || !g_bUseBackendBookmarks ? false : true);
    pCapabilities->bSupportsRecordingEdl          = true;
    pCapabilities->bSupportsRecordingsRename      = false;
    pCapabilities->bSupportsRecordingsLifetimeChange = false;
    pCapabilities->bSupportsDescrambleInfo = false;

    return PVR_ERROR_NO_ERROR;
  }
  return PVR_ERROR_FAILED;
}

const char *GetBackendName()
{
  return g_client->GetBackendName();
}

const char *GetBackendVersion()
{
  return g_client->GetBackendVersion();
}

const char *GetConnectionString()
{
  return g_client->GetConnectionString();
}

const char *GetBackendHostname(void)
{
  return g_szMythHostname.c_str();
}

PVR_ERROR GetDriveSpace(long long *iTotal, long long *iUsed)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;

  return g_client->GetDriveSpace(iTotal, iUsed);
}

PVR_ERROR OpenDialogChannelScan()
{
  return PVR_ERROR_FAILED;
}

PVR_ERROR CallMenuHook(const PVR_MENUHOOK &menuhook, const PVR_MENUHOOK_DATA &item)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;

  return g_client->CallMenuHook(menuhook, item);
}

void OnSystemSleep()
{
  XBMC->Log(LOG_INFO, "Received event: %s", __FUNCTION__);
  if (g_client)
    g_client->OnSleep();
}

void OnSystemWake()
{
  XBMC->Log(LOG_INFO, "Received event: %s", __FUNCTION__);
  if (g_client)
    g_client->OnWake();
}

void OnPowerSavingActivated()
{
  XBMC->Log(LOG_INFO, "Received event: %s", __FUNCTION__);
  if (g_client)
    g_client->OnDeactivatedGUI();
}

void OnPowerSavingDeactivated()
{
  XBMC->Log(LOG_INFO, "Received event: %s", __FUNCTION__);
  if (g_client)
    g_client->OnActivatedGUI();
}

/*
 * PVR EPG Functions
 */

PVR_ERROR GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;

  return g_client->GetEPGForChannel(handle, channel, iStart, iEnd);
}

/*
 * PVR Channel Functions
 */

int GetChannelsAmount()
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;

  return g_client->GetNumChannels();
}

PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;

  return g_client->GetChannels(handle, bRadio);
}

PVR_ERROR DeleteChannel(const PVR_CHANNEL &channel)
{
  (void)channel;
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR RenameChannel(const PVR_CHANNEL &channel)
{
  (void)channel;
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR OpenDialogChannelSettings(const PVR_CHANNEL &channel)
{
  (void)channel;
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR OpenDialogChannelAdd(const PVR_CHANNEL &channel)
{
  (void)channel;
  return PVR_ERROR_NOT_IMPLEMENTED;
}

int GetChannelGroupsAmount(void)
{
  if (g_client == NULL)
    return -1;

  return g_client->GetChannelGroupsAmount();
}

PVR_ERROR GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;

  return g_client->GetChannelGroups(handle, bRadio);
}

PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group){
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;

  return g_client->GetChannelGroupMembers(handle, group);
}


/*
 * PVR Recording Functions
 */

int GetRecordingsAmount(bool deleted)
{
  if (g_client == NULL)
    return 0;
  if (deleted)
    return g_client->GetDeletedRecordingsAmount();
  return g_client->GetRecordingsAmount();
}

PVR_ERROR GetRecordings(ADDON_HANDLE handle, bool deleted)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;
  if (deleted)
    return g_client->GetDeletedRecordings(handle);
  return g_client->GetRecordings(handle);
}

PVR_ERROR DeleteRecording(const PVR_RECORDING &recording)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;

  return g_client->DeleteRecording(recording);
}

PVR_ERROR RenameRecording(const PVR_RECORDING &recording)
{
  (void)recording;
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR SetRecordingPlayCount(const PVR_RECORDING &recording, int count)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;
  return g_client->SetRecordingPlayCount(recording, count);
}

PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;
  return g_client->SetRecordingLastPlayedPosition(recording, lastplayedposition);
}

int GetRecordingLastPlayedPosition(const PVR_RECORDING &recording)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;
  return g_client->GetRecordingLastPlayedPosition(recording);
}

PVR_ERROR GetRecordingEdl(const PVR_RECORDING &recording, PVR_EDL_ENTRY entries[], int *size)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;
  return g_client->GetRecordingEdl(recording, entries, size);
}

PVR_ERROR UndeleteRecording(const PVR_RECORDING& recording)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;
  return g_client->UndeleteRecording(recording);
}

PVR_ERROR DeleteAllRecordingsFromTrash()
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;
  return g_client->PurgeDeletedRecordings();
}

/*
 * PVR Timer Functions
 */

PVR_ERROR GetTimerTypes(PVR_TIMER_TYPE types[], int *size)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;
  return g_client->GetTimerTypes(types, size);
}

int GetTimersAmount(void)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;

  return g_client->GetTimersAmount();
}

PVR_ERROR GetTimers(ADDON_HANDLE handle)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;

  return g_client->GetTimers(handle);
}

PVR_ERROR AddTimer(const PVR_TIMER &timer)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;

  return g_client->AddTimer(timer);
}

PVR_ERROR DeleteTimer(const PVR_TIMER &timer, bool bForceDelete)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;

  return g_client->DeleteTimer(timer, bForceDelete);
}

PVR_ERROR UpdateTimer(const PVR_TIMER &timer)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;

  return g_client->UpdateTimer(timer);
}


/*
 * PVR Live Stream Functions
 */

bool OpenLiveStream(const PVR_CHANNEL &channel)
{
  if (g_client == NULL)
    return false;

  return g_client->OpenLiveStream(channel);
}

void CloseLiveStream(void)
{
  if (g_client == NULL)
    return;

  g_client->CloseLiveStream();
}

int ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  if (g_client == NULL)
    return -1;

  int dataread = g_client->ReadLiveStream(pBuffer, iBufferSize);
  if (dataread < 0)
  {
    XBMC->Log(LOG_ERROR,"%s: Failed to read liveStream. Errorcode: %d!", __FUNCTION__, dataread);
    dataread = 0;
  }
  return dataread;
}

PVR_ERROR SignalStatus(PVR_SIGNAL_STATUS &signalStatus)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;

  return g_client->SignalStatus(signalStatus);
}

void PauseStream(bool bPaused)
{
  (void)bPaused;
}

bool CanPauseStream(void)
{
  return true;
}

bool CanSeekStream(void)
{
  return true;
}

long long SeekLiveStream(long long iPosition, int iWhence)
{
  if (g_client == NULL)
    return -1;

  return g_client->SeekLiveStream(iPosition,iWhence);
}

long long LengthLiveStream(void)
{
  if (g_client == NULL)
    return -1;

  return g_client->LengthLiveStream();
}

bool IsRealTimeStream(void)
{
  if (g_client == NULL)
    return false;

  return g_client->IsRealTimeStream();
}

/*
 * PVR Recording Stream Functions
 */

bool OpenRecordedStream(const PVR_RECORDING &recinfo)
{
  if (g_client == NULL)
    return false;

  return g_client->OpenRecordedStream(recinfo);
}

void CloseRecordedStream(void)
{
  if (g_client == NULL)
    return;

  g_client->CloseRecordedStream();
}

int ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  if (g_client == NULL)
    return -1;

  return g_client->ReadRecordedStream(pBuffer, iBufferSize);
}

long long SeekRecordedStream(long long iPosition, int iWhence)
{
  if (g_client == NULL)
    return -1;
  if (iWhence == SEEK_POSSIBLE)
    return 1;
  return g_client->SeekRecordedStream(iPosition, iWhence);
}

long long LengthRecordedStream(void)
{
  if (g_client == NULL)
    return -1;

  return g_client->LengthRecordedStream();
}


/*
 * PVR Timeshift Functions
 */

bool IsTimeshifting(void) { return true; }

/*
 * Unused API Functions
 */

PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES *pStreamTimes)
{
  if (g_client == NULL)
    return PVR_ERROR_SERVER_ERROR;

  return g_client->GetStreamTimes(pStreamTimes);
}

PVR_ERROR GetStreamProperties(PVR_STREAM_PROPERTIES *) { return PVR_ERROR_NOT_IMPLEMENTED; }
void DemuxAbort(void) {}
DemuxPacket* DemuxRead(void) { return NULL; }
void DemuxFlush(void) {}
bool SeekTime(double, bool, double *) { return false; }
void DemuxReset() {}
void SetSpeed(int) {};
PVR_ERROR SetEPGTimeFrame(int) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetDescrambleInfo(PVR_DESCRAMBLE_INFO*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingLifetime(const PVR_RECORDING*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetChannelStreamProperties(const PVR_CHANNEL*, PVR_NAMED_VALUE*, unsigned int*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetRecordingStreamProperties(const PVR_RECORDING*, PVR_NAMED_VALUE*, unsigned int*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR IsEPGTagRecordable(const EPG_TAG*, bool*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR IsEPGTagPlayable(const EPG_TAG*, bool*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetEPGTagStreamProperties(const EPG_TAG*, PVR_NAMED_VALUE*, unsigned int*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetEPGTagEdl(const EPG_TAG* epgTag, PVR_EDL_ENTRY edl[], int *size) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetStreamReadChunkSize(int* chunksize) { return PVR_ERROR_NOT_IMPLEMENTED; }

} //end extern "C"
