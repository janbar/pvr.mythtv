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

#include "pvrclient-mythtv.h"
#include "client.h"
#include "tools.h"
#include "avinfo.h"
#include "filestreaming.h"
#include "taskhandler.h"
#include "private/os/threads/mutex.h"

#include <time.h>
#include <set>
#include <cassert>

using namespace ADDON;

PVRClientMythTV::PVRClientMythTV()
: m_connectionError(CONN_ERROR_NOT_CONNECTED)
, m_eventHandler(NULL)
, m_control(NULL)
, m_liveStream(NULL)
, m_recordingStream(NULL)
, m_dummyStream(NULL)
, m_hang(false)
, m_powerSaving(false)
, m_stopTV(false)
, m_artworksManager(NULL)
, m_scheduleManager(NULL)
, m_lock(new Myth::OS::CMutex)
, m_todo(NULL)
, m_channelsLock(new Myth::OS::CMutex)
, m_recordingsLock(new Myth::OS::CMutex)
, m_recordingChangePinCount(0)
, m_recordingsAmountChange(false)
, m_recordingsAmount(0)
, m_deletedRecAmountChange(false)
, m_deletedRecAmount(0)
{
}

PVRClientMythTV::~PVRClientMythTV()
{
  SAFE_DELETE(m_todo);
  SAFE_DELETE(m_dummyStream);
  SAFE_DELETE(m_liveStream);
  SAFE_DELETE(m_recordingStream);
  SAFE_DELETE(m_artworksManager);
  SAFE_DELETE(m_scheduleManager);
  SAFE_DELETE(m_eventHandler);
  SAFE_DELETE(m_control);
  delete m_recordingsLock;
  delete m_channelsLock;
  delete m_lock;
}

static void Log(int level, char *msg)
{
  if (msg && level != MYTH_DBG_NONE)
  {
    bool doLog = true; //g_bExtraDebug;
    addon_log_t loglevel = LOG_DEBUG;
    switch (level)
    {
    case MYTH_DBG_ERROR:
      loglevel = LOG_ERROR;
      doLog = true;
      break;
    case MYTH_DBG_WARN:
      loglevel = LOG_NOTICE;
      doLog = true;
      break;
    case MYTH_DBG_INFO:
      loglevel = LOG_INFO;
      doLog = true;
      break;
    case MYTH_DBG_DEBUG:
    case MYTH_DBG_PROTO:
    case MYTH_DBG_ALL:
      loglevel = LOG_DEBUG;
      break;
    }
    if (XBMC && doLog)
      XBMC->Log(loglevel, "%s", msg);
  }
}

void PVRClientMythTV::SetDebug(bool silent /*= false*/)
{
  // Setup libcppmyth logging
  if (g_bExtraDebug)
    Myth::DBGAll();
  else if (silent)
    Myth::DBGLevel(MYTH_DBG_NONE);
  else
    Myth::DBGLevel(MYTH_DBG_ERROR);
  Myth::SetDBGMsgCallback(Log);
}

bool PVRClientMythTV::Connect()
{
  assert(m_control == NULL);

  SetDebug(true);
  Myth::Control *control = new Myth::Control(g_szMythHostname, g_iProtoPort, g_iWSApiPort, g_szWSSecurityPin, true);
  if (!control->IsOpen())
  {
    switch(control->GetProtoError())
    {
      case Myth::ProtoBase::ERROR_UNKNOWN_VERSION:
        m_connectionError = CONN_ERROR_UNKNOWN_VERSION;
        break;
      default:
        m_connectionError = CONN_ERROR_SERVER_UNREACHABLE;
    }
    delete control;
    XBMC->Log(LOG_NOTICE, "Failed to connect to MythTV backend on %s:%d", g_szMythHostname.c_str(), g_iProtoPort);
    // Try wake up for the next attempt
    if (!g_szMythHostEther.empty())
      XBMC->WakeOnLan(g_szMythHostEther.c_str());
    return false;
  }
  if (!control->CheckService())
  {
    m_connectionError = CONN_ERROR_API_UNAVAILABLE;
    delete control;
    XBMC->Log(LOG_NOTICE,"Failed to connect to MythTV backend on %s:%d with pin %s", g_szMythHostname.c_str(), g_iWSApiPort, g_szWSSecurityPin.c_str());
    return false;
  }
  m_connectionError = CONN_ERROR_NO_ERROR;
  m_control = control;
  SetDebug(false);

  // Create event handler and subscription as needed
  unsigned subid = 0;
  m_eventHandler = new Myth::EventHandler(g_szMythHostname, g_iProtoPort);
  subid = m_eventHandler->CreateSubscription(this);
  m_eventHandler->SubscribeForEvent(subid, Myth::EVENT_HANDLER_STATUS);
  m_eventHandler->SubscribeForEvent(subid, Myth::EVENT_HANDLER_TIMER);
  m_eventHandler->SubscribeForEvent(subid, Myth::EVENT_ASK_RECORDING);
  m_eventHandler->SubscribeForEvent(subid, Myth::EVENT_RECORDING_LIST_CHANGE);

  // Create schedule manager and new subscription handled by dedicated thread
  m_scheduleManager = new MythScheduleManager(g_szMythHostname, g_iProtoPort, g_iWSApiPort, g_szWSSecurityPin);
  subid = m_eventHandler->CreateSubscription(this);
  m_eventHandler->SubscribeForEvent(subid, Myth::EVENT_SCHEDULE_CHANGE);

  // Create artwork manager
  m_artworksManager = new ArtworkManager(g_szMythHostname, g_iWSApiPort, g_szWSSecurityPin);

  // Create the task handler to process various task
  m_todo = new TaskHandler();

  // Now all is ready: Start event handler
  m_eventHandler->Start();
  return true;
}

PVRClientMythTV::CONN_ERROR PVRClientMythTV::GetConnectionError() const
{
  return m_connectionError;
}

unsigned PVRClientMythTV::GetBackendAPIVersion()
{
  if (m_control)
    return m_control->CheckService();
  return 0;
}

const char *PVRClientMythTV::GetBackendName()
{
  static std::string myName;
  myName.clear();
  if (m_control)
    myName.append("MythTV (").append(m_control->GetServerHostName()).append(")");
  XBMC->Log(LOG_DEBUG, "%s: %s", __FUNCTION__, myName.c_str());
  return myName.c_str();
}

const char *PVRClientMythTV::GetBackendVersion()
{
  static std::string myVersion;
  myVersion.clear();
  if (m_control)
  {
    Myth::VersionPtr version = m_control->GetVersion();
    myVersion = version->version;
  }
  XBMC->Log(LOG_DEBUG, "%s: %s", __FUNCTION__, myVersion.c_str());
  return myVersion.c_str();
}

const char *PVRClientMythTV::GetConnectionString()
{
  static std::string myConnectionString;
  myConnectionString.clear();
  myConnectionString.append("http://").append(g_szMythHostname).append(":").append(Myth::IntToString(g_iWSApiPort));
  XBMC->Log(LOG_DEBUG, "%s: %s", __FUNCTION__, myConnectionString.c_str());
  return myConnectionString.c_str();
}

PVR_ERROR PVRClientMythTV::GetDriveSpace(long long *iTotal, long long *iUsed)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  int64_t total = 0, used = 0;
  if (m_control->QueryFreeSpaceSummary(&total, &used))
  {
    *iTotal = (long long)total;
    *iUsed = (long long)used;
    return PVR_ERROR_NO_ERROR;
  }
  return PVR_ERROR_UNKNOWN;
}

void PVRClientMythTV::OnSleep()
{
  if (m_eventHandler)
    m_eventHandler->Stop();
  if (m_scheduleManager)
    m_scheduleManager->CloseControl();
  if (m_control)
    m_control->Close();
}

void PVRClientMythTV::OnWake()
{
  if (m_control)
    m_control->Open();
  if (m_scheduleManager)
    m_scheduleManager->OpenControl();
  if (m_eventHandler)
    m_eventHandler->Start();
}

void PVRClientMythTV::OnDeactivatedGUI()
{
  if (g_bAllowMythShutdown && m_control && m_control->IsOpen())
    AllowBackendShutdown();
  m_powerSaving = true;
}

void PVRClientMythTV::OnActivatedGUI()
{
  // block shutdown if backend is connected
  if (g_bAllowMythShutdown && m_control && m_control->IsOpen())
    BlockBackendShutdown();
  m_powerSaving = false;
}

void PVRClientMythTV::HandleBackendMessage(Myth::EventMessagePtr msg)
{
  switch (msg->event)
  {
    case Myth::EVENT_SCHEDULE_CHANGE:
      HandleScheduleChange();
      break;
    case Myth::EVENT_ASK_RECORDING:
      HandleAskRecording(*msg);
      break;
    case Myth::EVENT_RECORDING_LIST_CHANGE:
      HandleRecordingListChange(*msg);
      break;
    case Myth::EVENT_HANDLER_TIMER:
      RunHouseKeeping();
      break;
    case Myth::EVENT_HANDLER_STATUS:
      if (msg->subject[0] == EVENTHANDLER_DISCONNECTED)
      {
        m_hang = true;
        if (m_control)
          m_control->Close();
        if (m_scheduleManager)
          m_scheduleManager->CloseControl();
        XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30302)); // Connection to MythTV backend lost
      }
      else if (msg->subject[0] == EVENTHANDLER_CONNECTED)
      {
        if (m_hang)
        {
          if (m_control)
            m_control->Open();
          if (m_scheduleManager)
            m_scheduleManager->OpenControl();
          m_hang = false;
          XBMC->QueueNotification(QUEUE_INFO, XBMC->GetLocalizedString(30303)); // Connection to MythTV restored
          // still in mode power saving I have to allow shutdown again
          if (m_powerSaving && g_bAllowMythShutdown)
            AllowBackendShutdown();
        }
        // Refreshing all
        HandleChannelChange();
        HandleScheduleChange();
        HandleRecordingListChange(Myth::EventMessage());
      }
      else if (msg->subject[0] == EVENTHANDLER_NOTCONNECTED)
      {
        // Try wake up if GUI is activated
        if (!m_powerSaving && !g_szMythHostEther.empty())
          XBMC->WakeOnLan(g_szMythHostEther.c_str());
      }
      break;
    default:
      break;
  }
}

void PVRClientMythTV::HandleChannelChange()
{
  FillChannelsAndChannelGroups();
  PVR->TriggerChannelUpdate();
  PVR->TriggerChannelGroupsUpdate();
}

void PVRClientMythTV::HandleScheduleChange()
{
  if (!m_scheduleManager)
    return;
  m_scheduleManager->Update();
  PVR->TriggerTimerUpdate();
}

void PVRClientMythTV::HandleAskRecording(const Myth::EventMessage& msg)
{
  if (!m_control)
    return;
  // ASK_RECORDING <card id> <time until> <has rec> <has later>[]:[]<program info>
  // Example: ASK_RECORDING 9 29 0 1[]:[]<program>
  if (msg.subject.size() < 5)
  {
    for (unsigned i = 0; i < msg.subject.size(); ++i)
      XBMC->Log(LOG_ERROR, "%s: Incorrect message: %d : %s", __FUNCTION__, i, msg.subject[i].c_str());
    return;
  }
  // The scheduled recording will hang in MythTV if ASK_RECORDING is just ignored.
  // - Stop recorder (and blocked for time until seconds)
  // - Skip the recording by sending CANCEL_NEXT_RECORDING(true)
  uint32_t cardid = Myth::StringToId(msg.subject[1]);
  int timeuntil = Myth::StringToInt(msg.subject[2]);
  int hasrec = Myth::StringToInt(msg.subject[3]);
  int haslater = Myth::StringToInt(msg.subject[4]);
  XBMC->Log(LOG_NOTICE, "%s: Event ASK_RECORDING: rec=%d timeuntil=%d hasrec=%d haslater=%d", __FUNCTION__,
          cardid, timeuntil, hasrec, haslater);

  std::string title;
  if (msg.program)
    title = msg.program->title;
  XBMC->Log(LOG_NOTICE, "%s: Event ASK_RECORDING: title=%s", __FUNCTION__, title.c_str());

  if (timeuntil >= 0 && cardid > 0 && m_liveStream && m_liveStream->GetCardId() == cardid)
  {
    if (g_iLiveTVConflictStrategy == LIVETV_CONFLICT_STRATEGY_CANCELREC ||
      (g_iLiveTVConflictStrategy == LIVETV_CONFLICT_STRATEGY_HASLATER && haslater))
    {
      XBMC->QueueNotification(QUEUE_WARNING, XBMC->GetLocalizedString(30307), title.c_str()); // Canceling conflicting recording: %s
      m_control->CancelNextRecording((int)cardid, true);
    }
    else // LIVETV_CONFLICT_STRATEGY_STOPTV
    {
      XBMC->QueueNotification(QUEUE_WARNING, XBMC->GetLocalizedString(30308), title.c_str()); // Stopping Live TV due to conflicting recording: %s
      m_stopTV = true; // that will close live stream as soon as possible
    }
  }
}

void PVRClientMythTV::HandleRecordingListChange(const Myth::EventMessage& msg)
{
  if (!m_control)
    return;
  unsigned cs = (unsigned)msg.subject.size();
  if (cs <= 1)
  {
    if (g_bExtraDebug)
      XBMC->Log(LOG_DEBUG, "%s: Reload all recordings", __FUNCTION__);
    Myth::OS::CLockGuard lock(*m_recordingsLock);
    FillRecordings();
    ++m_recordingChangePinCount;
  }
  else if (cs == 4 && msg.subject[1] == "ADD")
  {
    uint32_t chanid = Myth::StringToId(msg.subject[2]);
    time_t startts = Myth::StringToTime(msg.subject[3]);
    MythProgramInfo prog(m_control->GetRecorded(chanid, startts));
    if (!prog.IsNull())
    {
      Myth::OS::CLockGuard lock(*m_recordingsLock);
      ProgramInfoMap::iterator it = m_recordings.find(prog.UID());
      if (it == m_recordings.end())
      {
        if (g_bExtraDebug)
          XBMC->Log(LOG_DEBUG, "%s: Add recording: %s", __FUNCTION__, prog.UID().c_str());
        // Add recording
        m_recordings.insert(std::pair<std::string, MythProgramInfo>(prog.UID().c_str(), prog));
        ++m_recordingChangePinCount;
      }
    }
    else
      XBMC->Log(LOG_ERROR, "%s: Add recording failed for %u %ld", __FUNCTION__, (unsigned)chanid, (long)startts);
  }
  else if (cs == 3 && msg.subject[1] == "ADD")
  {
    uint32_t recordedid = Myth::StringToId(msg.subject[2]);
    MythProgramInfo prog(m_control->GetRecorded(recordedid));
    if (!prog.IsNull())
    {
      Myth::OS::CLockGuard lock(*m_recordingsLock);
      ProgramInfoMap::iterator it = m_recordings.find(prog.UID());
      if (it == m_recordings.end())
      {
        if (g_bExtraDebug)
          XBMC->Log(LOG_DEBUG, "%s: Add recording: %s", __FUNCTION__, prog.UID().c_str());
        // Add recording
        m_recordings.insert(std::pair<std::string, MythProgramInfo>(prog.UID().c_str(), prog));
        ++m_recordingChangePinCount;
      }
    }
    else
      XBMC->Log(LOG_ERROR, "%s: Add recording failed for %u", __FUNCTION__, (unsigned)recordedid);
  }
  else if (cs == 2 && msg.subject[1] == "UPDATE" && msg.program)
  {
    Myth::OS::CLockGuard lock(*m_recordingsLock);
    MythProgramInfo prog(msg.program);
    ProgramInfoMap::iterator it = m_recordings.find(prog.UID());
    if (it != m_recordings.end())
    {
      if (g_bExtraDebug)
        XBMC->Log(LOG_DEBUG, "%s: Update recording: %s", __FUNCTION__, prog.UID().c_str());
      if (m_control->RefreshRecordedArtwork(*(msg.program)) && g_bExtraDebug)
        XBMC->Log(LOG_DEBUG, "%s: artwork found for %s", __FUNCTION__, prog.UID().c_str());
      // Reset to recalculate flags
      prog.ResetProps();
      // Keep props
      prog.CopyProps(it->second);
      // Keep original air date
      prog.GetPtr()->airdate = it->second.Airdate();
      // Update recording
      it->second = prog;
      ++m_recordingChangePinCount;
    }
  }
  else if (cs == 4 && msg.subject[1] == "DELETE")
  {
    // MythTV send two DELETE events. First requests deletion, second confirms deletion.
    // On first we delete recording. On second program will not be found.
    uint32_t chanid = Myth::StringToId(msg.subject[2]);
    time_t startts = Myth::StringToTime(msg.subject[3]);
    MythProgramInfo prog(m_control->GetRecorded(chanid, startts));
    if (!prog.IsNull())
    {
      Myth::OS::CLockGuard lock(*m_recordingsLock);
      ProgramInfoMap::iterator it = m_recordings.find(prog.UID());
      if (it != m_recordings.end())
      {
        if (g_bExtraDebug)
          XBMC->Log(LOG_DEBUG, "%s: Delete recording: %s", __FUNCTION__, prog.UID().c_str());
        // Remove recording
        m_recordings.erase(it);
        ++m_recordingChangePinCount;
      }
    }
  }
  else if (cs == 3 && msg.subject[1] == "DELETE")
  {
    // MythTV send two DELETE events. First requests deletion, second confirms deletion.
    // On first we delete recording. On second program will not be found.
    uint32_t recordedid = Myth::StringToId(msg.subject[2]);
    MythProgramInfo prog(m_control->GetRecorded(recordedid));
    if (!prog.IsNull())
    {
      Myth::OS::CLockGuard lock(*m_recordingsLock);
      ProgramInfoMap::iterator it = m_recordings.find(prog.UID());
      if (it != m_recordings.end())
      {
        if (g_bExtraDebug)
          XBMC->Log(LOG_DEBUG, "%s: Delete recording: %s", __FUNCTION__, prog.UID().c_str());
        // Remove recording
        m_recordings.erase(it);
        ++m_recordingChangePinCount;
      }
    }
  }
}

void PVRClientMythTV::PromptDeleteRecording(const MythProgramInfo &prog)
{
  if (IsPlaying() || prog.IsNull())
    return;
  std::string dispTitle = MakeProgramTitle(prog.Title(), prog.Subtitle());
  if (GUI->Dialog_YesNo_ShowAndGetInput(XBMC->GetLocalizedString(122),
          XBMC->GetLocalizedString(19112), "", dispTitle.c_str(),
          "", XBMC->GetLocalizedString(117)))
  {
    if (m_control->DeleteRecording(*(prog.GetPtr())))
      XBMC->Log(LOG_DEBUG, "%s: Deleted recording %s", __FUNCTION__, prog.UID().c_str());
    else
      XBMC->Log(LOG_ERROR, "%s: Failed to delete recording %s", __FUNCTION__, prog.UID().c_str());
  }
}

void PVRClientMythTV::RunHouseKeeping()
{
  if (!m_control || !m_eventHandler)
    return;
  // It is time to work
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  // Reconnect handler when backend connection has hanging during last period
  if (!m_hang && m_control->HasHanging())
  {
    XBMC->Log(LOG_NOTICE, "%s: Ask to refresh handler connection since control connection has hanging", __FUNCTION__);
    m_eventHandler->Reset();
    m_control->CleanHanging();
  }
  if (m_recordingChangePinCount)
  {
    Myth::OS::CLockGuard lock(*m_recordingsLock);
    m_recordingsAmountChange = true; // Need count recording amount
    m_deletedRecAmountChange = true; // Need count of deleted amount
    lock.Unlock();
    PVR->TriggerRecordingUpdate();
    lock.Lock();
    m_recordingChangePinCount = 0;
  }
}

PVR_ERROR PVRClientMythTV::GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG,"%s: start: %ld, end: %ld, chanid: %u", __FUNCTION__, (long)iStart, (long)iEnd, channel.iUniqueId);

  if (!channel.bIsHidden)
  {
    Myth::ProgramMapPtr EPG = m_control->GetProgramGuide(channel.iUniqueId, iStart, iEnd);
    // Transfer EPG for the given channel
    for (Myth::ProgramMap::reverse_iterator it = EPG->rbegin(); it != EPG->rend(); ++it)
    {
      EPG_TAG tag;
      memset(&tag, 0, sizeof(EPG_TAG));
      tag.startTime = it->first;
      tag.endTime = it->second->endTime;
      // Reject bad entry
      if (tag.endTime <= tag.startTime)
        continue;

      // EPG_TAG expects strings as char* and not as copies (like the other PVR types).
      // Therefore we have to make sure that we don't pass invalid (freed) memory to TransferEpgEntry.
      // In particular we have to use local variables and must not pass returned string values directly.
      tag.strTitle = it->second->title.c_str();
      tag.strPlot = it->second->description.c_str();
      tag.strGenreDescription = it->second->category.c_str();
      tag.iUniqueBroadcastId = MythEPGInfo::MakeBroadcastID(it->second->channel.chanId, it->first);
      tag.iUniqueChannelId = channel.iUniqueId;
      int genre = m_categories.Category(it->second->category);
      tag.iGenreSubType = genre & 0x0F;
      tag.iGenreType = genre & 0xF0;
      tag.strEpisodeName = it->second->subTitle.c_str();
      tag.strIconPath = "";
      tag.strPlotOutline = "";
      tag.bNotify = false;
      tag.firstAired = it->second->airdate;
      tag.iEpisodeNumber = (int)it->second->episode;
      tag.iEpisodePartNumber = 0;
      tag.iParentalRating = 0;
      tag.iSeriesNumber = (int)it->second->season;
      tag.iStarRating = atoi(it->second->stars.c_str());
      tag.strOriginalTitle = "";
      tag.strCast = "";
      tag.strDirector = "";
      tag.strWriter = "";
      tag.iYear = 0;
      tag.strIMDBNumber = it->second->inetref.c_str();
      if (!it->second->seriesId.empty())
        tag.iFlags = EPG_TAG_FLAG_IS_SERIES;
      else
        tag.iFlags = EPG_TAG_FLAG_UNDEFINED;

      PVR->TransferEpgEntry(handle, &tag);
    }
  }

  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: Done", __FUNCTION__);

  return PVR_ERROR_NO_ERROR;
}

int PVRClientMythTV::GetNumChannels()
{
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_channelsLock);
  return m_PVRChannels.size();
}

PVR_ERROR PVRClientMythTV::GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: radio: %s", __FUNCTION__, (bRadio ? "true" : "false"));

  Myth::OS::CLockGuard lock(*m_channelsLock);

  // Load channels list
  if (m_PVRChannels.empty())
    FillChannelsAndChannelGroups();
  // Transfer channels of the requested type (radio / tv)
  for (PVRChannelList::const_iterator it = m_PVRChannels.begin(); it != m_PVRChannels.end(); ++it)
  {
    if (it->bIsRadio == bRadio)
    {
      ChannelIdMap::const_iterator itm = m_channelsById.find(it->iUniqueId);
      if (itm != m_channelsById.end() && !itm->second.IsNull())
      {
        PVR_CHANNEL tag;
        memset(&tag, 0, sizeof(PVR_CHANNEL));

        tag.iUniqueId = itm->first;
        tag.iChannelNumber = itm->second.NumberMajor();
        tag.iSubChannelNumber = itm->second.NumberMinor();
        PVR_STRCPY(tag.strChannelName, itm->second.Name().c_str());
        tag.bIsHidden = !itm->second.Visible();
        tag.bIsRadio = itm->second.IsRadio();

        if (m_artworksManager)
          PVR_STRCPY(tag.strIconPath, m_artworksManager->GetChannelIconPath(itm->second).c_str());
        else
          PVR_STRCPY(tag.strIconPath, "");

        // Unimplemented
        PVR_STRCPY(tag.strInputFormat, "");
        tag.iEncryptionSystem = 0;

        PVR->TransferChannelEntry(handle, &tag);
      }
    }
  }

  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: Done", __FUNCTION__);

  return PVR_ERROR_NO_ERROR;
}

int PVRClientMythTV::GetChannelGroupsAmount()
{
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_channelsLock);
  return m_PVRChannelGroups.size();
}

PVR_ERROR PVRClientMythTV::GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: radio: %s", __FUNCTION__, (bRadio ? "true" : "false"));

  Myth::OS::CLockGuard lock(*m_channelsLock);

  // Transfer channel groups of the given type (radio / tv)
  for (PVRChannelGroupMap::iterator itg = m_PVRChannelGroups.begin(); itg != m_PVRChannelGroups.end(); ++itg)
  {
    PVR_CHANNEL_GROUP tag;
    memset(&tag, 0, sizeof(PVR_CHANNEL_GROUP));

    PVR_STRCPY(tag.strGroupName, itg->first.c_str());
    tag.bIsRadio = bRadio;
    tag.iPosition = 0;

    // Only add the group if we have at least one channel of the correct type
    for (PVRChannelList::const_iterator itc = itg->second.begin(); itc != itg->second.end(); ++itc)
    {
      if (itc->bIsRadio == bRadio)
      {
        PVR->TransferChannelGroup(handle, &tag);
        break;
      }
    }
  }

  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: Done", __FUNCTION__);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: group: %s", __FUNCTION__, group.strGroupName);

  Myth::OS::CLockGuard lock(*m_channelsLock);

  PVRChannelGroupMap::iterator itg = m_PVRChannelGroups.find(group.strGroupName);
  if (itg == m_PVRChannelGroups.end())
  {
    XBMC->Log(LOG_ERROR,"%s: Channel group not found", __FUNCTION__);
    return PVR_ERROR_INVALID_PARAMETERS;
  }

  // Transfer the channel group members for the requested group
  for (PVRChannelList::const_iterator itc = itg->second.begin(); itc != itg->second.end(); ++itc)
  {
    if (itc->bIsRadio == group.bIsRadio)
    {
      PVR_CHANNEL_GROUP_MEMBER tag;
      memset(&tag, 0, sizeof(PVR_CHANNEL_GROUP_MEMBER));
      tag.iChannelNumber = itc->iChannelNumber;
      tag.iSubChannelNumber = itc->iSubChannelNumber;
      tag.iChannelUniqueId = itc->iUniqueId;
      PVR_STRCPY(tag.strGroupName, group.strGroupName);
      PVR->TransferChannelGroupMember(handle, &tag);
    }
  }

  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: Done", __FUNCTION__);

  return PVR_ERROR_NO_ERROR;
}

bool PVRClientMythTV::IsPlaying() const
{
  Myth::OS::CLockGuard lock(*m_lock);
  if (m_liveStream || m_dummyStream || m_recordingStream)
    return true;
  return false;
}

int PVRClientMythTV::FillChannelsAndChannelGroups()
{
  if (!m_control)
    return 0;
  int count = 0;
  XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_channelsLock);
  m_PVRChannels.clear();
  m_PVRChannelGroups.clear();
  m_PVRChannelUidById.clear();
  m_channelsById.clear();

  // Create a channels map to merge channels with same channum and callsign within
  typedef std::pair<std::string, std::string> chanuid_t;
  typedef std::map<chanuid_t, PVRChannelItem> mapuid_t;
  mapuid_t channelIdentifiers;

  // For each source create a channels group
  Myth::VideoSourceListPtr sources = m_control->GetVideoSourceList();
  for (Myth::VideoSourceList::iterator its = sources->begin(); its != sources->end(); ++its)
  {
    Myth::ChannelListPtr channels = m_control->GetChannelList((*its)->sourceId);
    std::set<PVRChannelItem> channelIDs;
    //channelIdentifiers.clear();
    for (Myth::ChannelList::iterator itc = channels->begin(); itc != channels->end(); ++itc)
    {
      MythChannel channel((*itc));
      unsigned int chanid = channel.ID();
      PVRChannelItem item;
      item.iUniqueId = chanid;
      item.iChannelNumber = channel.NumberMajor();
      item.iSubChannelNumber = channel.NumberMinor();
      item.bIsRadio = channel.IsRadio();
      // Store the new Myth channel in the map
      m_channelsById.insert(std::make_pair(item.iUniqueId, channel));

      // Looking for PVR channel with same channum and callsign
      chanuid_t channelIdentifier = std::make_pair(channel.Number(), channel.Callsign());
      mapuid_t::iterator itm = channelIdentifiers.find(channelIdentifier);
      if (itm != channelIdentifiers.end())
      {
        if (g_bExtraDebug)
          XBMC->Log(LOG_DEBUG, "%s: skipping channel: %d", __FUNCTION__, chanid);
        // Link channel to PVR item
        m_PVRChannelUidById.insert(std::make_pair(chanid, itm->second.iUniqueId));
        // Add found PVR item to the grouping set
        channelIDs.insert(itm->second);
      }
      else
      {
        ++count;
        m_PVRChannels.push_back(item);
        channelIdentifiers.insert(std::make_pair(channelIdentifier, item));
        // Link channel to PVR item
        m_PVRChannelUidById.insert(std::make_pair(chanid, item.iUniqueId));
        // Add the new PVR item to the grouping set
        channelIDs.insert(item);
      }
    }
    m_PVRChannelGroups.insert(std::make_pair((*its)->sourceName, PVRChannelList(channelIDs.begin(), channelIDs.end())));
  }

  XBMC->Log(LOG_DEBUG, "%s: Loaded %d channel(s) %d group(s)", __FUNCTION__, count, (unsigned)m_PVRChannelGroups.size());
  return count;
}

MythChannel PVRClientMythTV::FindChannel(uint32_t channelId) const
{
  Myth::OS::CLockGuard lock(*m_channelsLock);
  ChannelIdMap::const_iterator it = m_channelsById.find(channelId);
  if (it != m_channelsById.end())
    return it->second;
  return MythChannel();
}

int PVRClientMythTV::FindPVRChannelUid(uint32_t channelId) const
{
  Myth::OS::CLockGuard lock(*m_channelsLock);
  PVRChannelMap::const_iterator it = m_PVRChannelUidById.find(channelId);
  if (it != m_PVRChannelUidById.end())
    return it->second;
  return PVR_CHANNEL_INVALID_UID;
}

int PVRClientMythTV::GetRecordingsAmount()
{
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  if (m_recordingsAmountChange)
  {
    int res = 0;
    Myth::OS::CLockGuard lock(*m_recordingsLock);
    for (ProgramInfoMap::iterator it = m_recordings.begin(); it != m_recordings.end(); ++it)
    {
      if (!it->second.IsNull() && it->second.IsVisible() && (g_bLiveTVRecordings || !it->second.IsLiveTV()))
        res++;
    }
    m_recordingsAmount = res;
    m_recordingsAmountChange = false;
    XBMC->Log(LOG_DEBUG, "%s: count %d", __FUNCTION__, res);
  }
  return m_recordingsAmount;
}

PVR_ERROR PVRClientMythTV::GetRecordings(ADDON_HANDLE handle)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_recordingsLock);

  // Setup series
  if (g_iGroupRecordings == GROUP_RECORDINGS_ONLY_FOR_SERIES)
  {
    typedef std::map<std::pair<std::string, std::string>, ProgramInfoMap::iterator::pointer> TitlesMap;
    TitlesMap titles;
    for (ProgramInfoMap::iterator it = m_recordings.begin(); it != m_recordings.end(); ++it)
    {
      if (!it->second.IsNull() && it->second.IsVisible() && (g_bLiveTVRecordings || !it->second.IsLiveTV()))
      {
        std::pair<std::string, std::string> title = std::make_pair(it->second.RecordingGroup(), it->second.GroupingTitle());
        TitlesMap::iterator found = titles.find(title);
        if (found != titles.end())
        {
          if (found->second)
          {
            found->second->second.SetPropsSerie(true);
            found->second = NULL;
          }
          it->second.SetPropsSerie(true);
        }
        else
          titles.insert(std::make_pair(title, &(*it)));
      }
    }
  }
  time_t now = time(NULL);
  // Transfer to PVR
  for (ProgramInfoMap::iterator it = m_recordings.begin(); it != m_recordings.end(); ++it)
  {
    if (!it->second.IsNull() && it->second.IsVisible() && (g_bLiveTVRecordings || !it->second.IsLiveTV()))
    {
      PVR_RECORDING tag;
      memset(&tag, 0, sizeof(PVR_RECORDING));
      tag.bIsDeleted = false;

      tag.recordingTime = GetRecordingTime(it->second.Airdate(), it->second.RecordingStartTime());
      tag.iDuration = it->second.Duration();
      tag.iPlayCount = it->second.IsWatched() ? 1 : 0;
      tag.iLastPlayedPosition = it->second.HasBookmark() ? 1 : 0;

      std::string id = it->second.UID();

      std::string str; // a temporary string to build formating label
      std::string title(it->second.Title());
      if (it->second.IsDamaged() && !g_szDamagedColor.empty())
      {
        str.assign(title);
        title.assign("[COLOR ").append(g_szDamagedColor).append("]").append(str).append("[/COLOR]");
      }

      PVR_STRCPY(tag.strRecordingId, id.c_str());
      PVR_STRCPY(tag.strTitle, title.c_str());
      PVR_STRCPY(tag.strEpisodeName, it->second.Subtitle().c_str());
      tag.iSeriesNumber = it->second.Season();
      tag.iEpisodeNumber = it->second.Episode();
      time_t airTime(it->second.Airdate());
      if (difftime(airTime, 0) > 0)
      {
        struct tm airTimeDate;
        localtime_r(&airTime, &airTimeDate);
        tag.iYear = airTimeDate.tm_year + 1900;
      }
      PVR_STRCPY(tag.strPlot, it->second.Description().c_str());
      PVR_STRCPY(tag.strChannelName, it->second.ChannelName().c_str());
      tag.iChannelUid = FindPVRChannelUid(it->second.ChannelID());
      tag.channelType = PVR_RECORDING_CHANNEL_TYPE_TV;

      int genre = m_categories.Category(it->second.Category());
      tag.iGenreSubType = genre&0x0F;
      tag.iGenreType = genre&0xF0;

      // Add recording title to directory to group everything according to its name just like MythTV does
      std::string strDirectory;
      if (!g_bRootDefaultGroup || it->second.RecordingGroup().compare("Default") != 0)
        strDirectory.append(it->second.RecordingGroup());
      if (g_iGroupRecordings == GROUP_RECORDINGS_ALWAYS || (g_iGroupRecordings == GROUP_RECORDINGS_ONLY_FOR_SERIES && it->second.GetPropsSerie()))
        strDirectory.append("/").append(it->second.GroupingTitle());
      PVR_STRCPY(tag.strDirectory, strDirectory.c_str());

      // Images
      std::string strIconPath;
      std::string strThumbnailPath;
      std::string strFanartPath;
      if (m_artworksManager)
      {
        strThumbnailPath = m_artworksManager->GetPreviewIconPath(it->second);

        if (it->second.HasCoverart())
          strIconPath = m_artworksManager->GetArtworkPath(it->second, ArtworkManager::AWTypeCoverart);
        else if (it->second.IsLiveTV())
        {
          MythChannel channel = FindRecordingChannel(it->second);
          if (!channel.IsNull())
            strIconPath = m_artworksManager->GetChannelIconPath(channel);
        }
        else
          strIconPath = strThumbnailPath;

        if (it->second.HasFanart())
          strFanartPath = m_artworksManager->GetArtworkPath(it->second, ArtworkManager::AWTypeFanart);
      }

      PVR_STRCPY(tag.strIconPath, strIconPath.c_str());
      PVR_STRCPY(tag.strThumbnailPath, strThumbnailPath.c_str());
      PVR_STRCPY(tag.strFanartPath, strFanartPath.c_str());

      // EPG Entry (Enables "Play recording" option and icon)
      if (!it->second.IsLiveTV() && difftime(now, it->second.EndTime()) < INTERVAL_DAY) // Up to 1 day in the past
        tag.iEpgEventId = MythEPGInfo::MakeBroadcastID(FindPVRChannelUid(it->second.ChannelID()), it->second.StartTime());

      // Unimplemented
      tag.iLifetime = 0;
      tag.iPriority = 0;
      PVR_STRCPY(tag.strPlotOutline, "");

      PVR->TransferRecordingEntry(handle, &tag);
    }
  }

  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: Done", __FUNCTION__);

  return PVR_ERROR_NO_ERROR;
}

int PVRClientMythTV::GetDeletedRecordingsAmount()
{
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  if (m_deletedRecAmountChange)
  {
    int res = 0;
    Myth::OS::CLockGuard lock(*m_recordingsLock);
    for (ProgramInfoMap::iterator it = m_recordings.begin(); it != m_recordings.end(); ++it)
    {
      if (!it->second.IsNull() && it->second.IsDeleted() && (g_bLiveTVRecordings || !it->second.IsLiveTV()))
        res++;
    }
    m_deletedRecAmount = res;
    m_deletedRecAmountChange = false;
    XBMC->Log(LOG_DEBUG, "%s: count %d", __FUNCTION__, res);
  }
  return m_deletedRecAmount;
}

PVR_ERROR PVRClientMythTV::GetDeletedRecordings(ADDON_HANDLE handle)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_recordingsLock);

  // Transfer to PVR
  for (ProgramInfoMap::iterator it = m_recordings.begin(); it != m_recordings.end(); ++it)
  {
    if (!it->second.IsNull() && it->second.IsDeleted() && (g_bLiveTVRecordings || !it->second.IsLiveTV()))
    {
      PVR_RECORDING tag;
      memset(&tag, 0, sizeof(PVR_RECORDING));
      tag.bIsDeleted = true;

      tag.recordingTime = GetRecordingTime(it->second.Airdate(), it->second.RecordingStartTime());
      tag.iDuration = it->second.Duration();
      tag.iPlayCount = it->second.IsWatched() ? 1 : 0;
      tag.iLastPlayedPosition = it->second.HasBookmark() ? 1 : 0;

      std::string id = it->second.UID();

      PVR_STRCPY(tag.strRecordingId, id.c_str());
      PVR_STRCPY(tag.strTitle, it->second.Title().c_str());
      PVR_STRCPY(tag.strEpisodeName, it->second.Subtitle().c_str());
      tag.iSeriesNumber = it->second.Season();
      tag.iEpisodeNumber = it->second.Episode();
      time_t airTime(it->second.Airdate());
      if (difftime(airTime, 0) > 0)
      {
        struct tm airTimeDate;
        localtime_r(&airTime, &airTimeDate);
        tag.iYear = airTimeDate.tm_year + 1900;
      }
      PVR_STRCPY(tag.strPlot, it->second.Description().c_str());
      PVR_STRCPY(tag.strChannelName, it->second.ChannelName().c_str());
      tag.iChannelUid = FindPVRChannelUid(it->second.ChannelID());
      tag.channelType = PVR_RECORDING_CHANNEL_TYPE_TV;

      int genre = m_categories.Category(it->second.Category());
      tag.iGenreSubType = genre&0x0F;
      tag.iGenreType = genre&0xF0;

      // Default to root of deleted view
      PVR_STRCPY(tag.strDirectory, "");

      // Images
      std::string strIconPath;
      std::string strThumbnailPath;
      std::string strFanartPath;
      if (m_artworksManager)
      {
        strThumbnailPath = m_artworksManager->GetPreviewIconPath(it->second);

        if (it->second.HasCoverart())
          strIconPath = m_artworksManager->GetArtworkPath(it->second, ArtworkManager::AWTypeCoverart);
        else if (it->second.IsLiveTV())
        {
          MythChannel channel = FindRecordingChannel(it->second);
          if (!channel.IsNull())
            strIconPath = m_artworksManager->GetChannelIconPath(channel);
        }
        else
          strIconPath = strThumbnailPath;

        if (it->second.HasFanart())
          strFanartPath = m_artworksManager->GetArtworkPath(it->second, ArtworkManager::AWTypeFanart);
      }
      PVR_STRCPY(tag.strIconPath, strIconPath.c_str());
      PVR_STRCPY(tag.strThumbnailPath, strIconPath.c_str());
      PVR_STRCPY(tag.strFanartPath, strFanartPath.c_str());

      // Unimplemented
      tag.iLifetime = 0;
      tag.iPriority = 0;
      PVR_STRCPY(tag.strPlotOutline, "");

      /* TODO: PVR API 5.0.0: Implement this */
      tag.iChannelUid = PVR_CHANNEL_INVALID_UID;

      /* TODO: PVR API 5.1.0: Implement this */
      tag.channelType = PVR_RECORDING_CHANNEL_TYPE_UNKNOWN;

      PVR->TransferRecordingEntry(handle, &tag);
    }
  }

  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: Done", __FUNCTION__);

  return PVR_ERROR_NO_ERROR;
}

void PVRClientMythTV::ForceUpdateRecording(ProgramInfoMap::iterator it)
{
  if (!m_control)
    return;
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  if (!it->second.IsNull())
  {
    MythProgramInfo prog(m_control->GetRecorded(it->second.ChannelID(), it->second.RecordingStartTime()));
    if (!prog.IsNull())
    {
      // Copy props
      prog.CopyProps(it->second);
      // Update recording
      it->second = prog;
      ++m_recordingChangePinCount;

      if (g_bExtraDebug)
        XBMC->Log(LOG_DEBUG, "%s: Done", __FUNCTION__);
    }
  }
}

int PVRClientMythTV::FillRecordings()
{
  int count = 0;
  if (!m_control || !m_eventHandler)
    return count;
  XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  // Check event connection
  if (!m_eventHandler->IsConnected())
    return count;

  // Load recordings map
  m_recordings.clear();
  m_recordingsAmount = 0;
  m_deletedRecAmount = 0;
  Myth::ProgramListPtr programs = m_control->GetRecordedList();
  for (Myth::ProgramList::iterator it = programs->begin(); it != programs->end(); ++it)
  {
    MythProgramInfo prog = MythProgramInfo(*it);
    m_recordings.insert(std::make_pair(prog.UID(), prog));
    ++count;
  }
  if (count > 0)
    m_recordingsAmountChange = m_deletedRecAmountChange = true; // Need count amounts
  XBMC->Log(LOG_DEBUG, "%s: count %d", __FUNCTION__, count);
  return count;
}

PVR_ERROR PVRClientMythTV::DeleteRecording(const PVR_RECORDING &recording)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_recordingsLock);

  ProgramInfoMap::iterator it = m_recordings.find(recording.strRecordingId);
  if (it != m_recordings.end())
  {
    // Deleting Live recording is prohibited. Otherwise continue
    if (this->IsMyLiveRecording(it->second))
    {
      if (it->second.IsLiveTV())
        return PVR_ERROR_RECORDING_RUNNING;
      // it is kept then ignore it now.
      if (m_liveStream && m_liveStream->KeepLiveRecording(false))
        return PVR_ERROR_NO_ERROR;
      else
        return PVR_ERROR_FAILED;
    }
    bool ret = m_control->DeleteRecording(*(it->second.GetPtr()));
    if (ret)
    {
      XBMC->Log(LOG_DEBUG, "%s: Deleted recording %s", __FUNCTION__, recording.strRecordingId);
      return PVR_ERROR_NO_ERROR;
    }
    else
    {
      XBMC->Log(LOG_ERROR, "%s: Failed to delete recording %s", __FUNCTION__, recording.strRecordingId);
    }
  }
  else
  {
    XBMC->Log(LOG_ERROR, "%s: Recording %s does not exist", __FUNCTION__, recording.strRecordingId);
  }
  return PVR_ERROR_FAILED;
}

PVR_ERROR PVRClientMythTV::DeleteAndForgetRecording(const PVR_RECORDING &recording)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_recordingsLock);

  ProgramInfoMap::iterator it = m_recordings.find(recording.strRecordingId);
  if (it != m_recordings.end())
  {
    // Deleting Live recording is prohibited. Otherwise continue
    if (this->IsMyLiveRecording(it->second))
    {
      if (it->second.IsLiveTV())
        return PVR_ERROR_RECORDING_RUNNING;
      // it is kept then ignore it now.
      if (m_liveStream && m_liveStream->KeepLiveRecording(false))
        return PVR_ERROR_NO_ERROR;
      else
        return PVR_ERROR_FAILED;
    }
    bool ret = m_control->DeleteRecording(*(it->second.GetPtr()), false, true);
    if (ret)
    {
      XBMC->Log(LOG_DEBUG, "%s: Deleted and forget recording %s", __FUNCTION__, recording.strRecordingId);
      return PVR_ERROR_NO_ERROR;
    }
    else
    {
      XBMC->Log(LOG_ERROR, "%s: Failed to delete recording %s", __FUNCTION__, recording.strRecordingId);
    }
  }
  else
  {
    XBMC->Log(LOG_ERROR, "%s: Recording %s does not exist", __FUNCTION__, recording.strRecordingId);
  }
  return PVR_ERROR_FAILED;
}

class PromptDeleteRecordingTask : public Task
{
public:
  PromptDeleteRecordingTask(PVRClientMythTV* pvr, const MythProgramInfo& prog)
  : Task()
  , m_pvr(pvr)
  , m_prog(prog) { }

  virtual void Execute()
  {
    m_pvr->PromptDeleteRecording(m_prog);
  }

  PVRClientMythTV *m_pvr;
  MythProgramInfo m_prog;
};

PVR_ERROR PVRClientMythTV::SetRecordingPlayCount(const PVR_RECORDING &recording, int count)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_recordingsLock);
  ProgramInfoMap::iterator it = m_recordings.find(recording.strRecordingId);
  if (it != m_recordings.end())
  {
    if (m_control->UpdateRecordedWatchedStatus(*(it->second.GetPtr()), (count > 0 ? true : false)))
    {
      if (g_bExtraDebug)
        XBMC->Log(LOG_DEBUG, "%s: Set watched state for %s", __FUNCTION__, recording.strRecordingId);
      ForceUpdateRecording(it);
    }
    else
    {
      XBMC->Log(LOG_DEBUG, "%s: Failed setting watched state for: %s", __FUNCTION__, recording.strRecordingId);
    }
    if (g_bPromptDeleteAtEnd)
    {
      m_todo->ScheduleTask(new PromptDeleteRecordingTask(this, it->second), 1000);
    }
    return PVR_ERROR_NO_ERROR;
  }
  else
  {
    XBMC->Log(LOG_DEBUG, "%s: Recording %s does not exist", __FUNCTION__, recording.strRecordingId);
    return PVR_ERROR_FAILED;
  }
}

PVRClientMythTV::cachedBookmark_t PVRClientMythTV::_cachedBookmark = { 0, 0, 0 };

PVR_ERROR PVRClientMythTV::SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition)
{
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: Setting Bookmark for: %s to %d", __FUNCTION__, recording.strTitle, lastplayedposition);

  Myth::OS::CLockGuard lock(*m_recordingsLock);
  ProgramInfoMap::iterator it = m_recordings.find(recording.strRecordingId);
  if (it != m_recordings.end())
  {
    Myth::ProgramPtr prog(it->second.GetPtr());
    lock.Unlock();
    if (prog)
    {
      long long duration = (long long)lastplayedposition * 1000;
      // Write the bookmark
      if (m_control->SetSavedBookmark(*prog, 2, duration))
      {
        _cachedBookmark = { recording.iChannelUid, recording.recordingTime, lastplayedposition };
        if (g_bExtraDebug)
          XBMC->Log(LOG_DEBUG, "%s: Setting Bookmark successful", __FUNCTION__);
        return PVR_ERROR_NO_ERROR;
      }
    }
    XBMC->Log(LOG_NOTICE, "%s: Setting Bookmark failed", __FUNCTION__);
    return PVR_ERROR_NO_ERROR;
  }
  XBMC->Log(LOG_ERROR, "%s: Recording %s does not exist", __FUNCTION__, recording.strRecordingId);
  return PVR_ERROR_FAILED;
}

int PVRClientMythTV::GetRecordingLastPlayedPosition(const PVR_RECORDING &recording)
{
  if (recording.iChannelUid == _cachedBookmark.channelUid && recording.recordingTime == _cachedBookmark.recordingTime)
  {
    XBMC->Log(LOG_DEBUG, "%s: Returning cached Bookmark for: %s", __FUNCTION__, recording.strTitle);
    return _cachedBookmark.position;
  }

  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: Reading Bookmark for: %s", __FUNCTION__, recording.strTitle);

  Myth::OS::CLockGuard lock(*m_recordingsLock);
  ProgramInfoMap::iterator it = m_recordings.find(recording.strRecordingId);
  if (it != m_recordings.end())
  {
    if (it->second.HasBookmark())
    {
      Myth::ProgramPtr prog(it->second.GetPtr());
      lock.Unlock();
      if (prog)
      {
        int64_t duration = m_control->GetSavedBookmark(*prog, 2); // returns 0 if no bookmark was found
        if (duration > 0)
        {
          int position = (int)(duration / 1000);
          _cachedBookmark = { recording.iChannelUid, recording.recordingTime, position };
          if (g_bExtraDebug)
            XBMC->Log(LOG_DEBUG, "%s: %d", __FUNCTION__, position);
          return position;
        }
      }
    }
    if (g_bExtraDebug)
      XBMC->Log(LOG_DEBUG, "%s: Recording %s has no bookmark", __FUNCTION__, recording.strTitle);
  }
  else
    XBMC->Log(LOG_ERROR, "%s: Recording %s does not exist", __FUNCTION__, recording.strRecordingId);
  _cachedBookmark = { recording.iChannelUid, recording.recordingTime, 0 };
  return 0;
}

PVR_ERROR PVRClientMythTV::GetRecordingEdl(const PVR_RECORDING &recording, PVR_EDL_ENTRY entries[], int *size)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  *size = 0;
  if (g_iEnableEDL == ENABLE_EDL_NEVER)
    return PVR_ERROR_NO_ERROR;
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: Reading edl for: %s", __FUNCTION__, recording.strTitle);
  // Check recording
  MythProgramInfo prog;
  {
    Myth::OS::CLockGuard lock(*m_recordingsLock);
    ProgramInfoMap::iterator it = m_recordings.find(recording.strRecordingId);
    if (it == m_recordings.end())
    {
      XBMC->Log(LOG_ERROR, "%s: Recording %s does not exist", __FUNCTION__, recording.strRecordingId);
      return PVR_ERROR_INVALID_PARAMETERS;
    }
    prog = it->second;
  }

  // Checking backend capabilities
  int unit = 2; // default unit is duration (ms)
  float rate = 1000.0f;
  if (m_control->CheckService() < 85)
  {
    unit = 0; // marks are based on framecount
    // Check required props else return
    rate = prog.GetPropsVideoFrameRate();
    XBMC->Log(LOG_DEBUG, "%s: AV props: Frame Rate = %.3f", __FUNCTION__, rate);
    if (rate <= 0)
      return PVR_ERROR_NO_ERROR;
  }

  Myth::MarkList skpList;

  // Search for commbreak list with defined unit
  Myth::MarkListPtr comList = m_control->GetCommBreakList(*(prog.GetPtr()), unit);
  XBMC->Log(LOG_DEBUG, "%s: Found %d commercial breaks for: %s", __FUNCTION__, comList->size(), recording.strTitle);
  if (!comList->empty())
  {
    if (comList->front()->markType == Myth::MARK_COMM_END)
    {
      Myth::MarkPtr m(new Myth::Mark());
      m->markType = Myth::MARK_COMM_START;
      m->markValue = 0L;
      skpList.push_back(m);
    }
    skpList.insert(skpList.end(), comList->begin(), comList->end());
    if (comList->back()->markType == Myth::MARK_COMM_START)
    {
      Myth::MarkPtr m(new Myth::Mark());
      m->markType = Myth::MARK_COMM_END;
      m->markValue = (int64_t)(prog.Duration()) * rate;
      skpList.push_back(m);
    }
  }

  // Search for cutting list with defined unit
  Myth::MarkListPtr cutList = m_control->GetCutList(*(prog.GetPtr()), unit);
  XBMC->Log(LOG_DEBUG, "%s: Found %d cut list entries for: %s", __FUNCTION__, cutList->size(), recording.strTitle);
  if (!cutList->empty())
  {
    if (cutList->front()->markType == Myth::MARK_CUT_END)
    {
      Myth::MarkPtr m(new Myth::Mark());
      m->markType = Myth::MARK_CUT_START;
      m->markValue = 0L;
      skpList.push_back(m);
    }
    skpList.insert(skpList.end(), cutList->begin(), cutList->end());
    if (cutList->back()->markType == Myth::MARK_CUT_START)
    {
      Myth::MarkPtr m(new Myth::Mark());
      m->markType = Myth::MARK_CUT_END;
      m->markValue = (int64_t)(prog.Duration()) * rate;
      skpList.push_back(m);
    }
  }

  // Open dialog
  if (g_iEnableEDL == ENABLE_EDL_DIALOG && !skpList.empty())
  {
    bool canceled = false;
    if (!GUI->Dialog_YesNo_ShowAndGetInput(XBMC->GetLocalizedString(30110), XBMC->GetLocalizedString(30111), canceled) && !canceled)
      return PVR_ERROR_NO_ERROR;
  }

  // Processing marks
  int index = 0;
  Myth::MarkList::const_iterator it;
  Myth::MarkPtr startPtr;
  for (it = skpList.begin(); it != skpList.end(); ++it)
  {
    if (index >= PVR_ADDON_EDL_LENGTH)
      break;
    switch ((*it)->markType)
    {
      case Myth::MARK_COMM_START:
      case Myth::MARK_CUT_START:
        startPtr = *it;
        break;
      case Myth::MARK_COMM_END:
        if (startPtr && startPtr->markType == Myth::MARK_COMM_START && (*it)->markValue > startPtr->markValue)
        {
          PVR_EDL_ENTRY entry;
          double s = (double)(startPtr->markValue) / rate;
          double e = (double)((*it)->markValue) / rate;
          // Use scene marker instead commercial break
          if (g_iEnableEDL == ENABLE_EDL_SCENE)
          {
            entry.start = entry.end = (int64_t)(e * 1000.0);
            entry.type = PVR_EDL_TYPE_SCENE;
            XBMC->Log(LOG_DEBUG, "%s: SCENE %9.3f", __FUNCTION__, e);
          }
          else
          {
            entry.start = (int64_t)(s * 1000.0);
            entry.end = (int64_t)(e * 1000.0);
            entry.type = PVR_EDL_TYPE_COMBREAK;
            XBMC->Log(LOG_DEBUG, "%s: COMBREAK %9.3f - %9.3f", __FUNCTION__, s, e);
          }
          entries[index] = entry;
          index++;
        }
        startPtr.reset();
        break;
      case Myth::MARK_CUT_END:
        if (startPtr && startPtr->markType == Myth::MARK_CUT_START && (*it)->markValue > startPtr->markValue)
        {
          PVR_EDL_ENTRY entry;
          double s = (double)(startPtr->markValue) / rate;
          double e = (double)((*it)->markValue) / rate;
          entry.start = (int64_t)(s * 1000.0);
          entry.end = (int64_t)(e * 1000.0);
          entry.type = PVR_EDL_TYPE_CUT;
          entries[index] = entry;
          index++;
          if (g_bExtraDebug)
            XBMC->Log(LOG_DEBUG, "%s: CUT %9.3f - %9.3f", __FUNCTION__, s, e);
        }
        startPtr.reset();
        break;
      default:
        startPtr.reset();
    }
  }

  *size = index;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::UndeleteRecording(const PVR_RECORDING &recording)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_recordingsLock);

  ProgramInfoMap::iterator it = m_recordings.find(recording.strRecordingId);
  if (it != m_recordings.end())
  {
    bool ret = m_control->UndeleteRecording(*(it->second.GetPtr()));
    if (ret)
    {
      XBMC->Log(LOG_DEBUG, "%s: Undeleted recording %s", __FUNCTION__, recording.strRecordingId);
      return PVR_ERROR_NO_ERROR;
    }
    else
    {
      XBMC->Log(LOG_ERROR, "%s: Failed to undelete recording %s", __FUNCTION__, recording.strRecordingId);
    }
  }
  else
  {
    XBMC->Log(LOG_ERROR, "%s: Recording %s does not exist", __FUNCTION__, recording.strRecordingId);
  }
  return PVR_ERROR_FAILED;
}

PVR_ERROR PVRClientMythTV::PurgeDeletedRecordings()
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  bool err = false;
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_recordingsLock);

  for (ProgramInfoMap::iterator it = m_recordings.begin(); it != m_recordings.end(); ++it)
  {
    if (!it->second.IsNull() && it->second.IsDeleted())
    {
      if (m_control->DeleteRecording(*(it->second.GetPtr())))
        XBMC->Log(LOG_DEBUG, "%s: Deleted recording %s", __FUNCTION__, it->first.c_str());
      else
      {
        err = true;
        XBMC->Log(LOG_ERROR, "%s: Failed to delete recording %s", __FUNCTION__, it->first.c_str());
      }
    }
  }
  if (err)
    return PVR_ERROR_REJECTED;
  return PVR_ERROR_NO_ERROR;
}

MythChannel PVRClientMythTV::FindRecordingChannel(const MythProgramInfo& programInfo) const
{
  return FindChannel(programInfo.ChannelID());
}

bool PVRClientMythTV::IsMyLiveRecording(const MythProgramInfo& programInfo)
{
  if (!programInfo.IsNull())
  {
    // Begin critical section
    Myth::OS::CLockGuard lock(*m_lock);
    if (m_liveStream && m_liveStream->IsPlaying())
    {
      MythProgramInfo live(m_liveStream->GetPlayedProgram());
      if (live == programInfo)
        return true;
    }
  }
  return false;
}

int PVRClientMythTV::GetTimersAmount(void)
{
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  if (m_scheduleManager)
    return m_scheduleManager->GetUpcomingCount();
  return 0;
}

PVR_ERROR PVRClientMythTV::GetTimers(ADDON_HANDLE handle)
{
  if (!m_scheduleManager)
    return PVR_ERROR_SERVER_ERROR;
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  MythTimerEntryList entries;
  {
    Myth::OS::CLockGuard lock(*m_lock);
    m_PVRtimerMemorandum.clear();
    entries = m_scheduleManager->GetTimerEntries();
  }
  for (MythTimerEntryList::const_iterator it = entries.begin(); it != entries.end(); ++it)
  {
    PVR_TIMER tag;
    memset(&tag, 0, sizeof(PVR_TIMER));

    tag.iClientIndex = (*it)->entryIndex;
    tag.iParentClientIndex = (*it)->parentIndex;
    tag.iClientChannelUid = FindPVRChannelUid((*it)->chanid);
    tag.startTime = (*it)->startTime;
    tag.endTime = (*it)->endTime;

    // Discard upcoming without valid channel uid
    if (tag.iClientChannelUid == PVR_CHANNEL_INVALID_UID && !(*it)->isRule)
      continue;

    // Status: Match recording status with PVR_TIMER status
    switch ((*it)->recordingStatus)
    {
    case Myth::RS_ABORTED:
    case Myth::RS_MISSED:
    case Myth::RS_NOT_LISTED:
    case Myth::RS_OFFLINE:
      tag.state = PVR_TIMER_STATE_ABORTED;
      break;
    case Myth::RS_RECORDING:
    case Myth::RS_TUNING:
      tag.state = PVR_TIMER_STATE_RECORDING;
      break;
    case Myth::RS_RECORDED:
      tag.state = PVR_TIMER_STATE_COMPLETED;
      break;
    case Myth::RS_WILL_RECORD:
      tag.state = PVR_TIMER_STATE_SCHEDULED;
      break;
    case Myth::RS_CONFLICT:
      tag.state = PVR_TIMER_STATE_CONFLICT_NOK;
      break;
    case Myth::RS_FAILED:
    case Myth::RS_TUNER_BUSY:
    case Myth::RS_LOW_DISKSPACE:
      tag.state = PVR_TIMER_STATE_ERROR;
      break;
    case Myth::RS_INACTIVE:
    case Myth::RS_EARLIER_RECORDING:  //Another entry in the list will record 'earlier'
    case Myth::RS_LATER_SHOWING:      //Another entry in the list will record 'later'
    case Myth::RS_CURRENT_RECORDING:  //Already in the current library
    case Myth::RS_PREVIOUS_RECORDING: //Recorded before but not in the library anylonger
    case Myth::RS_TOO_MANY_RECORDINGS:
    case Myth::RS_OTHER_SHOWING:
    case Myth::RS_REPEAT:
    case Myth::RS_DONT_RECORD:
    case Myth::RS_NEVER_RECORD:
      tag.state = PVR_TIMER_STATE_DISABLED;
      break;
    case Myth::RS_CANCELLED:
      tag.state = PVR_TIMER_STATE_CANCELLED;
      break;
    case Myth::RS_UNKNOWN:
      if ((*it)->isInactive)
        tag.state = PVR_TIMER_STATE_DISABLED;
      else
        tag.state = PVR_TIMER_STATE_SCHEDULED;
    }

    tag.iTimerType = static_cast<unsigned>((*it)->timerType);
    PVR_STRCPY(tag.strTitle, (*it)->title.c_str());
    PVR_STRCPY(tag.strEpgSearchString, (*it)->epgSearch.c_str());
    tag.bFullTextEpgSearch = false;
    PVR_STRCPY(tag.strDirectory, ""); // not implemented
    PVR_STRCPY(tag.strSummary, (*it)->description.c_str());
    tag.iPriority = (*it)->priority;
    tag.iLifetime = (*it)->expiration;
    tag.iRecordingGroup = (*it)->recordingGroup;
    tag.firstDay = (*it)->startTime; // using startTime
    tag.iWeekdays = PVR_WEEKDAY_NONE; // not implemented
    tag.iPreventDuplicateEpisodes = static_cast<unsigned>((*it)->dupMethod);
    if ((*it)->epgCheck)
      tag.iEpgUid = MythEPGInfo::MakeBroadcastID(FindPVRChannelUid((*it)->epgInfo.ChannelID()) , (*it)->epgInfo.StartTime());
    tag.iMarginStart = (*it)->startOffset;
    tag.iMarginEnd = (*it)->endOffset;
    int genre = m_categories.Category((*it)->category);
    tag.iGenreType = genre & 0xF0;
    tag.iGenreSubType = genre & 0x0F;

    // Add it to memorandom: cf UpdateTimer()
    MYTH_SHARED_PTR<PVR_TIMER> pTag = MYTH_SHARED_PTR<PVR_TIMER>(new PVR_TIMER(tag));
    m_PVRtimerMemorandum.insert(std::make_pair((unsigned int&)tag.iClientIndex, pTag));
    PVR->TransferTimerEntry(handle, &tag);
    if (g_bExtraDebug)
      XBMC->Log(LOG_DEBUG, "%s: #%u: IN=%d RS=%d type %u state %d parent %u autoexpire %d", __FUNCTION__,
              tag.iClientIndex, (*it)->isInactive, (*it)->recordingStatus,
              tag.iTimerType, (int)tag.state, tag.iParentClientIndex, tag.iLifetime);
  }

  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: Done", __FUNCTION__);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::AddTimer(const PVR_TIMER &timer)
{
  if (!m_scheduleManager)
    return PVR_ERROR_SERVER_ERROR;
  if (g_bExtraDebug)
  {
    XBMC->Log(LOG_DEBUG, "%s: iClientIndex = %d", __FUNCTION__, timer.iClientIndex);
    XBMC->Log(LOG_DEBUG, "%s: iParentClientIndex = %d", __FUNCTION__, timer.iParentClientIndex);
    XBMC->Log(LOG_DEBUG, "%s: iClientChannelUid = %d", __FUNCTION__, timer.iClientChannelUid);
    XBMC->Log(LOG_DEBUG, "%s: startTime = %ld", __FUNCTION__, timer.startTime);
    XBMC->Log(LOG_DEBUG, "%s: endTime = %ld", __FUNCTION__, timer.endTime);
    XBMC->Log(LOG_DEBUG, "%s: state = %d", __FUNCTION__, timer.state);
    XBMC->Log(LOG_DEBUG, "%s: iTimerType = %d", __FUNCTION__, timer.iTimerType);
    XBMC->Log(LOG_DEBUG, "%s: strTitle = %s", __FUNCTION__, timer.strTitle);
    XBMC->Log(LOG_DEBUG, "%s: strEpgSearchString = %s", __FUNCTION__, timer.strEpgSearchString);
    XBMC->Log(LOG_DEBUG, "%s: bFullTextEpgSearch = %d", __FUNCTION__, timer.bFullTextEpgSearch);
    XBMC->Log(LOG_DEBUG, "%s: strDirectory = %s", __FUNCTION__, timer.strDirectory);
    XBMC->Log(LOG_DEBUG, "%s: strSummary = %s", __FUNCTION__, timer.strSummary);
    XBMC->Log(LOG_DEBUG, "%s: iPriority = %d", __FUNCTION__, timer.iPriority);
    XBMC->Log(LOG_DEBUG, "%s: iLifetime = %d", __FUNCTION__, timer.iLifetime);
    XBMC->Log(LOG_DEBUG, "%s: firstDay = %d", __FUNCTION__, timer.firstDay);
    XBMC->Log(LOG_DEBUG, "%s: iWeekdays = %d", __FUNCTION__, timer.iWeekdays);
    XBMC->Log(LOG_DEBUG, "%s: iPreventDuplicateEpisodes = %d", __FUNCTION__, timer.iPreventDuplicateEpisodes);
    XBMC->Log(LOG_DEBUG, "%s: iEpgUid = %d", __FUNCTION__, timer.iEpgUid);
    XBMC->Log(LOG_DEBUG, "%s: iMarginStart = %d", __FUNCTION__, timer.iMarginStart);
    XBMC->Log(LOG_DEBUG, "%s: iMarginEnd = %d", __FUNCTION__, timer.iMarginEnd);
    XBMC->Log(LOG_DEBUG, "%s: iGenreType = %d", __FUNCTION__, timer.iGenreType);
    XBMC->Log(LOG_DEBUG, "%s: iGenreSubType = %d", __FUNCTION__, timer.iGenreSubType);
    XBMC->Log(LOG_DEBUG, "%s: iRecordingGroup = %d", __FUNCTION__, timer.iRecordingGroup);
  }
  XBMC->Log(LOG_DEBUG, "%s: title: %s, start: %ld, end: %ld, chanID: %u", __FUNCTION__, timer.strTitle, timer.startTime, timer.endTime, timer.iClientChannelUid);
  Myth::OS::CLockGuard lock(*m_lock);
  // Check if our timer is a quick recording of live tv
  // Assumptions: Our live recorder is locked on the same channel and the recording starts
  // at the same time as or before (includes 0) the currently in progress program
  // If true then keep recording, setup recorder and let the backend handle the rule.
  if (m_liveStream && m_liveStream->IsPlaying())
  {
    Myth::ProgramPtr program = m_liveStream->GetPlayedProgram();
    if (timer.iClientChannelUid == FindPVRChannelUid(program->channel.chanId) &&
        timer.startTime <= program->startTime)
    {
      XBMC->Log(LOG_DEBUG, "%s: Timer is a quick recording. Toggling Record on", __FUNCTION__);
      if (m_liveStream->IsLiveRecording())
        XBMC->Log(LOG_NOTICE, "%s: Record already on! Retrying...", __FUNCTION__);
      else
      {
        // Add bookmark for the current stream position
        m_control->SetSavedBookmark(*program, 1, m_liveStream->GetPosition());
      }
      if (m_liveStream->KeepLiveRecording(true))
        return PVR_ERROR_NO_ERROR;
      else
        // Supress error notification! XBMC locks if we return an error here.
        return PVR_ERROR_NO_ERROR;
    }
  }

  // Otherwise submit the new timer
  XBMC->Log(LOG_DEBUG, "%s: Submitting new timer", __FUNCTION__);
  MythTimerEntry entry = PVRtoTimerEntry(timer, true);
  MythScheduleManager::MSM_ERROR ret = m_scheduleManager->SubmitTimer(entry);
  if (ret == MythScheduleManager::MSM_ERROR_FAILED)
    return PVR_ERROR_FAILED;
  if (ret == MythScheduleManager::MSM_ERROR_NOT_IMPLEMENTED)
    return PVR_ERROR_REJECTED;

  // Completion of the scheduling will be signaled by a SCHEDULE_CHANGE event.
  // Thus no need to call TriggerTimerUpdate().
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::DeleteTimer(const PVR_TIMER &timer, bool force)
{
  if (!m_scheduleManager)
    return PVR_ERROR_SERVER_ERROR;
  if (g_bExtraDebug)
  {
    XBMC->Log(LOG_DEBUG, "%s: iClientIndex = %d", __FUNCTION__, timer.iClientIndex);
    XBMC->Log(LOG_DEBUG, "%s: state = %d", __FUNCTION__, timer.state);
    XBMC->Log(LOG_DEBUG, "%s: iTimerType = %d", __FUNCTION__, timer.iTimerType);
  }
  // Check if our timer is related to rule for live recording:
  // Assumptions: Recorder handle same recording.
  // If true then expire recording, setup recorder and let backend handle the rule.
  {
    Myth::OS::CLockGuard lock(*m_lock);
    if (m_liveStream && m_liveStream->IsLiveRecording())
    {
      MythRecordingRuleNodePtr node = m_scheduleManager->FindRuleByIndex(timer.iClientIndex);
      if (node)
      {
        MythScheduleList reclist = m_scheduleManager->FindUpComingByRuleId(node->GetRule().RecordID());
        MythScheduleList::const_iterator it = reclist.begin();
        if (it != reclist.end() && it->second && IsMyLiveRecording(*(it->second)))
        {
          XBMC->Log(LOG_DEBUG, "%s: Timer %u is a quick recording. Toggling Record off", __FUNCTION__, timer.iClientIndex);
          if (m_liveStream->KeepLiveRecording(false))
            return PVR_ERROR_NO_ERROR;
          else
            return PVR_ERROR_FAILED;
        }
      }
    }
  }

  // Otherwise delete timer
  XBMC->Log(LOG_DEBUG, "%s: Deleting timer %u force %s", __FUNCTION__, timer.iClientIndex, (force ? "true" : "false"));
  MythTimerEntry entry = PVRtoTimerEntry(timer, false);
  MythScheduleManager::MSM_ERROR ret = m_scheduleManager->DeleteTimer(entry);
  if (ret == MythScheduleManager::MSM_ERROR_FAILED)
    return PVR_ERROR_FAILED;
  if (ret == MythScheduleManager::MSM_ERROR_NOT_IMPLEMENTED)
    return PVR_ERROR_NOT_IMPLEMENTED;

  return PVR_ERROR_NO_ERROR;
}

MythTimerEntry PVRClientMythTV::PVRtoTimerEntry(const PVR_TIMER& timer, bool checkEPG)
{
  MythTimerEntry entry;

  bool hasEpg = false;
  bool hasTimeslot = false;
  bool hasChannel = false;
  bool hasEpgSearch = false;
  time_t st = timer.startTime;
  time_t et = timer.endTime;
  time_t fd = timer.firstDay;
  time_t now = time(NULL);

  if (checkEPG && timer.iEpgUid != PVR_TIMER_NO_EPG_UID)
  {
    entry.epgCheck = true;
    hasEpg = true;
  }
  if (timer.iClientChannelUid != PVR_TIMER_ANY_CHANNEL)
  {
    hasChannel = true;
  }
  // Fix timeslot as needed
  if (st == 0 && difftime(et, 0) > INTERVAL_DAY)
  {
    st = now;
  }
  // near 0 or invalid unix time seems to be ANY TIME
  if (difftime(st, 0) < INTERVAL_DAY)
  {
    st = et = 0;
    hasTimeslot = false;
  }
  else
  {
    hasTimeslot = true;
    struct tm oldtm;
    struct tm newtm;
    if (difftime(fd, st) > 0)
    {
      localtime_r(&fd, &newtm);
      localtime_r(&st, &oldtm);
      newtm.tm_hour = oldtm.tm_hour;
      newtm.tm_min = oldtm.tm_min;
      newtm.tm_sec = 0;
      st = mktime(&newtm);
      localtime_r(&et, &oldtm);
      newtm.tm_hour = oldtm.tm_hour;
      newtm.tm_min = oldtm.tm_min;
      newtm.tm_sec = 0;
      et = mktime(&newtm);
    }
    else
    {
      localtime_r(&st, &oldtm);
      oldtm.tm_sec = 0;
      st = mktime(&oldtm);
      localtime_r(&et, &oldtm);
      oldtm.tm_sec = 0;
      et = mktime(&oldtm);
    }
    // Adjust end time as needed
    if (et < st)
    {
      localtime_r(&et, &oldtm);
      localtime_r(&st, &newtm);
      newtm.tm_hour = oldtm.tm_hour;
      newtm.tm_min = oldtm.tm_min;
      newtm.tm_sec = oldtm.tm_sec;
      newtm.tm_mday++;
      et = mktime(&newtm);
    }
  }
  if (*(timer.strEpgSearchString))
  {
    hasEpgSearch = true;
  }

  XBMC->Log(LOG_DEBUG, "%s: EPG=%d CHAN=%d TS=%d SEARCH=%d", __FUNCTION__, hasEpg, hasChannel, hasTimeslot, hasEpgSearch);

  // Fill EPG
  if (hasEpg && m_control)
  {
    unsigned bid;
    time_t bst;
    MythEPGInfo::BreakBroadcastID(timer.iEpgUid, &bid, &bst);
    XBMC->Log(LOG_DEBUG, "%s: broadcastid=%u chanid=%u localtime=%s", __FUNCTION__, (unsigned)timer.iEpgUid, bid, Myth::TimeToString(bst, false).c_str());
    // Retrieve broadcast using prior selected channel if valid else use original channel
    if (hasChannel)
    {
      bid = static_cast<unsigned>(timer.iClientChannelUid);
      XBMC->Log(LOG_DEBUG, "%s: original chanid is overridden with id %u", __FUNCTION__, bid);
    }
    Myth::ProgramMapPtr epg = m_control->GetProgramGuide(bid, bst, bst);
    Myth::ProgramMap::iterator epgit = epg->begin();
    // Get the last and longer
    for (Myth::ProgramMap::iterator it = epgit; it != epg->end(); ++it)
    {
      if (it->second->startTime > epgit->second->startTime ||
              (it->second->startTime == epgit->second->startTime && it->second->endTime > epgit->second->endTime))
        epgit = it;
    }
    if (epgit != epg->end())
    {
      entry.epgInfo = MythEPGInfo(epgit->second);
      entry.chanid = epgit->second->channel.chanId;
      entry.callsign = epgit->second->channel.callSign;
      st = entry.epgInfo.StartTime();
      et = entry.epgInfo.EndTime();
      XBMC->Log(LOG_NOTICE, "%s: select EPG program: %u %lu %s", __FUNCTION__, entry.chanid, st, entry.epgInfo.Title().c_str());
    }
    else
    {
      XBMC->Log(LOG_NOTICE, "%s: EPG program not found: %u %lu", __FUNCTION__, bid, bst);
      hasEpg = false;
    }
  }
  // Fill channel
  if (!hasEpg && hasChannel)
  {
    MythChannel channel = FindChannel(timer.iClientChannelUid);
    if (!channel.IsNull())
    {
      entry.chanid = channel.ID();
      entry.callsign = channel.Callsign();
      XBMC->Log(LOG_DEBUG,"%s: Found channel: %u %s", __FUNCTION__, entry.chanid, entry.callsign.c_str());
    }
    else
    {
      XBMC->Log(LOG_NOTICE,"%s: Channel not found: %u", __FUNCTION__, timer.iClientChannelUid);
      hasChannel = false;
    }
  }
  // Fill others
  if (hasTimeslot)
  {
    entry.startTime = st;
    entry.endTime = et;
  }
  if (hasEpgSearch)
  {
    unsigned i = 0;
    while (timer.strEpgSearchString[i] && isspace(timer.strEpgSearchString[i] != 0)) ++i;
    if (timer.strEpgSearchString[i])
      entry.epgSearch.assign(&(timer.strEpgSearchString[i]));
  }
  entry.timerType = static_cast<TimerTypeId>(timer.iTimerType);
  entry.title.assign(timer.strTitle);
  entry.description.assign(timer.strSummary);
  entry.category.assign(m_categories.Category(timer.iGenreType));
  entry.startOffset = timer.iMarginStart;
  entry.endOffset = timer.iMarginEnd;
  entry.dupMethod = static_cast<Myth::DM_t>(timer.iPreventDuplicateEpisodes);
  entry.priority = timer.iPriority;
  entry.expiration = timer.iLifetime;
  entry.firstShowing = false;
  entry.recordingGroup = timer.iRecordingGroup;
  if (timer.iTimerType == TIMER_TYPE_DONT_RECORD)
    entry.isInactive = (timer.state == PVR_TIMER_STATE_DISABLED ? false : true);
  else
    entry.isInactive = (timer.state == PVR_TIMER_STATE_DISABLED ? true : false);
  entry.entryIndex = timer.iClientIndex;
  entry.parentIndex = timer.iParentClientIndex;
  return entry;
}

PVR_ERROR PVRClientMythTV::UpdateTimer(const PVR_TIMER &timer)
{
  if (!m_scheduleManager)
    return PVR_ERROR_SERVER_ERROR;
  if (g_bExtraDebug)
  {
    XBMC->Log(LOG_DEBUG, "%s: iClientIndex = %d", __FUNCTION__, timer.iClientIndex);
    XBMC->Log(LOG_DEBUG, "%s: iParentClientIndex = %d", __FUNCTION__, timer.iParentClientIndex);
    XBMC->Log(LOG_DEBUG, "%s: iClientChannelUid = %d", __FUNCTION__, timer.iClientChannelUid);
    XBMC->Log(LOG_DEBUG, "%s: startTime = %ld", __FUNCTION__, timer.startTime);
    XBMC->Log(LOG_DEBUG, "%s: endTime = %ld", __FUNCTION__, timer.endTime);
    XBMC->Log(LOG_DEBUG, "%s: state = %d", __FUNCTION__, timer.state);
    XBMC->Log(LOG_DEBUG, "%s: iTimerType = %d", __FUNCTION__, timer.iTimerType);
    XBMC->Log(LOG_DEBUG, "%s: strTitle = %s", __FUNCTION__, timer.strTitle);
    XBMC->Log(LOG_DEBUG, "%s: strEpgSearchString = %s", __FUNCTION__, timer.strEpgSearchString);
    XBMC->Log(LOG_DEBUG, "%s: bFullTextEpgSearch = %d", __FUNCTION__, timer.bFullTextEpgSearch);
    XBMC->Log(LOG_DEBUG, "%s: strDirectory = %s", __FUNCTION__, timer.strDirectory);
    XBMC->Log(LOG_DEBUG, "%s: strSummary = %s", __FUNCTION__, timer.strSummary);
    XBMC->Log(LOG_DEBUG, "%s: iPriority = %d", __FUNCTION__, timer.iPriority);
    XBMC->Log(LOG_DEBUG, "%s: iLifetime = %d", __FUNCTION__, timer.iLifetime);
    XBMC->Log(LOG_DEBUG, "%s: firstDay = %d", __FUNCTION__, timer.firstDay);
    XBMC->Log(LOG_DEBUG, "%s: iWeekdays = %d", __FUNCTION__, timer.iWeekdays);
    XBMC->Log(LOG_DEBUG, "%s: iPreventDuplicateEpisodes = %d", __FUNCTION__, timer.iPreventDuplicateEpisodes);
    XBMC->Log(LOG_DEBUG, "%s: iEpgUid = %d", __FUNCTION__, timer.iEpgUid);
    XBMC->Log(LOG_DEBUG, "%s: iMarginStart = %d", __FUNCTION__, timer.iMarginStart);
    XBMC->Log(LOG_DEBUG, "%s: iMarginEnd = %d", __FUNCTION__, timer.iMarginEnd);
    XBMC->Log(LOG_DEBUG, "%s: iGenreType = %d", __FUNCTION__, timer.iGenreType);
    XBMC->Log(LOG_DEBUG, "%s: iGenreSubType = %d", __FUNCTION__, timer.iGenreSubType);
    XBMC->Log(LOG_DEBUG, "%s: iRecordingGroup = %d", __FUNCTION__, timer.iRecordingGroup);
  }
  XBMC->Log(LOG_DEBUG, "%s: title: %s, start: %ld, end: %ld, chanID: %u", __FUNCTION__, timer.strTitle, timer.startTime, timer.endTime, timer.iClientChannelUid);
  MythTimerEntry entry;
  // Restore discarded info by PVR manager from our saved timer
  {
    Myth::OS::CLockGuard lock(*m_lock);
    std::map<unsigned int, MYTH_SHARED_PTR<PVR_TIMER> >::const_iterator it = m_PVRtimerMemorandum.find(timer.iClientIndex);
    if (it == m_PVRtimerMemorandum.end())
      return PVR_ERROR_INVALID_PARAMETERS;
    PVR_TIMER newTimer = timer;
    newTimer.iEpgUid = it->second->iEpgUid;
    entry = PVRtoTimerEntry(newTimer, true);
  }
  MythScheduleManager::MSM_ERROR ret = m_scheduleManager->UpdateTimer(entry);
  if (ret == MythScheduleManager::MSM_ERROR_FAILED)
    return PVR_ERROR_FAILED;
  if (ret == MythScheduleManager::MSM_ERROR_NOT_IMPLEMENTED)
    return PVR_ERROR_NOT_IMPLEMENTED;

  XBMC->Log(LOG_DEBUG,"%s: Done", __FUNCTION__);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::GetTimerTypes(PVR_TIMER_TYPE types[], int *size)
{
  unsigned index = 0;
  if (m_scheduleManager)
  {
    MythTimerTypeList typeList = m_scheduleManager->GetTimerTypes();
    assert(typeList.size() <= static_cast<unsigned>(*size));
    for (MythTimerTypeList::const_iterator it = typeList.begin(); it != typeList.end(); ++it)
      (*it)->Fill(&types[index++]);
    *size = index;
    return PVR_ERROR_NO_ERROR;
  }
  //@FIXME: Returning ERROR or empty types will break PVR manager
  memset(&types[index], 0, sizeof(PVR_TIMER_TYPE));
  types[index].iId = 1;
  types[index].iAttributes = PVR_TIMER_TYPE_IS_MANUAL;
  *size = ++index;
  return PVR_ERROR_NO_ERROR;
}

bool PVRClientMythTV::OpenLiveStream(const PVR_CHANNEL &channel)
{
  if (!m_eventHandler)
    return false;
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG,"%s: channel uid: %u, num: %u", __FUNCTION__, channel.iUniqueId, channel.iChannelNumber);

  // Begin critical section
  Myth::OS::CLockGuard lock(*m_lock);
  // First we have to get merged channels for the selected channel
  Myth::ChannelList chanset;
  for (PVRChannelMap::const_iterator it = m_PVRChannelUidById.begin(); it != m_PVRChannelUidById.end(); ++it)
  {
    if (it->second == channel.iUniqueId)
      chanset.push_back(FindChannel(it->first).GetPtr());
  }

  if (chanset.empty())
  {
    XBMC->Log(LOG_ERROR,"%s: Invalid channel", __FUNCTION__);
    return false;
  }
  // Need to create live
  if (!m_liveStream)
    m_liveStream = new Myth::LiveTVPlayback(*m_eventHandler);
  else if (m_liveStream->IsPlaying())
    return false;
  // Configure tuning of channel
  m_liveStream->SetTuneDelay(g_iTuneDelay);
  m_liveStream->SetLimitTuneAttempts(g_bLimitTuneAttempts);
  // Try to open
  if (m_liveStream->SpawnLiveTV(chanset[0]->chanNum, chanset))
  {
    XBMC->Log(LOG_DEBUG, "%s: Done", __FUNCTION__);
    return true;
  }

  SAFE_DELETE(m_liveStream);
  XBMC->Log(LOG_ERROR,"%s: Failed to open live stream", __FUNCTION__);
  // Open the dummy stream 'CHANNEL UNAVAILABLE'
  if (!m_dummyStream)
    m_dummyStream = new FileStreaming(g_szClientPath + PATH_SEPARATOR_STRING + "resources" + PATH_SEPARATOR_STRING + "channel_unavailable.ts");
  if (m_dummyStream && m_dummyStream->IsValid())
  {
    return true;
  }
  SAFE_DELETE(m_dummyStream);
  XBMC->QueueNotification(QUEUE_WARNING, XBMC->GetLocalizedString(30305)); // Channel unavailable
  return false;
}

void PVRClientMythTV::CloseLiveStream()
{
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  // Begin critical section
  Myth::OS::CLockGuard lock(*m_lock);
  // Destroy my stream
  SAFE_DELETE(m_liveStream);
  SAFE_DELETE(m_dummyStream);

  // Reset stop request
  m_stopTV = false;

  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: Done", __FUNCTION__);
}

int PVRClientMythTV::ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  // Keep unlocked
  if (m_stopTV)
  {
    CloseLiveStream();
  }
  else
  {
    if (m_liveStream)
      return m_liveStream->Read(pBuffer, iBufferSize);
    if (m_dummyStream)
      return m_dummyStream->Read(pBuffer, iBufferSize);
  }
  return 0;
}

long long PVRClientMythTV::SeekLiveStream(long long iPosition, int iWhence)
{
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: pos: %lld, whence: %d", __FUNCTION__, iPosition, iWhence);

  Myth::WHENCE_t whence;
  switch (iWhence)
  {
  case SEEK_SET:
    whence = Myth::WHENCE_SET;
    break;
  case SEEK_CUR:
    whence = Myth::WHENCE_CUR;
    break;
  case SEEK_END:
    whence = Myth::WHENCE_END;
    break;
  default:
    return -1;
  }

  long long retval;
  if (m_liveStream)
    retval = (long long) m_liveStream->Seek((int64_t)iPosition, whence);
  else if (m_dummyStream)
    retval = (long long) m_dummyStream->Seek((int64_t)iPosition, whence);
  else
    return -1;

  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: Done - position: %lld", __FUNCTION__, retval);

  return retval;
}

long long PVRClientMythTV::LengthLiveStream()
{
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  long long retval;
  if (m_liveStream)
    retval = (long long) m_liveStream->GetSize();
  else if (m_dummyStream)
    retval = (long long) m_dummyStream->GetSize();
  else
    return -1;

  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: Done - duration: %lld", __FUNCTION__, retval);

  return retval;
}

PVR_ERROR PVRClientMythTV::SignalStatus(PVR_SIGNAL_STATUS &signalStatus)
{
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_lock);
  if (!m_liveStream)
    return PVR_ERROR_REJECTED;

  char buf[50];
  sprintf(buf, "Myth Recorder %u", (unsigned)m_liveStream->GetCardId());
  PVR_STRCPY(signalStatus.strAdapterName, buf);
  Myth::SignalStatusPtr signal = m_liveStream->GetSignal();
  if (signal)
  {
    if (signal->lock)
      PVR_STRCPY(signalStatus.strAdapterStatus, "Locked");
    else
      PVR_STRCPY(signalStatus.strAdapterStatus, "No lock");
    signalStatus.iSignal = signal->signal;
    signalStatus.iBER = signal->ber;
    signalStatus.iSNR = signal->snr;
    signalStatus.iUNC = signal->ucb;
  }

  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: Done", __FUNCTION__);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::GetStreamTimes(PVR_STREAM_TIMES* pStreamTimes)
{
  time_t begTs, endTs;
  {
    Myth::OS::CLockGuard lock(*m_lock);
    if (m_liveStream)
    {
      if (!m_liveStream->IsPlaying())
        return PVR_ERROR_REJECTED;
      unsigned seq = m_liveStream->GetChainedCount();
      if (seq == 0)
        return PVR_ERROR_REJECTED;
      begTs = m_liveStream->GetLiveTimeStart();
      endTs = m_liveStream->GetChainedProgram(seq)->recording.endTs;
      pStreamTimes->startTime = begTs;
    }
    else if (m_recordingStream && !m_recordingStreamInfo.IsNull())
    {
      begTs = m_recordingStreamInfo.RecordingStartTime();
      endTs = m_recordingStreamInfo.RecordingEndTime();
      pStreamTimes->startTime = 0; // for recordings, this must be zero
    }
    else
    {
      return PVR_ERROR_REJECTED;
    }
  }
  time_t now = time(NULL);
  if (now < endTs)
    endTs = now;
  pStreamTimes->ptsStart = 0; // it is started from 0 by the ffmpeg demuxer
  pStreamTimes->ptsBegin = 0; // earliest pts player can seek back
  pStreamTimes->ptsEnd = static_cast<int64_t>(difftime(endTs, begTs)) * DVD_TIME_BASE;
  return PVR_ERROR_NO_ERROR;
}

bool PVRClientMythTV::OpenRecordedStream(const PVR_RECORDING &recording)
{
  if (!m_control || !m_eventHandler)
    return false;
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: title: %s, ID: %s, duration: %d", __FUNCTION__, recording.strTitle, recording.strRecordingId, recording.iDuration);

  // Begin critical section
  Myth::OS::CLockGuard lock(*m_lock);
  if (m_recordingStream)
  {
    XBMC->Log(LOG_NOTICE, "%s: Recorded stream is busy", __FUNCTION__);
    return false;
  }

  MythProgramInfo prog;
  {
    Myth::OS::CLockGuard lock(*m_recordingsLock);
    ProgramInfoMap::iterator it = m_recordings.find(recording.strRecordingId);
    if (it == m_recordings.end())
    {
      XBMC->Log(LOG_ERROR, "%s: Recording %s does not exist", __FUNCTION__, recording.strRecordingId);
      return false;
    }
    prog = it->second;
  }

  if (prog.HostName() == m_control->GetServerHostName())
  {
    // Request the stream from our master using the opened event handler.
    m_recordingStream = new Myth::RecordingPlayback(*m_eventHandler);
    if (!m_recordingStream->IsOpen())
      XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30302)); // MythTV backend unavailable
    else if (m_recordingStream->OpenTransfer(prog.GetPtr()))
    {
      m_recordingStreamInfo = prog;
      if (g_bExtraDebug)
        XBMC->Log(LOG_DEBUG, "%s: Done", __FUNCTION__);
      // Fill AV info for later use
      FillRecordingAVInfo(prog, m_recordingStream);
      return true;
    }
  }
  else
  {
    // MasterBackendOverride setting will guide us to choose best method
    // If checked we will try to connect master failover slave
    Myth::SettingPtr mbo = m_control->GetSetting("MasterBackendOverride", false);
    if (mbo && mbo->value == "1")
    {
      XBMC->Log(LOG_INFO, "%s: Option 'MasterBackendOverride' is enabled", __FUNCTION__);
      m_recordingStream = new Myth::RecordingPlayback(*m_eventHandler);
      if (m_recordingStream->IsOpen() && m_recordingStream->OpenTransfer(prog.GetPtr()))
      {
        m_recordingStreamInfo = prog;
        if (g_bExtraDebug)
          XBMC->Log(LOG_DEBUG, "%s: Done", __FUNCTION__);
        // Fill AV info for later use
        FillRecordingAVInfo(prog, m_recordingStream);
        return true;
      }
      SAFE_DELETE(m_recordingStream);
      XBMC->Log(LOG_NOTICE, "%s: Failed to open recorded stream from master backend", __FUNCTION__);
      XBMC->Log(LOG_NOTICE, "%s: You should uncheck option 'MasterBackendOverride' from MythTV setup", __FUNCTION__);
    }
    // Query backend server IP
    std::string backend_addr(m_control->GetBackendServerIP6(prog.HostName()));
    if (backend_addr.empty())
      backend_addr = m_control->GetBackendServerIP(prog.HostName());
    if (backend_addr.empty())
      backend_addr = prog.HostName();
    // Query backend server port
    unsigned backend_port(m_control->GetBackendServerPort(prog.HostName()));
    if (!backend_port)
      backend_port = (unsigned)g_iProtoPort;
    // Request the stream from slave host. A dedicated event handler will be opened.
    XBMC->Log(LOG_INFO, "%s: Connect to remote backend %s:%u", __FUNCTION__, backend_addr.c_str(), backend_port);
    m_recordingStream = new Myth::RecordingPlayback(backend_addr, backend_port);
    if (!m_recordingStream->IsOpen())
      XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30302)); // MythTV backend unavailable
    else if (m_recordingStream->OpenTransfer(prog.GetPtr()))
    {
      m_recordingStreamInfo = prog;
      if (g_bExtraDebug)
        XBMC->Log(LOG_DEBUG, "%s: Done", __FUNCTION__);
      // Fill AV info for later use
      FillRecordingAVInfo(prog, m_recordingStream);
      return true;
    }
  }

  SAFE_DELETE(m_recordingStream);
  XBMC->Log(LOG_ERROR,"%s: Failed to open recorded stream", __FUNCTION__);
  return false;
}

void PVRClientMythTV::CloseRecordedStream()
{
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  // Begin critical section
  Myth::OS::CLockGuard lock(*m_lock);

  // Destroy my stream
  SAFE_DELETE(m_recordingStream);
  // Reset my info
  m_recordingStreamInfo = MythProgramInfo();

  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: Done", __FUNCTION__);
}

int PVRClientMythTV::ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  // Keep unlocked
  return (m_recordingStream ? m_recordingStream->Read(pBuffer, iBufferSize) : -1);
}

long long PVRClientMythTV::SeekRecordedStream(long long iPosition, int iWhence)
{
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: pos: %lld, whence: %d", __FUNCTION__, iPosition, iWhence);

  if (!m_recordingStream)
    return -1;

  Myth::WHENCE_t whence;
  switch (iWhence)
  {
  case SEEK_SET:
    whence = Myth::WHENCE_SET;
    break;
  case SEEK_CUR:
    whence = Myth::WHENCE_CUR;
    break;
  case SEEK_END:
    whence = Myth::WHENCE_END;
    break;
  default:
    return -1;
  }

  long long retval = (long long) m_recordingStream->Seek((int64_t)iPosition, whence);
  // PVR API needs zero when seeking beyond the end
  if (retval < 0 && m_recordingStream->GetSize() > 0)
    retval = 0;

  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: Done - position: %lld", __FUNCTION__, retval);

  return retval;
}

long long PVRClientMythTV::LengthRecordedStream()
{
  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  if (!m_recordingStream)
    return -1;

  long long retval = (long long) m_recordingStream->GetSize();

  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, "%s: Done - duration: %lld", __FUNCTION__, retval);

  return retval;
}

PVR_ERROR PVRClientMythTV::CallMenuHook(const PVR_MENUHOOK &menuhook, const PVR_MENUHOOK_DATA &item)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;

  if (menuhook.iHookId == MENUHOOK_REC_DELETE_AND_RERECORD && item.cat == PVR_MENUHOOK_RECORDING) {
    return DeleteAndForgetRecording(item.data.recording);
  }

  if (menuhook.iHookId == MENUHOOK_KEEP_RECORDING && item.cat == PVR_MENUHOOK_RECORDING)
  {
    Myth::OS::CLockGuard lock(*m_recordingsLock);
    ProgramInfoMap::iterator it = m_recordings.find(item.data.recording.strRecordingId);
    if (it == m_recordings.end())
    {
      XBMC->Log(LOG_ERROR,"%s: Recording not found", __FUNCTION__);
      return PVR_ERROR_INVALID_PARAMETERS;
    }

    // If recording is current live show then keep it and set live recorder
    if (IsMyLiveRecording(it->second))
    {
      Myth::OS::CLockGuard lock(*m_lock);
      if (m_liveStream && m_liveStream->KeepLiveRecording(true))
        return PVR_ERROR_NO_ERROR;
      return PVR_ERROR_FAILED;
    }
    // Else keep recording
    else
    {
      if (m_control->UndeleteRecording(*(it->second.GetPtr())))
      {
        std::string info = XBMC->GetLocalizedString(menuhook.iLocalizedStringId);
        info.append(": ").append(it->second.Title());
        XBMC->QueueNotification(QUEUE_INFO, info.c_str());
        return PVR_ERROR_NO_ERROR;
      }
    }
    return PVR_ERROR_FAILED;
  }

  if (menuhook.iHookId == MENUHOOK_INFO_RECORDING && item.cat == PVR_MENUHOOK_RECORDING)
  {
    MythProgramInfo pinfo;
    {
      Myth::OS::CLockGuard lock(*m_recordingsLock);
      ProgramInfoMap::iterator it = m_recordings.find(item.data.recording.strRecordingId);
      if (it == m_recordings.end())
      {
        XBMC->Log(LOG_ERROR,"%s: Recording not found", __FUNCTION__);
        return PVR_ERROR_INVALID_PARAMETERS;
      }
      pinfo = it->second;
    }
    if (pinfo.IsNull())
      return PVR_ERROR_REJECTED;

    const unsigned sz = 12;
    std::string items[sz];
    const char* entries[sz];
    items[0].append("Status : [COLOR white]")
            .append(Myth::RecStatusToString(m_control->CheckService(), pinfo.Status())).append("[/COLOR]");
    items[1].append("RecordID : [COLOR white]")
            .append(Myth::IdToString(pinfo.RecordID())).append("[/COLOR]");
    items[2].append("StartTime : [COLOR white]")
            .append(Myth::TimeToString(pinfo.RecordingStartTime())).append("[/COLOR]");
    items[3].append("EndTime : [COLOR white]")
            .append(Myth::TimeToString(pinfo.RecordingEndTime())).append("[/COLOR]");
    items[4].append("ChannelName : [COLOR white]")
            .append(pinfo.ChannelName()).append("[/COLOR]");
    items[5].append("FileName : [COLOR white]")
            .append(pinfo.FileName()).append("[/COLOR]");
    items[6].append("StorageGroup : [COLOR white]")
            .append(pinfo.StorageGroup()).append("[/COLOR]");
    items[7].append("HostName : [COLOR white]")
            .append(pinfo.HostName()).append("[/COLOR]");

    items[8].append("ProgramFlags : [COLOR white]");
    unsigned pf = pinfo.GetPtr()->programFlags;
    items[8].append((pf & 0x00001) ? "0 " : "");
    items[8].append((pf & 0x00002) ? "1 " : "");
    items[8].append((pf & 0x00004) ? "2 " : "");
    items[8].append((pf & 0x00008) ? "3 " : "");
    items[8].append((pf & 0x00010) ? "4 " : "");
    items[8].append((pf & 0x00020) ? "5 " : "");
    items[8].append((pf & 0x00040) ? "6 " : "");
    items[8].append((pf & 0x00080) ? "7 " : "");
    items[8].append((pf & 0x00100) ? "8 " : "");
    items[8].append((pf & 0x00200) ? "9 " : "");
    items[8].append((pf & 0x00400) ? "A " : "");
    items[8].append((pf & 0x00800) ? "B " : "");
    items[8].append((pf & 0x01000) ? "C " : "");
    items[8].append((pf & 0x02000) ? "D " : "");
    items[8].append((pf & 0x04000) ? "E " : "");
    items[8].append((pf & 0x08000) ? "F " : "");
    items[8].append((pf & 0x10000) ? "G " : "");
    items[8].append((pf & 0x20000) ? "H " : "");
    items[8].append("[/COLOR]");

    items[9].append("AudioProps : [COLOR white]");
    unsigned ap = pinfo.GetPtr()->audioProps;
    items[9].append((ap & 0x01) ? "0 " : "");
    items[9].append((ap & 0x02) ? "1 " : "");
    items[9].append((ap & 0x04) ? "2 " : "");
    items[9].append((ap & 0x08) ? "3 " : "");
    items[9].append((ap & 0x10) ? "4 " : "");
    items[9].append((ap & 0x20) ? "5 " : "");
    items[9].append("[/COLOR]");

    items[10].append("VideoProps : [COLOR white]");
    unsigned vp = pinfo.GetPtr()->videoProps;
    items[10].append((vp & 0x01) ? "0 " : "");
    items[10].append((vp & 0x02) ? "1 " : "");
    items[10].append((vp & 0x04) ? "2 " : "");
    items[10].append((vp & 0x08) ? "3 " : "");
    items[10].append((vp & 0x10) ? "4 " : "");
    items[10].append((vp & 0x20) ? "5 " : "");
    items[10].append((vp & 0x40) ? "6 " : "");
    items[10].append("[/COLOR]");

    items[11].append("FrameRate : [COLOR white]");
    if (pinfo.GetPropsVideoFrameRate() > 0.0)
      items[11].append(std::to_string(pinfo.GetPropsVideoFrameRate()));
    items[11].append("[/COLOR]");

    for (unsigned i = 0; i < sz; ++i)
      entries[i] = items[i].c_str();
    GUI->Dialog_Select(item.data.recording.strTitle, entries, sz);

    return PVR_ERROR_NO_ERROR;
  }

  if (menuhook.category == PVR_MENUHOOK_TIMER)
  {
    if (menuhook.iHookId == MENUHOOK_TIMER_BACKEND_INFO && m_scheduleManager && item.cat == PVR_MENUHOOK_TIMER)
    {
      MythScheduledPtr prog = m_scheduleManager->FindUpComingByIndex(item.data.timer.iClientIndex);
      if (!prog)
      {
        MythScheduleList progs = m_scheduleManager->FindUpComingByRuleId(item.data.timer.iClientIndex);
        if (progs.end() != progs.begin())
          prog = progs.begin()->second;
      }
      if (prog)
      {
        const unsigned sz = 4;
        std::string items[sz];
        const char* entries[sz];
        items[0].append("Status : [COLOR white]")
                .append(Myth::RecStatusToString(m_control->CheckService(), prog->Status())).append("[/COLOR]");
        items[1].append("RecordID : [COLOR white]")
                .append(Myth::IdToString(prog->RecordID())).append("[/COLOR]");
        items[2].append("StartTime : [COLOR white]")
                .append(Myth::TimeToString(prog->RecordingStartTime())).append("[/COLOR]");
        items[3].append("EndTime : [COLOR white]")
                .append(Myth::TimeToString(prog->RecordingEndTime())).append("[/COLOR]");
        for (unsigned i = 0; i < sz; ++i)
          entries[i] = items[i].c_str();
        GUI->Dialog_Select(item.data.timer.strTitle, entries, sz);
      }
      return PVR_ERROR_NO_ERROR;
    }
    else if (menuhook.iHookId == MENUHOOK_SHOW_HIDE_NOT_RECORDING && m_scheduleManager)
    {
      bool flag = m_scheduleManager->ToggleShowNotRecording();
      HandleScheduleChange();
      std::string info = (flag ? XBMC->GetLocalizedString(30310) : XBMC->GetLocalizedString(30311)); //Enabled / Disabled
      info += ": ";
      info += XBMC->GetLocalizedString(30421); //Show/hide rules with status 'Not Recording'
      XBMC->QueueNotification(QUEUE_INFO, info.c_str());
      return PVR_ERROR_NO_ERROR;
    }
  }

  if (menuhook.category == PVR_MENUHOOK_CHANNEL)
  {
    if (menuhook.iHookId == MENUHOOK_TRIGGER_CHANNEL_UPDATE)
    {
      PVR->TriggerChannelUpdate();
      return PVR_ERROR_NO_ERROR;
    }
  }

  if (menuhook.category == PVR_MENUHOOK_EPG && item.cat == PVR_MENUHOOK_EPG)
  {
    time_t attime;
    unsigned int chanid;
    MythEPGInfo::BreakBroadcastID(item.data.iEpgUid, &chanid, &attime);
    MythEPGInfo epgInfo;
    Myth::ProgramMapPtr epg = m_control->GetProgramGuide(chanid, attime, attime);
    Myth::ProgramMap::reverse_iterator epgit = epg->rbegin(); // Get last found
    if (epgit != epg->rend())
    {
      epgInfo = MythEPGInfo(epgit->second);
      if (g_bExtraDebug)
        XBMC->Log(LOG_DEBUG, "%s: Found EPG program (%d) chanid: %u attime: %lu", __FUNCTION__, item.data.iEpgUid, chanid, attime);

      if (menuhook.iHookId == MENUHOOK_INFO_EPG)
      {
        const unsigned sz = 8;
        std::string items[sz];
        const char* entries[sz];
        items[0] = "BID " + std::to_string((unsigned)item.data.iEpgUid);
        items[1] = Myth::TimeToString(epgInfo.StartTime());
        items[2] = Myth::TimeToString(epgInfo.EndTime());
        items[3] = epgInfo.ChannelName();
        items[4] = epgInfo.ChannelNumber();
        items[5] = epgInfo.Category();
        items[6] = epgInfo.CategoryType();
        items[7] = epgInfo.SeriesID();

        for (unsigned i = 0; i < sz; ++i)
          entries[i] = items[i].c_str();
        GUI->Dialog_Select(epgInfo.Title().c_str(), entries, sz);

        return PVR_ERROR_NO_ERROR;
      }

    }
    else
    {
      XBMC->QueueNotification(QUEUE_WARNING, XBMC->GetLocalizedString(30312));
      XBMC->Log(LOG_DEBUG, "%s: EPG program not found (%d) chanid: %u attime: %lu", __FUNCTION__, item.data.iEpgUid, chanid, attime);
      return PVR_ERROR_INVALID_PARAMETERS;
    }
    return PVR_ERROR_FAILED;
  }

  return PVR_ERROR_NOT_IMPLEMENTED;
}

bool PVRClientMythTV::GetLiveTVPriority()
{
  if (m_control)
  {
    Myth::SettingPtr setting = m_control->GetSetting("LiveTVPriority", true);
    return ((setting && setting->value.compare("1") == 0) ? true : false);
  }
  return false;
}

void PVRClientMythTV::SetLiveTVPriority(bool enabled)
{
  if (m_control)
  {
    std::string value = (enabled ? "1" : "0");
    m_control->PutSetting("LiveTVPriority", value, true);
  }
}

void PVRClientMythTV::BlockBackendShutdown()
{
  if (m_control)
    m_control->BlockShutdown();
}

void PVRClientMythTV::AllowBackendShutdown()
{
  if (m_control)
    m_control->AllowShutdown();
}

std::string PVRClientMythTV::MakeProgramTitle(const std::string& title, const std::string& subtitle)
{
  // Must contain the original title at the begining
  std::string epgtitle;
  if (subtitle.empty())
    epgtitle = title;
  else
    epgtitle = title + " (" + subtitle + ")";
  return epgtitle;
}

void PVRClientMythTV::FillRecordingAVInfo(MythProgramInfo& programInfo, Myth::Stream *stream)
{
  AVInfo info(stream);
  AVInfo::STREAM_AVINFO mInfo;
  if (info.GetMainStream(&mInfo))
  {
    // Set video frame rate
    if (mInfo.stream_info.fps_scale > 0)
    {
      float fps = 0;
      switch(mInfo.stream_type)
      {
        case TSDemux::STREAM_TYPE_VIDEO_H264:
          fps = (float)(mInfo.stream_info.fps_rate) / (mInfo.stream_info.fps_scale * (mInfo.stream_info.interlaced ? 2 : 1));
          break;
        default:
          fps = (float)(mInfo.stream_info.fps_rate) / mInfo.stream_info.fps_scale;
      }
      programInfo.SetPropsVideoFrameRate(fps);
    }
    // Set video aspec
    programInfo.SetPropsVideoAspec(mInfo.stream_info.aspect);
  }
}

time_t PVRClientMythTV::GetRecordingTime(time_t airtt, time_t recordingtt)
{
  if (!g_bUseAirdate || airtt == 0)
    return recordingtt;

  /* Airdate is usually a Date, not a time.  So we include the time part from
  the recording time in order to give the reported time something other than
  12AM.  If two shows are recorded on the same day, typically they are aired
  in the correct time order.  Combining airdate and recording time gives us
  the best possible time to report to the user to allow them to sort by
  datetime to see the correct episode ordering. */
  struct tm airtm, rectm;
  localtime_r(&airtt, &airtm);
  localtime_r(&recordingtt, &rectm);
  airtm.tm_hour = rectm.tm_hour;
  airtm.tm_min = rectm.tm_min;
  airtm.tm_sec = rectm.tm_sec;
  if (airtm.tm_yday == 0)
  {
    airtm.tm_mday = rectm.tm_mday;
    airtm.tm_mon = rectm.tm_mon;
  }
  return mktime(&airtm);
}
