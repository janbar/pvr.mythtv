/*
 *  Copyright (C) 2013-2021 Jean-Luc Barrière
 *  Copyright (C) 2005-2014 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "pvrclient-mythtv.h"
#include "tools.h"
#include "avinfo.h"
#include "filestreaming.h"
#include "taskhandler.h"
#include "private/os/threads/mutex.h"

#include <kodi/General.h>
#include <kodi/Network.h>
#include <kodi/gui/dialogs/Select.h>
#include <kodi/gui/dialogs/YesNo.h>

#include <time.h>
#include <set>
#include <cassert>

#define SEEK_POSSIBLE 0x10 ///< flag used to check if protocol allows seeks

PVRClientMythTV::PVRClientMythTV(KODI_HANDLE instance, const std::string& version)
: kodi::addon::CInstancePVRClient(instance, version)
, m_connectionError(CONN_ERROR_NOT_CONNECTED)
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
  // Create menu hooks
  kodi::Log(ADDON_LOG_DEBUG, "Creating menu hooks...");

  kodi::addon::CInstancePVRClient::AddMenuHook(kodi::addon::PVRMenuhook(MENUHOOK_REC_DELETE_AND_RERECORD, 30411, PVR_MENUHOOK_RECORDING));
  kodi::addon::CInstancePVRClient::AddMenuHook(kodi::addon::PVRMenuhook(MENUHOOK_KEEP_RECORDING, 30412, PVR_MENUHOOK_RECORDING));
  kodi::addon::CInstancePVRClient::AddMenuHook(kodi::addon::PVRMenuhook(MENUHOOK_INFO_RECORDING, 30425, PVR_MENUHOOK_RECORDING));
  kodi::addon::CInstancePVRClient::AddMenuHook(kodi::addon::PVRMenuhook(MENUHOOK_TIMER_BACKEND_INFO, 30424, PVR_MENUHOOK_TIMER));
  kodi::addon::CInstancePVRClient::AddMenuHook(kodi::addon::PVRMenuhook(MENUHOOK_SHOW_HIDE_NOT_RECORDING, 30421, PVR_MENUHOOK_TIMER));
  kodi::addon::CInstancePVRClient::AddMenuHook(kodi::addon::PVRMenuhook(MENUHOOK_TRIGGER_CHANNEL_UPDATE, 30423, PVR_MENUHOOK_CHANNEL));
  kodi::addon::CInstancePVRClient::AddMenuHook(kodi::addon::PVRMenuhook(MENUHOOK_INFO_EPG, 30426, PVR_MENUHOOK_EPG));

  kodi::Log(ADDON_LOG_DEBUG, "Creating menu hooks...done");
}

PVRClientMythTV::~PVRClientMythTV()
{
  delete m_todo;
  delete m_dummyStream;
  delete m_liveStream;
  delete m_recordingStream;
  delete m_artworksManager;
  delete m_scheduleManager;
  delete m_eventHandler;
  delete m_control;
  delete m_recordingsLock;
  delete m_channelsLock;
  delete m_lock;
}

static void Log(int level, char *msg)
{
  if (msg && level != MYTH_DBG_NONE)
  {
    bool doLog = true; //CMythSettings::GetExtraDebug();
    AddonLog loglevel = ADDON_LOG_DEBUG;
    switch (level)
    {
    case MYTH_DBG_ERROR:
      loglevel = ADDON_LOG_ERROR;
      doLog = true;
      break;
    case MYTH_DBG_WARN:
      loglevel = ADDON_LOG_INFO;
      doLog = true;
      break;
    case MYTH_DBG_INFO:
      loglevel = ADDON_LOG_INFO;
      doLog = true;
      break;
    case MYTH_DBG_DEBUG:
    case MYTH_DBG_PROTO:
    case MYTH_DBG_ALL:
      loglevel = ADDON_LOG_DEBUG;
      break;
    }
    if (doLog)
      kodi::Log(loglevel, "%s", msg);
  }
}

void PVRClientMythTV::SetDebug(bool silent /*= false*/)
{
  // Setup libcppmyth logging
  if (CMythSettings::GetExtraDebug())
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
  Myth::Control *control = new Myth::Control(CMythSettings::GetMythHostname(), CMythSettings::GetProtoPort(), CMythSettings::GetWSApiPort(), CMythSettings::GetWSSecurityPin(), true);
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
    kodi::Log(ADDON_LOG_INFO, "Failed to connect to MythTV backend on %s:%d", CMythSettings::GetMythHostname().c_str(), CMythSettings::GetProtoPort());
    // Try wake up for the next attempt
    if (!CMythSettings::GetMythHostEther().empty())
      kodi::network::WakeOnLan(CMythSettings::GetMythHostEther().c_str());
    return false;
  }
  if (!control->CheckService())
  {
    m_connectionError = CONN_ERROR_API_UNAVAILABLE;
    delete control;
    kodi::Log(ADDON_LOG_INFO,"Failed to connect to MythTV backend on %s:%d with pin %s", CMythSettings::GetMythHostname().c_str(), CMythSettings::GetWSApiPort(), CMythSettings::GetWSSecurityPin().c_str());
    return false;
  }
  m_connectionError = CONN_ERROR_NO_ERROR;
  m_control = control;
  SetDebug(false);

  // Create event handler and subscription as needed
  unsigned subid = 0;
  m_eventHandler = new Myth::EventHandler(CMythSettings::GetMythHostname(), CMythSettings::GetProtoPort());
  subid = m_eventHandler->CreateSubscription(this);
  m_eventHandler->SubscribeForEvent(subid, Myth::EVENT_HANDLER_STATUS);
  m_eventHandler->SubscribeForEvent(subid, Myth::EVENT_HANDLER_TIMER);
  m_eventHandler->SubscribeForEvent(subid, Myth::EVENT_ASK_RECORDING);
  m_eventHandler->SubscribeForEvent(subid, Myth::EVENT_RECORDING_LIST_CHANGE);

  // Create schedule manager and new subscription handled by dedicated thread
  m_scheduleManager = new MythScheduleManager(CMythSettings::GetMythHostname(), CMythSettings::GetProtoPort(), CMythSettings::GetWSApiPort(), CMythSettings::GetWSSecurityPin());
  subid = m_eventHandler->CreateSubscription(this);
  m_eventHandler->SubscribeForEvent(subid, Myth::EVENT_SCHEDULE_CHANGE);

  // Create artwork manager
  m_artworksManager = new ArtworkManager(CMythSettings::GetMythHostname(), CMythSettings::GetWSApiPort(), CMythSettings::GetWSSecurityPin());

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

PVR_ERROR PVRClientMythTV::GetCapabilities(kodi::addon::PVRCapabilities& capabilities)
{
  unsigned version = GetBackendAPIVersion();
  capabilities.SetSupportsTV(CMythSettings::GetLiveTV());
  capabilities.SetSupportsRadio(CMythSettings::GetLiveTV());
  capabilities.SetSupportsChannelGroups(true);
  capabilities.SetSupportsChannelScan(false);
  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsTimers(true);

  capabilities.SetHandlesInputStream(true);
  capabilities.SetHandlesDemuxing(false);

  capabilities.SetSupportsRecordings(true);
  capabilities.SetSupportsRecordingsUndelete(true);
  capabilities.SetSupportsRecordingPlayCount((version < 80 ? false : true));
  capabilities.SetSupportsLastPlayedPosition((version < 88 || !CMythSettings::GetUseBackendBookmarks() ? false : true));
  capabilities.SetSupportsRecordingEdl(true);
  capabilities.SetSupportsRecordingsRename(false);
  capabilities.SetSupportsRecordingsLifetimeChange(false);
  capabilities.SetSupportsDescrambleInfo(false);
  capabilities.SetSupportsAsyncEPGTransfer(false);
  capabilities.SetSupportsRecordingSize(true);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::GetBackendName(std::string& name)
{
  if (m_control)
    name.append("MythTV (").append(m_control->GetServerHostName()).append(")");
  kodi::Log(ADDON_LOG_DEBUG, "%s: %s", __FUNCTION__, name.c_str());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::GetBackendVersion(std::string& version)
{
  if (m_control)
  {
    Myth::VersionPtr myVersion = m_control->GetVersion();
    version = myVersion->version;
  }
  kodi::Log(ADDON_LOG_DEBUG, "%s: %s", __FUNCTION__, version.c_str());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::GetBackendHostname(std::string& hostname)
{
  hostname = CMythSettings::GetMythHostname();
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR PVRClientMythTV::GetConnectionString(std::string& connection)
{
  connection.append("http://").append(CMythSettings::GetMythHostname()).append(":").append(Myth::IntToString(CMythSettings::GetWSApiPort()));
  kodi::Log(ADDON_LOG_DEBUG, "%s: %s", __FUNCTION__, connection.c_str());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::GetDriveSpace(uint64_t& iTotal, uint64_t& iUsed)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  int64_t total = 0, used = 0;
  if (m_control->QueryFreeSpaceSummary(&total, &used))
  {
    iTotal = (uint64_t)total;
    iUsed = (uint64_t)used;
    return PVR_ERROR_NO_ERROR;
  }
  return PVR_ERROR_UNKNOWN;
}

PVR_ERROR PVRClientMythTV::OnSystemSleep()
{
  kodi::Log(ADDON_LOG_INFO, "Received event: %s", __FUNCTION__);

  if (m_eventHandler)
    m_eventHandler->Stop();
  if (m_scheduleManager)
    m_scheduleManager->CloseControl();
  if (m_control)
    m_control->Close();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::OnSystemWake()
{
  kodi::Log(ADDON_LOG_INFO, "Received event: %s", __FUNCTION__);

  if (m_control)
    m_control->Open();
  if (m_scheduleManager)
    m_scheduleManager->OpenControl();
  if (m_eventHandler)
    m_eventHandler->Start();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::OnPowerSavingActivated()
{
  kodi::Log(ADDON_LOG_INFO, "Received event: %s", __FUNCTION__);

  if (CMythSettings::GetAllowMythShutdown() && m_control && m_control->IsOpen())
    AllowBackendShutdown();
  m_powerSaving = true;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::OnPowerSavingDeactivated()
{
  kodi::Log(ADDON_LOG_INFO, "Received event: %s", __FUNCTION__);

  // block shutdown if backend is connected
  if (CMythSettings::GetAllowMythShutdown() && m_control && m_control->IsOpen())
    BlockBackendShutdown();
  m_powerSaving = false;
  return PVR_ERROR_NO_ERROR;
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
        // notify the user when the screen is activated
        if (!m_powerSaving)
          kodi::QueueNotification(QUEUE_ERROR, "", kodi::GetLocalizedString(30302)); // Connection to MythTV backend lost
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
          // notify the user when the screen is activated
          if (!m_powerSaving)
            kodi::QueueNotification(QUEUE_INFO, "", kodi::GetLocalizedString(30303)); // Connection to MythTV restored
          // still in mode power saving I have to allow shutdown again
          if (m_powerSaving && CMythSettings::GetAllowMythShutdown())
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
        if (!m_powerSaving && !CMythSettings::GetMythHostEther().empty())
          kodi::network::WakeOnLan(CMythSettings::GetMythHostEther());
      }
      break;
    default:
      break;
  }
}

void PVRClientMythTV::HandleChannelChange()
{
  FillChannelsAndChannelGroups();
  kodi::addon::CInstancePVRClient::TriggerChannelUpdate();
  kodi::addon::CInstancePVRClient::TriggerChannelGroupsUpdate();
}

void PVRClientMythTV::HandleScheduleChange()
{
  if (!m_scheduleManager)
    return;
  m_scheduleManager->Update();
  kodi::addon::CInstancePVRClient::TriggerTimerUpdate();
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
      kodi::Log(ADDON_LOG_ERROR, "%s: Incorrect message: %d : %s", __FUNCTION__, i, msg.subject[i].c_str());
    return;
  }
  // The scheduled recording will hang in MythTV if ASK_RECORDING is just ignored.
  // - Stop recorder (and blocked for time until seconds)
  // - Skip the recording by sending CANCEL_NEXT_RECORDING(true)
  uint32_t cardid = Myth::StringToId(msg.subject[1]);
  int timeuntil = Myth::StringToInt(msg.subject[2]);
  int hasrec = Myth::StringToInt(msg.subject[3]);
  int haslater = Myth::StringToInt(msg.subject[4]);
  kodi::Log(ADDON_LOG_INFO, "%s: Event ASK_RECORDING: rec=%d timeuntil=%d hasrec=%d haslater=%d", __FUNCTION__,
          cardid, timeuntil, hasrec, haslater);

  std::string title;
  if (msg.program)
    title = msg.program->title;
  kodi::Log(ADDON_LOG_INFO, "%s: Event ASK_RECORDING: title=%s", __FUNCTION__, title.c_str());

  if (timeuntil >= 0 && cardid > 0 && m_liveStream && m_liveStream->GetCardId() == cardid)
  {
    if (CMythSettings::GetLiveTVConflictStrategy() == LIVETV_CONFLICT_STRATEGY_CANCELREC ||
      (CMythSettings::GetLiveTVConflictStrategy() == LIVETV_CONFLICT_STRATEGY_HASLATER && haslater))
    {
      kodi::QueueFormattedNotification(QUEUE_WARNING, kodi::GetLocalizedString(30307).c_str(), title.c_str()); // Canceling conflicting recording: %s
      m_control->CancelNextRecording((int)cardid, true);
    }
    else // LIVETV_CONFLICT_STRATEGY_STOPTV
    {
      kodi::QueueFormattedNotification(QUEUE_WARNING, kodi::GetLocalizedString(30308).c_str(), title.c_str()); // Stopping Live TV due to conflicting recording: %s
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
    if (CMythSettings::GetExtraDebug())
      kodi::Log(ADDON_LOG_DEBUG, "%s: Reload all recordings", __FUNCTION__);
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
        if (CMythSettings::GetExtraDebug())
          kodi::Log(ADDON_LOG_DEBUG, "%s: Add recording: %s", __FUNCTION__, prog.UID().c_str());
        // Add recording
        m_recordings.insert(std::pair<std::string, MythProgramInfo>(prog.UID().c_str(), prog));
        ++m_recordingChangePinCount;
      }
    }
    else
      kodi::Log(ADDON_LOG_ERROR, "%s: Add recording failed for %u %ld", __FUNCTION__, (unsigned)chanid, (long)startts);
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
        if (CMythSettings::GetExtraDebug())
          kodi::Log(ADDON_LOG_DEBUG, "%s: Add recording: %s", __FUNCTION__, prog.UID().c_str());
        // Add recording
        m_recordings.insert(std::pair<std::string, MythProgramInfo>(prog.UID().c_str(), prog));
        ++m_recordingChangePinCount;
      }
    }
    else
      kodi::Log(ADDON_LOG_ERROR, "%s: Add recording failed for %u", __FUNCTION__, (unsigned)recordedid);
  }
  else if (cs == 2 && msg.subject[1] == "UPDATE" && msg.program)
  {
    Myth::OS::CLockGuard lock(*m_recordingsLock);
    MythProgramInfo prog(msg.program);
    ProgramInfoMap::iterator it = m_recordings.find(prog.UID());
    if (it != m_recordings.end())
    {
      if (CMythSettings::GetExtraDebug())
        kodi::Log(ADDON_LOG_DEBUG, "%s: Update recording: %s", __FUNCTION__, prog.UID().c_str());
      if (m_control->RefreshRecordedArtwork(*(msg.program)) && CMythSettings::GetExtraDebug())
        kodi::Log(ADDON_LOG_DEBUG, "%s: artwork found for %s", __FUNCTION__, prog.UID().c_str());
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
        if (CMythSettings::GetExtraDebug())
          kodi::Log(ADDON_LOG_DEBUG, "%s: Delete recording: %s", __FUNCTION__, prog.UID().c_str());
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
        if (CMythSettings::GetExtraDebug())
          kodi::Log(ADDON_LOG_DEBUG, "%s: Delete recording: %s", __FUNCTION__, prog.UID().c_str());
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
  if (kodi::gui::dialogs::YesNo::ShowAndGetInput(kodi::GetLocalizedString(122),
          kodi::GetLocalizedString(19112), "", dispTitle,
          "", kodi::GetLocalizedString(117)))
  {
    if (m_control->DeleteRecording(*(prog.GetPtr())))
      kodi::Log(ADDON_LOG_DEBUG, "%s: Deleted recording %s", __FUNCTION__, prog.UID().c_str());
    else
      kodi::Log(ADDON_LOG_ERROR, "%s: Failed to delete recording %s", __FUNCTION__, prog.UID().c_str());
  }
}

void PVRClientMythTV::RunHouseKeeping()
{
  if (!m_control || !m_eventHandler)
    return;
  // It is time to work
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  // Reconnect handler when backend connection has hanging during last period
  if (!m_hang && m_control->HasHanging())
  {
    kodi::Log(ADDON_LOG_INFO, "%s: Ask to refresh handler connection since control connection has hanging", __FUNCTION__);
    m_eventHandler->Reset();
    m_control->CleanHanging();
  }
  if (m_recordingChangePinCount)
  {
    Myth::OS::CLockGuard lock(*m_recordingsLock);
    m_recordingsAmountChange = true; // Need count recording amount
    m_deletedRecAmountChange = true; // Need count of deleted amount
    lock.Unlock();
    kodi::addon::CInstancePVRClient::TriggerRecordingUpdate();
    lock.Lock();
    m_recordingChangePinCount = 0;
  }
}

PVR_ERROR PVRClientMythTV::GetEPGForChannel(int channelUid, time_t start, time_t end, kodi::addon::PVREPGTagsResultSet& results)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG,"%s: start: %ld, end: %ld, chanid: %u", __FUNCTION__, (long)start, (long)end, channelUid);


  Myth::ProgramMapPtr EPG = m_control->GetProgramGuide(channelUid, start, end);
  // Transfer EPG for the given channel
  for (Myth::ProgramMap::reverse_iterator it = EPG->rbegin(); it != EPG->rend(); ++it)
  {
    time_t startTime = it->first;
    time_t endTime = it->second->endTime;
    // Reject bad entry
    if (endTime <= startTime)
      continue;

    kodi::addon::PVREPGTag tag;

    tag.SetStartTime(startTime);
    tag.SetEndTime(endTime);
    tag.SetTitle(it->second->title);
    tag.SetPlot(it->second->description);
    tag.SetGenreDescription(it->second->category);
    tag.SetUniqueBroadcastId(MythEPGInfo::MakeBroadcastID(it->second->channel.chanId, startTime));
    tag.SetUniqueChannelId(channelUid);
    int genre = m_categories.Category(it->second->category);
    tag.SetGenreSubType(genre & 0x0F);
    tag.SetGenreType(genre & 0xF0);
    tag.SetEpisodeName(it->second->subTitle);
    tag.SetIconPath("");
    tag.SetPlotOutline("");
    tag.SetFirstAired(it->second->airdate);
    // Kodi have -1 to not use, but cppmyth it bring 0
    if (it->second->episode > 0 || it->second->season > 0)
    {
      tag.SetSeriesNumber((int)it->second->season);
      tag.SetEpisodeNumber((int)it->second->episode);
    }
    tag.SetEpisodePartNumber(EPG_TAG_INVALID_SERIES_EPISODE);
    tag.SetParentalRating(0);
    tag.SetStarRating(std::stoi(it->second->stars));
    tag.SetOriginalTitle("");
    tag.SetCast("");
    tag.SetDirector("");
    tag.SetWriter("");
    tag.SetYear(0);
    tag.SetIMDBNumber(it->second->inetref);
    if (!it->second->seriesId.empty())
      tag.SetFlags(EPG_TAG_FLAG_IS_SERIES);
    else
      tag.SetFlags(EPG_TAG_FLAG_UNDEFINED);

    results.Add(tag);
  }

  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: Done", __FUNCTION__);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::GetChannelsAmount(int& amount)
{
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_channelsLock);
  amount = m_PVRChannels.size();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: radio: %s", __FUNCTION__, (radio ? "true" : "false"));

  Myth::OS::CLockGuard lock(*m_channelsLock);

  // Load channels list
  if (m_PVRChannels.empty())
    FillChannelsAndChannelGroups();
  // Transfer channels of the requested type (radio / tv)
  for (PVRChannelList::const_iterator it = m_PVRChannels.begin(); it != m_PVRChannels.end(); ++it)
  {
    if (it->bIsRadio == radio)
    {
      ChannelIdMap::const_iterator itm = m_channelsById.find(it->iUniqueId);
      if (itm != m_channelsById.end() && !itm->second.IsNull())
      {
        kodi::addon::PVRChannel tag;

        tag.SetUniqueId(itm->first);
        tag.SetChannelNumber(itm->second.NumberMajor());
        tag.SetSubChannelNumber(itm->second.NumberMinor());
        tag.SetChannelName(itm->second.Name());
        tag.SetIsHidden(!itm->second.Visible());
        tag.SetIsRadio(itm->second.IsRadio());

        if (m_artworksManager)
          tag.SetIconPath(m_artworksManager->GetChannelIconPath(itm->second));
        else
          tag.SetIconPath("");

        // Unimplemented
        tag.SetMimeType("");
        tag.SetEncryptionSystem(0);

        results.Add(tag);
      }
    }
  }

  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: Done", __FUNCTION__);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::GetChannelGroupsAmount(int& amount)
{
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_channelsLock);
  amount = m_PVRChannelGroups.size();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: radio: %s", __FUNCTION__, (radio ? "true" : "false"));

  Myth::OS::CLockGuard lock(*m_channelsLock);

  // Transfer channel groups of the given type (radio / tv)
  for (PVRChannelGroupMap::iterator itg = m_PVRChannelGroups.begin(); itg != m_PVRChannelGroups.end(); ++itg)
  {
    kodi::addon::PVRChannelGroup tag;

    tag.SetGroupName(itg->first);
    tag.SetIsRadio(radio);
    tag.SetPosition(0);

    // Only add the group if we have at least one channel of the correct type
    for (PVRChannelList::const_iterator itc = itg->second.begin(); itc != itg->second.end(); ++itc)
    {
      if (itc->bIsRadio == radio)
      {
        results.Add(tag);
        break;
      }
    }
  }

  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: Done", __FUNCTION__);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group, kodi::addon::PVRChannelGroupMembersResultSet& results)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: group: %s", __FUNCTION__, group.GetGroupName().c_str());

  Myth::OS::CLockGuard lock(*m_channelsLock);

  PVRChannelGroupMap::iterator itg = m_PVRChannelGroups.find(group.GetGroupName());
  if (itg == m_PVRChannelGroups.end())
  {
    kodi::Log(ADDON_LOG_ERROR,"%s: Channel group not found", __FUNCTION__);
    return PVR_ERROR_INVALID_PARAMETERS;
  }

  // Transfer the channel group members for the requested group
  for (PVRChannelList::const_iterator itc = itg->second.begin(); itc != itg->second.end(); ++itc)
  {
    if (itc->bIsRadio == group.GetIsRadio())
    {
      kodi::addon::PVRChannelGroupMember tag;
      tag.SetChannelNumber(itc->iChannelNumber);
      tag.SetSubChannelNumber(itc->iSubChannelNumber);
      tag.SetChannelUniqueId(itc->iUniqueId);
      tag.SetGroupName(group.GetGroupName());
      results.Add(tag);
    }
  }

  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: Done", __FUNCTION__);

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
  kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

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
        if (CMythSettings::GetExtraDebug())
          kodi::Log(ADDON_LOG_DEBUG, "%s: skipping channel: %d", __FUNCTION__, chanid);
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

  kodi::Log(ADDON_LOG_DEBUG, "%s: Loaded %d channel(s) %d group(s)", __FUNCTION__, count, (unsigned)m_PVRChannelGroups.size());
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

PVR_ERROR PVRClientMythTV::GetRecordingsAmount(bool deleted, int& amount)
{
  if (deleted)
    amount = GetDeletedRecordingsAmount();
  else
    amount = GetRecordingsAmount();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results)
{
  if (deleted)
    return GetDeletedRecordings(results);
  return GetRecordings(results);
}

int PVRClientMythTV::GetRecordingsAmount()
{
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  if (m_recordingsAmountChange)
  {
    int res = 0;
    Myth::OS::CLockGuard lock(*m_recordingsLock);
    for (ProgramInfoMap::iterator it = m_recordings.begin(); it != m_recordings.end(); ++it)
    {
      if (!it->second.IsNull() && it->second.IsVisible() && (CMythSettings::GetLiveTVRecordings() || !it->second.IsLiveTV()))
        res++;
    }
    m_recordingsAmount = res;
    m_recordingsAmountChange = false;
    kodi::Log(ADDON_LOG_DEBUG, "%s: count %d", __FUNCTION__, res);
  }
  return m_recordingsAmount;
}

PVR_ERROR PVRClientMythTV::GetRecordings(kodi::addon::PVRRecordingsResultSet& results)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);
  unsigned schemaVersion = m_control->GetVersion()->schema;

  Myth::OS::CLockGuard lock(*m_recordingsLock);

  // Setup series
  if (CMythSettings::GetGroupRecordings() == GROUP_RECORDINGS_ONLY_FOR_SERIES)
  {
    typedef std::map<std::pair<std::string, std::string>, ProgramInfoMap::iterator::pointer> TitlesMap;
    TitlesMap titles;
    for (ProgramInfoMap::iterator it = m_recordings.begin(); it != m_recordings.end(); ++it)
    {
      if (!it->second.IsNull() && it->second.IsVisible() && (CMythSettings::GetLiveTVRecordings() || !it->second.IsLiveTV()))
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
    if (!it->second.IsNull() && it->second.IsVisible() && (CMythSettings::GetLiveTVRecordings() || !it->second.IsLiveTV()))
    {
      kodi::addon::PVRRecording tag;

      tag.SetIsDeleted(false);
      time_t airTime = Myth::StringToTime(it->second.Airdate());

      tag.SetRecordingTime(GetRecordingTime(airTime, it->second.RecordingStartTime()));
      tag.SetDuration(it->second.Duration());
      tag.SetPlayCount(it->second.IsWatched() ? 1 : 0);
      tag.SetLastPlayedPosition(it->second.HasBookmark() ? 1 : 0);

      std::string id = it->second.UID();

      std::string str; // a temporary string to build formating label
      std::string title(it->second.Title());
      if (it->second.IsDamaged(schemaVersion) && !CMythSettings::GetDamagedColor().empty())
      {
        str.assign(title);
        title.assign("[COLOR ").append(CMythSettings::GetDamagedColor()).append("]").append(str).append("[/COLOR]");
      }

      tag.SetRecordingId(id);
      tag.SetTitle(title);
      tag.SetEpisodeName(it->second.Subtitle());
      if (it->second.Season() == 0 && it->second.Episode() == 0)
      {
        tag.SetSeriesNumber(PVR_RECORDING_INVALID_SERIES_EPISODE);
        tag.SetEpisodeNumber(PVR_RECORDING_INVALID_SERIES_EPISODE);
      }
      else
      {
        tag.SetSeriesNumber(it->second.Season());
        tag.SetEpisodeNumber(it->second.Episode());
      }
      if (difftime(airTime, 0) > 0)
      {
        struct tm airTimeDate;
        localtime_r(&airTime, &airTimeDate);
        tag.SetYear(airTimeDate.tm_year + 1900);
      }
      tag.SetPlot(it->second.Description());
      tag.SetChannelName(it->second.ChannelName());
      tag.SetChannelUid(FindPVRChannelUid(it->second.ChannelID()));
      tag.SetChannelType(PVR_RECORDING_CHANNEL_TYPE_TV);

      int genre = m_categories.Category(it->second.Category());
      tag.SetGenreSubType(genre&0x0F);
      tag.SetGenreType(genre&0xF0);

      // Add recording title to directory to group everything according to its name just like MythTV does
      std::string strDirectory;
      if (!CMythSettings::GetRootDefaultGroup() || it->second.RecordingGroup().compare("Default") != 0)
        strDirectory.append(it->second.RecordingGroup());
      if (CMythSettings::GetGroupRecordings() == GROUP_RECORDINGS_ALWAYS || (CMythSettings::GetGroupRecordings() == GROUP_RECORDINGS_ONLY_FOR_SERIES && it->second.GetPropsSerie()))
        strDirectory.append("/").append(it->second.GroupingTitle());
      tag.SetDirectory(strDirectory);

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

      tag.SetIconPath(strIconPath);
      tag.SetThumbnailPath(strIconPath); // show the coverart when possible
      tag.SetFanartPath(strFanartPath);

      // EPG Entry (Enables "Play recording" option and icon)
      if (!it->second.IsLiveTV() && difftime(now, it->second.EndTime()) < INTERVAL_DAY) // Up to 1 day in the past
        tag.SetEPGEventId(MythEPGInfo::MakeBroadcastID(FindPVRChannelUid(it->second.ChannelID()), it->second.StartTime()));

      tag.SetLifetime(0);
      tag.SetPriority(0);
      tag.SetPlotOutline("");
      tag.SetSizeInBytes(it->second.FileSize());
      unsigned int flags = PVR_RECORDING_FLAG_UNDEFINED;
      if (it->second.GetPropsSerie())
        flags |= PVR_RECORDING_FLAG_IS_SERIES;
      tag.SetFlags(flags);

      results.Add(tag);
    }
  }

  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: Done", __FUNCTION__);

  return PVR_ERROR_NO_ERROR;
}

int PVRClientMythTV::GetDeletedRecordingsAmount()
{
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  if (m_deletedRecAmountChange)
  {
    int res = 0;
    Myth::OS::CLockGuard lock(*m_recordingsLock);
    for (ProgramInfoMap::iterator it = m_recordings.begin(); it != m_recordings.end(); ++it)
    {
      if (!it->second.IsNull() && it->second.IsDeleted() && (CMythSettings::GetLiveTVRecordings() || !it->second.IsLiveTV()))
        res++;
    }
    m_deletedRecAmount = res;
    m_deletedRecAmountChange = false;
    kodi::Log(ADDON_LOG_DEBUG, "%s: count %d", __FUNCTION__, res);
  }
  return m_deletedRecAmount;
}

PVR_ERROR PVRClientMythTV::GetDeletedRecordings(kodi::addon::PVRRecordingsResultSet& results)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_recordingsLock);

  // Transfer to PVR
  for (ProgramInfoMap::iterator it = m_recordings.begin(); it != m_recordings.end(); ++it)
  {
    if (!it->second.IsNull() && it->second.IsDeleted() && (CMythSettings::GetLiveTVRecordings() || !it->second.IsLiveTV()))
    {
      kodi::addon::PVRRecording tag;

      tag.SetIsDeleted(true);
      time_t airTime = Myth::StringToTime(it->second.Airdate());

      tag.SetRecordingTime(GetRecordingTime(airTime, it->second.RecordingStartTime()));
      tag.SetDuration(it->second.Duration());
      tag.SetPlayCount(it->second.IsWatched() ? 1 : 0);
      tag.SetLastPlayedPosition(it->second.HasBookmark() ? 1 : 0);

      std::string id = it->second.UID();

      tag.SetRecordingId(id);
      tag.SetTitle(it->second.Title());
      tag.SetEpisodeName(it->second.Subtitle());
      if (it->second.Season() == 0 && it->second.Episode() == 0)
      {
        tag.SetSeriesNumber(PVR_RECORDING_INVALID_SERIES_EPISODE);
        tag.SetEpisodeNumber(PVR_RECORDING_INVALID_SERIES_EPISODE);
      }
      else
      {
        tag.SetSeriesNumber(it->second.Season());
        tag.SetEpisodeNumber(it->second.Episode());
      }
      if (difftime(airTime, 0) > 0)
      {
        struct tm airTimeDate;
        localtime_r(&airTime, &airTimeDate);
        tag.SetYear(airTimeDate.tm_year + 1900);
      }
      tag.SetPlot(it->second.Description());
      tag.SetChannelName(it->second.ChannelName());
      tag.SetChannelUid(FindPVRChannelUid(it->second.ChannelID()));
      tag.SetChannelType(PVR_RECORDING_CHANNEL_TYPE_TV);

      int genre = m_categories.Category(it->second.Category());
      tag.SetGenreSubType(genre&0x0F);
      tag.SetGenreType(genre&0xF0);

      // Default to root of deleted view
      tag.SetDirectory("");

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
      tag.SetIconPath(strIconPath);
      tag.SetThumbnailPath(strIconPath); // show the coverart when possible
      tag.SetFanartPath(strFanartPath);

      tag.SetLifetime(0);
      tag.SetPriority(0);
      tag.SetPlotOutline("");
      tag.SetSizeInBytes(it->second.FileSize());
      unsigned int flags = PVR_RECORDING_FLAG_UNDEFINED;
      if (it->second.GetPropsSerie())
        flags |= PVR_RECORDING_FLAG_IS_SERIES;
      tag.SetFlags(flags);

      results.Add(tag);
    }
  }

  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: Done", __FUNCTION__);

  return PVR_ERROR_NO_ERROR;
}

void PVRClientMythTV::ForceUpdateRecording(ProgramInfoMap::iterator it)
{
  if (!m_control)
    return;
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

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

      if (CMythSettings::GetExtraDebug())
        kodi::Log(ADDON_LOG_DEBUG, "%s: Done", __FUNCTION__);
    }
  }
}

int PVRClientMythTV::FillRecordings()
{
  int count = 0;
  if (!m_control || !m_eventHandler)
    return count;
  kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

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
  kodi::Log(ADDON_LOG_DEBUG, "%s: count %d", __FUNCTION__, count);
  return count;
}

PVR_ERROR PVRClientMythTV::DeleteRecording(const kodi::addon::PVRRecording& recording)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_recordingsLock);

  ProgramInfoMap::iterator it = m_recordings.find(recording.GetRecordingId());
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
      kodi::Log(ADDON_LOG_DEBUG, "%s: Deleted recording %s", __FUNCTION__, recording.GetRecordingId().c_str());
      return PVR_ERROR_NO_ERROR;
    }
    else
    {
      kodi::Log(ADDON_LOG_ERROR, "%s: Failed to delete recording %s", __FUNCTION__, recording.GetRecordingId().c_str());
    }
  }
  else
  {
    kodi::Log(ADDON_LOG_ERROR, "%s: Recording %s does not exist", __FUNCTION__, recording.GetRecordingId().c_str());
  }
  return PVR_ERROR_FAILED;
}

PVR_ERROR PVRClientMythTV::DeleteAndForgetRecording(const kodi::addon::PVRRecording& recording)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_recordingsLock);

  ProgramInfoMap::iterator it = m_recordings.find(recording.GetRecordingId());
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
      kodi::Log(ADDON_LOG_DEBUG, "%s: Deleted and forget recording %s", __FUNCTION__, recording.GetRecordingId().c_str());
      return PVR_ERROR_NO_ERROR;
    }
    else
    {
      kodi::Log(ADDON_LOG_ERROR, "%s: Failed to delete recording %s", __FUNCTION__, recording.GetRecordingId().c_str());
    }
  }
  else
  {
    kodi::Log(ADDON_LOG_ERROR, "%s: Recording %s does not exist", __FUNCTION__, recording.GetRecordingId().c_str());
  }
  return PVR_ERROR_FAILED;
}

class ATTRIBUTE_HIDDEN PromptDeleteRecordingTask : public Task
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

PVR_ERROR PVRClientMythTV::SetRecordingPlayCount(const kodi::addon::PVRRecording& recording, int count)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_recordingsLock);
  ProgramInfoMap::iterator it = m_recordings.find(recording.GetRecordingId());
  if (it != m_recordings.end())
  {
    if (m_control->UpdateRecordedWatchedStatus(*(it->second.GetPtr()), (count > 0 ? true : false)))
    {
      if (CMythSettings::GetExtraDebug())
        kodi::Log(ADDON_LOG_DEBUG, "%s: Set watched state for %s", __FUNCTION__, recording.GetRecordingId().c_str());
      ForceUpdateRecording(it);
    }
    else
    {
      kodi::Log(ADDON_LOG_DEBUG, "%s: Failed setting watched state for: %s", __FUNCTION__, recording.GetRecordingId().c_str());
    }
    if (CMythSettings::GetPromptDeleteAtEnd())
    {
      m_todo->ScheduleTask(new PromptDeleteRecordingTask(this, it->second), 1000);
    }
    return PVR_ERROR_NO_ERROR;
  }
  else
  {
    kodi::Log(ADDON_LOG_DEBUG, "%s: Recording %s does not exist", __FUNCTION__, recording.GetRecordingId().c_str());
    return PVR_ERROR_FAILED;
  }
}

PVRClientMythTV::cachedBookmark_t PVRClientMythTV::_cachedBookmark = { 0, 0, 0 };

PVR_ERROR PVRClientMythTV::SetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording, int lastplayedposition)
{
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: Setting Bookmark for: %s to %d", __FUNCTION__, recording.GetTitle().c_str(), lastplayedposition);

  Myth::OS::CLockGuard lock(*m_recordingsLock);
  ProgramInfoMap::iterator it = m_recordings.find(recording.GetRecordingId());
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
        _cachedBookmark = { recording.GetChannelUid(), recording.GetRecordingTime(), lastplayedposition };
        if (CMythSettings::GetExtraDebug())
          kodi::Log(ADDON_LOG_DEBUG, "%s: Setting Bookmark successful", __FUNCTION__);
        return PVR_ERROR_NO_ERROR;
      }
    }
    kodi::Log(ADDON_LOG_INFO, "%s: Setting Bookmark failed", __FUNCTION__);
    return PVR_ERROR_NO_ERROR;
  }
  kodi::Log(ADDON_LOG_ERROR, "%s: Recording %s does not exist", __FUNCTION__, recording.GetRecordingId().c_str());
  return PVR_ERROR_FAILED;
}

PVR_ERROR PVRClientMythTV::GetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording, int& position)
{
  if (recording.GetChannelUid() == _cachedBookmark.channelUid && recording.GetRecordingTime() == _cachedBookmark.recordingTime)
  {
    kodi::Log(ADDON_LOG_DEBUG, "%s: Returning cached Bookmark for: %s", __FUNCTION__, recording.GetTitle().c_str());
    position = _cachedBookmark.position;
    return PVR_ERROR_NO_ERROR;
  }

  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: Reading Bookmark for: %s", __FUNCTION__, recording.GetTitle().c_str());

  Myth::OS::CLockGuard lock(*m_recordingsLock);
  ProgramInfoMap::iterator it = m_recordings.find(recording.GetRecordingId());
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
          position = (int)(duration / 1000);
          _cachedBookmark = { recording.GetChannelUid(), recording.GetRecordingTime(), position };
          if (CMythSettings::GetExtraDebug())
            kodi::Log(ADDON_LOG_DEBUG, "%s: %d", __FUNCTION__, position);
          return PVR_ERROR_NO_ERROR;
        }
      }
    }
    if (CMythSettings::GetExtraDebug())
      kodi::Log(ADDON_LOG_DEBUG, "%s: Recording %s has no bookmark", __FUNCTION__, recording.GetTitle().c_str());
    return PVR_ERROR_NO_ERROR;
  }
  else
    kodi::Log(ADDON_LOG_ERROR, "%s: Recording %s does not exist", __FUNCTION__, recording.GetRecordingId().c_str());
  _cachedBookmark = { recording.GetChannelUid(), recording.GetRecordingTime(), 0 };
  return PVR_ERROR_INVALID_PARAMETERS;
}

PVR_ERROR PVRClientMythTV::GetRecordingEdl(const kodi::addon::PVRRecording& recording, std::vector<kodi::addon::PVREDLEntry>& edl)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  if (CMythSettings::GetEnableEDL() == ENABLE_EDL_NEVER)
    return PVR_ERROR_NO_ERROR;
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: Reading edl for: %s", __FUNCTION__, recording.GetTitle().c_str());
  // Check recording
  MythProgramInfo prog;
  {
    Myth::OS::CLockGuard lock(*m_recordingsLock);
    ProgramInfoMap::iterator it = m_recordings.find(recording.GetRecordingId());
    if (it == m_recordings.end())
    {
      kodi::Log(ADDON_LOG_ERROR, "%s: Recording %s does not exist", __FUNCTION__, recording.GetRecordingId().c_str());
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
    kodi::Log(ADDON_LOG_DEBUG, "%s: AV props: Frame Rate = %.3f", __FUNCTION__, rate);
    if (rate <= 0)
      return PVR_ERROR_NO_ERROR;
  }

  Myth::MarkList skpList;

  // Search for commbreak list with defined unit
  Myth::MarkListPtr comList = m_control->GetCommBreakList(*(prog.GetPtr()), unit);
  kodi::Log(ADDON_LOG_DEBUG, "%s: Found %d commercial breaks for: %s", __FUNCTION__, comList->size(), recording.GetTitle().c_str());
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
  kodi::Log(ADDON_LOG_DEBUG, "%s: Found %d cut list entries for: %s", __FUNCTION__, cutList->size(), recording.GetTitle().c_str());
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
  if (CMythSettings::GetEnableEDL() == ENABLE_EDL_DIALOG && !skpList.empty())
  {
    bool canceled = false;
    if (!kodi::gui::dialogs::YesNo::ShowAndGetInput(kodi::GetLocalizedString(30110), kodi::GetLocalizedString(30111), canceled) && !canceled)
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
          kodi::addon::PVREDLEntry entry;
          double s = (double)(startPtr->markValue) / rate;
          double e = (double)((*it)->markValue) / rate;
          // Use scene marker instead commercial break
          if (CMythSettings::GetEnableEDL() == ENABLE_EDL_SCENE)
          {
            entry.SetStart((int64_t)(e * 1000.0));
            entry.SetEnd(entry.GetStart());
            entry.SetType(PVR_EDL_TYPE_SCENE);
            kodi::Log(ADDON_LOG_DEBUG, "%s: SCENE %9.3f", __FUNCTION__, e);
          }
          else
          {
            entry.SetStart((int64_t)(s * 1000.0));
            entry.SetEnd((int64_t)(e * 1000.0));
            entry.SetType(PVR_EDL_TYPE_COMBREAK);
            kodi::Log(ADDON_LOG_DEBUG, "%s: COMBREAK %9.3f - %9.3f", __FUNCTION__, s, e);
          }
          edl.emplace_back(entry);
          index++;
        }
        startPtr.reset();
        break;
      case Myth::MARK_CUT_END:
        if (startPtr && startPtr->markType == Myth::MARK_CUT_START && (*it)->markValue > startPtr->markValue)
        {
          kodi::addon::PVREDLEntry entry;
          double s = (double)(startPtr->markValue) / rate;
          double e = (double)((*it)->markValue) / rate;
          entry.SetStart((int64_t)(s * 1000.0));
          entry.SetEnd((int64_t)(e * 1000.0));
          entry.SetType(PVR_EDL_TYPE_CUT);
          edl.emplace_back(entry);
          index++;
          if (CMythSettings::GetExtraDebug())
            kodi::Log(ADDON_LOG_DEBUG, "%s: CUT %9.3f - %9.3f", __FUNCTION__, s, e);
        }
        startPtr.reset();
        break;
      default:
        startPtr.reset();
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::UndeleteRecording(const kodi::addon::PVRRecording& recording)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_recordingsLock);

  ProgramInfoMap::iterator it = m_recordings.find(recording.GetRecordingId());
  if (it != m_recordings.end())
  {
    bool ret = m_control->UndeleteRecording(*(it->second.GetPtr()));
    if (ret)
    {
      kodi::Log(ADDON_LOG_DEBUG, "%s: Undeleted recording %s", __FUNCTION__, recording.GetRecordingId().c_str());
      return PVR_ERROR_NO_ERROR;
    }
    else
    {
      kodi::Log(ADDON_LOG_ERROR, "%s: Failed to undelete recording %s", __FUNCTION__, recording.GetRecordingId().c_str());
    }
  }
  else
  {
    kodi::Log(ADDON_LOG_ERROR, "%s: Recording %s does not exist", __FUNCTION__, recording.GetRecordingId().c_str());
  }
  return PVR_ERROR_FAILED;
}

PVR_ERROR PVRClientMythTV::DeleteAllRecordingsFromTrash()
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  bool err = false;
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_recordingsLock);

  for (ProgramInfoMap::iterator it = m_recordings.begin(); it != m_recordings.end(); ++it)
  {
    if (!it->second.IsNull() && it->second.IsDeleted())
    {
      if (m_control->DeleteRecording(*(it->second.GetPtr())))
        kodi::Log(ADDON_LOG_DEBUG, "%s: Deleted recording %s", __FUNCTION__, it->first.c_str());
      else
      {
        err = true;
        kodi::Log(ADDON_LOG_ERROR, "%s: Failed to delete recording %s", __FUNCTION__, it->first.c_str());
      }
    }
  }
  if (err)
    return PVR_ERROR_REJECTED;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::GetRecordingSize(const kodi::addon::PVRRecording& recording, int64_t& bytes)
{
  if (!m_control)
    return PVR_ERROR_SERVER_ERROR;
  bytes = 0;
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: %s", __FUNCTION__, recording.GetTitle().c_str());
  // Check recording
  Myth::OS::CLockGuard lock(*m_recordingsLock);
  ProgramInfoMap::iterator it = m_recordings.find(recording.GetRecordingId());
  if (it == m_recordings.end())
  {
    kodi::Log(ADDON_LOG_ERROR, "%s: Recording %s does not exist", __FUNCTION__, recording.GetRecordingId().c_str());
    return PVR_ERROR_INVALID_PARAMETERS;
  }
  bytes = it->second.FileSize();
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

PVR_ERROR PVRClientMythTV::GetTimersAmount(int& amount)
{
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  if (m_scheduleManager)
    amount = m_scheduleManager->GetUpcomingCount();
  else
    amount = 0;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::GetTimers(kodi::addon::PVRTimersResultSet& results)
{
  if (!m_scheduleManager)
    return PVR_ERROR_SERVER_ERROR;
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  MythTimerEntryList entries;
  {
    Myth::OS::CLockGuard lock(*m_lock);
    m_PVRtimerMemorandum.clear();
    entries = m_scheduleManager->GetTimerEntries();
  }
  for (MythTimerEntryList::const_iterator it = entries.begin(); it != entries.end(); ++it)
  {
    kodi::addon::PVRTimer tag;

    tag.SetClientIndex((*it)->entryIndex);
    tag.SetParentClientIndex((*it)->parentIndex);
    tag.SetClientChannelUid(FindPVRChannelUid((*it)->chanid));
    tag.SetStartTime((*it)->startTime);
    tag.SetEndTime((*it)->endTime);

    // Discard upcoming without valid channel uid
    if (tag.GetClientChannelUid() == PVR_CHANNEL_INVALID_UID && !(*it)->isRule)
      continue;

    // Status: Match recording status with PVR_TIMER status
    switch ((*it)->recordingStatus)
    {
    case Myth::RS_ABORTED:
    case Myth::RS_MISSED:
    case Myth::RS_NOT_LISTED:
    case Myth::RS_OFFLINE:
      tag.SetState(PVR_TIMER_STATE_ABORTED);
      break;
    case Myth::RS_RECORDING:
    case Myth::RS_TUNING:
      tag.SetState(PVR_TIMER_STATE_RECORDING);
      break;
    case Myth::RS_RECORDED:
      tag.SetState(PVR_TIMER_STATE_COMPLETED);
      break;
    case Myth::RS_WILL_RECORD:
      tag.SetState(PVR_TIMER_STATE_SCHEDULED);
      break;
    case Myth::RS_CONFLICT:
      tag.SetState(PVR_TIMER_STATE_CONFLICT_NOK);
      break;
    case Myth::RS_FAILED:
    case Myth::RS_TUNER_BUSY:
    case Myth::RS_LOW_DISKSPACE:
      tag.SetState(PVR_TIMER_STATE_ERROR);
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
      tag.SetState(PVR_TIMER_STATE_DISABLED);
      break;
    case Myth::RS_CANCELLED:
      tag.SetState(PVR_TIMER_STATE_CANCELLED);
      break;
    case Myth::RS_UNKNOWN:
      if ((*it)->isInactive)
        tag.SetState(PVR_TIMER_STATE_DISABLED);
      else
        tag.SetState(PVR_TIMER_STATE_SCHEDULED);
    }

    tag.SetTimerType(static_cast<unsigned>((*it)->timerType));
    tag.SetTitle((*it)->title);
    tag.SetEPGSearchString((*it)->epgSearch.c_str());
    tag.SetFullTextEpgSearch(false);
    tag.SetDirectory(""); // not implemented
    tag.SetSummary((*it)->description);
    tag.SetPriority((*it)->priority);
    tag.SetLifetime((*it)->expiration);
    tag.SetRecordingGroup((*it)->recordingGroup);
    tag.SetFirstDay((*it)->startTime); // using startTime
    tag.SetWeekdays(PVR_WEEKDAY_NONE); // not implemented
    tag.SetPreventDuplicateEpisodes(static_cast<unsigned>((*it)->dupMethod));
    if ((*it)->epgCheck)
      tag.SetEPGUid(MythEPGInfo::MakeBroadcastID(FindPVRChannelUid((*it)->epgInfo.ChannelID()) , (*it)->epgInfo.StartTime()));
    tag.SetMarginStart((*it)->startOffset);
    tag.SetMarginEnd((*it)->endOffset);
    int genre = m_categories.Category((*it)->category);
    tag.SetGenreType(genre & 0xF0);
    tag.SetGenreSubType(genre & 0x0F);

    // Add it to memorandom: cf UpdateTimer()
    MYTH_SHARED_PTR<kodi::addon::PVRTimer> pTag = MYTH_SHARED_PTR<kodi::addon::PVRTimer>(new kodi::addon::PVRTimer(tag));
    m_PVRtimerMemorandum.insert(std::make_pair(static_cast<unsigned int>(tag.GetClientIndex()), pTag));
    results.Add(tag);
    if (CMythSettings::GetExtraDebug())
      kodi::Log(ADDON_LOG_DEBUG, "%s: #%u: IN=%d RS=%d type %u state %d parent %u autoexpire %d", __FUNCTION__,
              tag.GetClientIndex(), (*it)->isInactive, (*it)->recordingStatus,
              tag.GetTimerType(), (int)tag.GetState(), tag.GetParentClientIndex(), tag.GetLifetime());
  }

  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: Done", __FUNCTION__);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::AddTimer(const kodi::addon::PVRTimer& timer)
{
  if (!m_scheduleManager)
    return PVR_ERROR_SERVER_ERROR;
  if (CMythSettings::GetExtraDebug())
  {
    kodi::Log(ADDON_LOG_DEBUG, "%s: iClientIndex = %d", __FUNCTION__, timer.GetClientIndex());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iParentClientIndex = %d", __FUNCTION__, timer.GetParentClientIndex());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iClientChannelUid = %d", __FUNCTION__, timer.GetClientChannelUid());
    kodi::Log(ADDON_LOG_DEBUG, "%s: startTime = %ld", __FUNCTION__, timer.GetStartTime());
    kodi::Log(ADDON_LOG_DEBUG, "%s: endTime = %ld", __FUNCTION__, timer.GetEndTime());
    kodi::Log(ADDON_LOG_DEBUG, "%s: state = %d", __FUNCTION__, timer.GetState());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iTimerType = %d", __FUNCTION__, timer.GetTimerType());
    kodi::Log(ADDON_LOG_DEBUG, "%s: strTitle = %s", __FUNCTION__, timer.GetTitle().c_str());
    kodi::Log(ADDON_LOG_DEBUG, "%s: strEpgSearchString = %s", __FUNCTION__, timer.GetEPGSearchString().c_str());
    kodi::Log(ADDON_LOG_DEBUG, "%s: bFullTextEpgSearch = %d", __FUNCTION__, timer.GetFullTextEpgSearch());
    kodi::Log(ADDON_LOG_DEBUG, "%s: strDirectory = %s", __FUNCTION__, timer.GetDirectory().c_str());
    kodi::Log(ADDON_LOG_DEBUG, "%s: strSummary = %s", __FUNCTION__, timer.GetSummary().c_str());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iPriority = %d", __FUNCTION__, timer.GetPriority());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iLifetime = %d", __FUNCTION__, timer.GetLifetime());
    kodi::Log(ADDON_LOG_DEBUG, "%s: firstDay = %d", __FUNCTION__, timer.GetFirstDay());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iWeekdays = %d", __FUNCTION__, timer.GetWeekdays());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iPreventDuplicateEpisodes = %d", __FUNCTION__, timer.GetPreventDuplicateEpisodes());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iEpgUid = %d", __FUNCTION__, timer.GetEPGUid());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iMarginStart = %d", __FUNCTION__, timer.GetMarginStart());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iMarginEnd = %d", __FUNCTION__, timer.GetMarginEnd());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iGenreType = %d", __FUNCTION__, timer.GetGenreType());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iGenreSubType = %d", __FUNCTION__, timer.GetGenreSubType());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iRecordingGroup = %d", __FUNCTION__, timer.GetRecordingGroup());
  }
  kodi::Log(ADDON_LOG_DEBUG, "%s: title: %s, start: %ld, end: %ld, chanID: %u", __FUNCTION__, timer.GetTitle().c_str(), timer.GetStartTime(), timer.GetEndTime(), timer.GetClientChannelUid());
  Myth::OS::CLockGuard lock(*m_lock);
  // Check if our timer is a quick recording of live tv
  // Assumptions: Our live recorder is locked on the same channel and the recording starts
  // at the same time as or before (includes 0) the currently in progress program
  // If true then keep recording, setup recorder and let the backend handle the rule.
  if (m_liveStream && m_liveStream->IsPlaying())
  {
    Myth::ProgramPtr program = m_liveStream->GetPlayedProgram();
    if (timer.GetClientChannelUid() == FindPVRChannelUid(program->channel.chanId) &&
        timer.GetStartTime() <= program->startTime)
    {
      kodi::Log(ADDON_LOG_DEBUG, "%s: Timer is a quick recording. Toggling Record on", __FUNCTION__);
      if (m_liveStream->IsLiveRecording())
        kodi::Log(ADDON_LOG_INFO, "%s: Record already on! Retrying...", __FUNCTION__);
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
  kodi::Log(ADDON_LOG_DEBUG, "%s: Submitting new timer", __FUNCTION__);
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

PVR_ERROR PVRClientMythTV::DeleteTimer(const kodi::addon::PVRTimer& timer, bool force)
{
  if (!m_scheduleManager)
    return PVR_ERROR_SERVER_ERROR;
  if (CMythSettings::GetExtraDebug())
  {
    kodi::Log(ADDON_LOG_DEBUG, "%s: iClientIndex = %d", __FUNCTION__, timer.GetClientIndex());
    kodi::Log(ADDON_LOG_DEBUG, "%s: state = %d", __FUNCTION__, timer.GetState());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iTimerType = %d", __FUNCTION__, timer.GetTimerType());
  }
  // Check if our timer is related to rule for live recording:
  // Assumptions: Recorder handle same recording.
  // If true then expire recording, setup recorder and let backend handle the rule.
  {
    Myth::OS::CLockGuard lock(*m_lock);
    if (m_liveStream && m_liveStream->IsLiveRecording())
    {
      MythRecordingRuleNodePtr node = m_scheduleManager->FindRuleByIndex(timer.GetClientIndex());
      if (node)
      {
        MythScheduleList reclist = m_scheduleManager->FindUpComingByRuleId(node->GetRule().RecordID());
        MythScheduleList::const_iterator it = reclist.begin();
        if (it != reclist.end() && it->second && IsMyLiveRecording(*(it->second)))
        {
          kodi::Log(ADDON_LOG_DEBUG, "%s: Timer %u is a quick recording. Toggling Record off", __FUNCTION__, timer.GetClientIndex());
          if (m_liveStream->KeepLiveRecording(false))
            return PVR_ERROR_NO_ERROR;
          else
            return PVR_ERROR_FAILED;
        }
      }
    }
  }

  // Otherwise delete timer
  kodi::Log(ADDON_LOG_DEBUG, "%s: Deleting timer %u force %s", __FUNCTION__, timer.GetClientIndex(), (force ? "true" : "false"));
  MythTimerEntry entry = PVRtoTimerEntry(timer, false);
  MythScheduleManager::MSM_ERROR ret = m_scheduleManager->DeleteTimer(entry);
  if (ret == MythScheduleManager::MSM_ERROR_FAILED)
    return PVR_ERROR_FAILED;
  if (ret == MythScheduleManager::MSM_ERROR_NOT_IMPLEMENTED)
    return PVR_ERROR_NOT_IMPLEMENTED;

  return PVR_ERROR_NO_ERROR;
}

MythTimerEntry PVRClientMythTV::PVRtoTimerEntry(const kodi::addon::PVRTimer& timer, bool checkEPG)
{
  MythTimerEntry entry;

  bool hasEpg = false;
  bool hasTimeslot = false;
  bool hasChannel = false;
  bool hasEpgSearch = false;
  time_t st = timer.GetStartTime();
  time_t et = timer.GetEndTime();
  time_t fd = timer.GetFirstDay();
  time_t now = time(NULL);

  if (checkEPG && timer.GetEPGUid() != PVR_TIMER_NO_EPG_UID)
  {
    entry.epgCheck = true;
    hasEpg = true;
  }
  if (timer.GetClientChannelUid() != PVR_TIMER_ANY_CHANNEL)
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
  if (!timer.GetEPGSearchString().empty())
  {
    hasEpgSearch = true;
  }

  kodi::Log(ADDON_LOG_DEBUG, "%s: EPG=%d CHAN=%d TS=%d SEARCH=%d", __FUNCTION__, hasEpg, hasChannel, hasTimeslot, hasEpgSearch);

  // Fill EPG
  if (hasEpg && m_control)
  {
    unsigned bid;
    time_t bst;
    MythEPGInfo::BreakBroadcastID(timer.GetEPGUid(), &bid, &bst);
    kodi::Log(ADDON_LOG_DEBUG, "%s: broadcastid=%u chanid=%u localtime=%s", __FUNCTION__, (unsigned)timer.GetEPGUid(), bid, Myth::TimeToString(bst, false).c_str());
    // Retrieve broadcast using prior selected channel if valid else use original channel
    if (hasChannel)
    {
      bid = static_cast<unsigned>(timer.GetClientChannelUid());
      kodi::Log(ADDON_LOG_DEBUG, "%s: original chanid is overridden with id %u", __FUNCTION__, bid);
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
      kodi::Log(ADDON_LOG_INFO, "%s: select EPG program: %u %lu %s", __FUNCTION__, entry.chanid, st, entry.epgInfo.Title().c_str());
    }
    else
    {
      kodi::Log(ADDON_LOG_INFO, "%s: EPG program not found: %u %lu", __FUNCTION__, bid, bst);
      hasEpg = false;
    }
  }
  // Fill channel
  if (!hasEpg && hasChannel)
  {
    MythChannel channel = FindChannel(timer.GetClientChannelUid());
    if (!channel.IsNull())
    {
      entry.chanid = channel.ID();
      entry.callsign = channel.Callsign();
      kodi::Log(ADDON_LOG_DEBUG,"%s: Found channel: %u %s", __FUNCTION__, entry.chanid, entry.callsign.c_str());
    }
    else
    {
      kodi::Log(ADDON_LOG_INFO,"%s: Channel not found: %u", __FUNCTION__, timer.GetClientChannelUid());
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
    while (timer.GetEPGSearchString()[i] && isspace(timer.GetEPGSearchString()[i] != 0)) ++i;
    if (timer.GetEPGSearchString()[i])
      entry.epgSearch.assign(&(timer.GetEPGSearchString()[i]));
  }
  entry.timerType = static_cast<TimerTypeId>(timer.GetTimerType());
  entry.title.assign(timer.GetTitle());
  entry.description.assign(timer.GetSummary());
  entry.category.assign(m_categories.Category(timer.GetGenreType()));
  entry.startOffset = timer.GetMarginStart();
  entry.endOffset = timer.GetMarginEnd();
  entry.dupMethod = static_cast<Myth::DM_t>(timer.GetPreventDuplicateEpisodes());
  entry.priority = timer.GetPriority();
  entry.expiration = timer.GetLifetime();
  entry.firstShowing = false;
  entry.recordingGroup = timer.GetRecordingGroup();
  if (timer.GetTimerType() == TIMER_TYPE_DONT_RECORD)
    entry.isInactive = (timer.GetState() == PVR_TIMER_STATE_DISABLED ? false : true);
  else
    entry.isInactive = (timer.GetState() == PVR_TIMER_STATE_DISABLED ? true : false);
  entry.entryIndex = timer.GetClientIndex();
  entry.parentIndex = timer.GetParentClientIndex();
  return entry;
}

PVR_ERROR PVRClientMythTV::UpdateTimer(const kodi::addon::PVRTimer& timer)
{
  if (!m_scheduleManager)
    return PVR_ERROR_SERVER_ERROR;
  if (CMythSettings::GetExtraDebug())
  {
    kodi::Log(ADDON_LOG_DEBUG, "%s: iClientIndex = %d", __FUNCTION__, timer.GetClientIndex());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iParentClientIndex = %d", __FUNCTION__, timer.GetParentClientIndex());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iClientChannelUid = %d", __FUNCTION__, timer.GetClientChannelUid());
    kodi::Log(ADDON_LOG_DEBUG, "%s: startTime = %ld", __FUNCTION__, timer.GetStartTime());
    kodi::Log(ADDON_LOG_DEBUG, "%s: endTime = %ld", __FUNCTION__, timer.GetEndTime());
    kodi::Log(ADDON_LOG_DEBUG, "%s: state = %d", __FUNCTION__, timer.GetState());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iTimerType = %d", __FUNCTION__, timer.GetTimerType());
    kodi::Log(ADDON_LOG_DEBUG, "%s: strTitle = %s", __FUNCTION__, timer.GetTitle().c_str());
    kodi::Log(ADDON_LOG_DEBUG, "%s: strEpgSearchString = %s", __FUNCTION__, timer.GetEPGSearchString().c_str());
    kodi::Log(ADDON_LOG_DEBUG, "%s: bFullTextEpgSearch = %d", __FUNCTION__, timer.GetFullTextEpgSearch());
    kodi::Log(ADDON_LOG_DEBUG, "%s: strDirectory = %s", __FUNCTION__, timer.GetDirectory().c_str());
    kodi::Log(ADDON_LOG_DEBUG, "%s: strSummary = %s", __FUNCTION__, timer.GetSummary().c_str());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iPriority = %d", __FUNCTION__, timer.GetPriority());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iLifetime = %d", __FUNCTION__, timer.GetLifetime());
    kodi::Log(ADDON_LOG_DEBUG, "%s: firstDay = %d", __FUNCTION__, timer.GetFirstDay());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iWeekdays = %d", __FUNCTION__, timer.GetWeekdays());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iPreventDuplicateEpisodes = %d", __FUNCTION__, timer.GetPreventDuplicateEpisodes());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iEpgUid = %d", __FUNCTION__, timer.GetEPGUid());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iMarginStart = %d", __FUNCTION__, timer.GetMarginStart());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iMarginEnd = %d", __FUNCTION__, timer.GetMarginEnd());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iGenreType = %d", __FUNCTION__, timer.GetGenreType());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iGenreSubType = %d", __FUNCTION__, timer.GetGenreSubType());
    kodi::Log(ADDON_LOG_DEBUG, "%s: iRecordingGroup = %d", __FUNCTION__, timer.GetRecordingGroup());
  }
  kodi::Log(ADDON_LOG_DEBUG, "%s: title: %s, start: %ld, end: %ld, chanID: %u", __FUNCTION__, timer.GetTitle().c_str(), timer.GetStartTime(), timer.GetEndTime(), timer.GetClientChannelUid());
  MythTimerEntry entry;
  // Restore discarded info by PVR manager from our saved timer
  {
    Myth::OS::CLockGuard lock(*m_lock);
    std::map<unsigned int, MYTH_SHARED_PTR<kodi::addon::PVRTimer> >::const_iterator it = m_PVRtimerMemorandum.find(timer.GetClientIndex());
    if (it == m_PVRtimerMemorandum.end())
      return PVR_ERROR_INVALID_PARAMETERS;
    kodi::addon::PVRTimer newTimer = timer;
    newTimer.SetEPGUid(it->second->GetEPGUid());
    entry = PVRtoTimerEntry(newTimer, true);
  }
  MythScheduleManager::MSM_ERROR ret = m_scheduleManager->UpdateTimer(entry);
  if (ret == MythScheduleManager::MSM_ERROR_FAILED)
    return PVR_ERROR_FAILED;
  if (ret == MythScheduleManager::MSM_ERROR_NOT_IMPLEMENTED)
    return PVR_ERROR_NOT_IMPLEMENTED;

  kodi::Log(ADDON_LOG_DEBUG,"%s: Done", __FUNCTION__);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types)
{
  if (m_scheduleManager)
  {
    MythTimerTypeList typeList = m_scheduleManager->GetTimerTypes();
    for (const auto typeEntry : typeList)
    {
      kodi::addon::PVRTimerType type;
      typeEntry->Fill(type);
      types.emplace_back(type);
    }
    return PVR_ERROR_NO_ERROR;
  }
  //@FIXME: Returning ERROR or empty types will break PVR manager
  kodi::addon::PVRTimerType type;
  type.SetId(1);
  type.SetAttributes(PVR_TIMER_TYPE_IS_MANUAL);
  types.emplace_back(type);
  return PVR_ERROR_NO_ERROR;
}

bool PVRClientMythTV::OpenLiveStream(const kodi::addon::PVRChannel& channel)
{
  if (!m_eventHandler)
    return false;
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG,"%s: channel uid: %u, num: %u", __FUNCTION__, channel.GetUniqueId(), channel.GetChannelNumber());

  // Begin critical section
  Myth::OS::CLockGuard lock(*m_lock);
  // First we have to get merged channels for the selected channel
  Myth::ChannelList chanset;
  for (PVRChannelMap::const_iterator it = m_PVRChannelUidById.begin(); it != m_PVRChannelUidById.end(); ++it)
  {
    if (it->second == channel.GetUniqueId())
      chanset.push_back(FindChannel(it->first).GetPtr());
  }

  if (chanset.empty())
  {
    kodi::Log(ADDON_LOG_ERROR,"%s: Invalid channel", __FUNCTION__);
    return false;
  }
  // Need to create live
  if (!m_liveStream)
    m_liveStream = new Myth::LiveTVPlayback(*m_eventHandler);
  else if (m_liveStream->IsPlaying())
    return false;
  // Configure tuning of channel
  m_liveStream->SetTuneDelay(CMythSettings::GetTuneDelay());
  m_liveStream->SetLimitTuneAttempts(CMythSettings::GetLimitTuneAttempts());
  // Try to open
  if (m_liveStream->SpawnLiveTV(chanset[0]->chanNum, chanset))
  {
    kodi::Log(ADDON_LOG_DEBUG, "%s: Done", __FUNCTION__);
    return true;
  }

  delete m_liveStream;
  m_liveStream = nullptr;
  kodi::Log(ADDON_LOG_ERROR,"%s: Failed to open live stream", __FUNCTION__);
  // Open the dummy stream 'CHANNEL UNAVAILABLE'
  if (!m_dummyStream)
    m_dummyStream = new FileStreaming(ClientPath() + PATH_SEPARATOR_STRING + "resources" + PATH_SEPARATOR_STRING + "channel_unavailable.ts");
  if (m_dummyStream && m_dummyStream->IsValid())
  {
    return true;
  }
  delete m_dummyStream;
  m_dummyStream = nullptr;
  kodi::QueueNotification(QUEUE_WARNING, "", kodi::GetLocalizedString(30305)); // Channel unavailable
  return false;
}

void PVRClientMythTV::CloseLiveStream()
{
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  // Begin critical section
  Myth::OS::CLockGuard lock(*m_lock);
  // Destroy my stream
  delete m_liveStream;
  m_liveStream = nullptr;
  delete m_dummyStream;
  m_dummyStream = nullptr;

  // Reset stop request
  m_stopTV = false;

  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: Done", __FUNCTION__);
}

int PVRClientMythTV::ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  int dataread = 0;

  // Keep unlocked
  if (m_stopTV)
  {
    CloseLiveStream();
  }
  else
  {
    if (m_liveStream)
      dataread = m_liveStream->Read(pBuffer, iBufferSize);
    else if (m_dummyStream)
      dataread = m_dummyStream->Read(pBuffer, iBufferSize);
  }

  if (dataread < 0)
  {
    kodi::Log(ADDON_LOG_ERROR,"%s: Failed to read liveStream. Errorcode: %d!", __FUNCTION__, dataread);
    dataread = 0;
  }

  return dataread;
}

int64_t PVRClientMythTV::SeekLiveStream(int64_t iPosition, int iWhence)
{
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: pos: %lld, whence: %d", __FUNCTION__, iPosition, iWhence);

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

  int64_t retval;
  if (m_liveStream)
    retval = (int64_t) m_liveStream->Seek((int64_t)iPosition, whence);
  else if (m_dummyStream)
    retval = (int64_t) m_dummyStream->Seek((int64_t)iPosition, whence);
  else
    return -1;

  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: Done - position: %lld", __FUNCTION__, retval);

  return retval;
}

int64_t PVRClientMythTV::LengthLiveStream()
{
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  int64_t retval;
  if (m_liveStream)
    retval = (int64_t) m_liveStream->GetSize();
  else if (m_dummyStream)
    retval = (int64_t) m_dummyStream->GetSize();
  else
    return -1;

  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: Done - duration: %lld", __FUNCTION__, retval);

  return retval;
}

PVR_ERROR PVRClientMythTV::GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus)
{
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  Myth::OS::CLockGuard lock(*m_lock);
  if (!m_liveStream)
    return PVR_ERROR_REJECTED;

  char buf[50];
  sprintf(buf, "Myth Recorder %u", (unsigned)m_liveStream->GetCardId());
  signalStatus.SetAdapterName(buf);
  Myth::SignalStatusPtr signal = m_liveStream->GetSignal();
  if (signal)
  {
    if (signal->lock)
      signalStatus.SetAdapterStatus("Locked");
    else
      signalStatus.SetAdapterStatus("No lock");
    signalStatus.SetSignal(signal->signal);
    signalStatus.SetBER(signal->ber);
    signalStatus.SetSNR(signal->snr);
    signalStatus.SetUNC(signal->ucb);
  }

  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: Done", __FUNCTION__);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRClientMythTV::GetStreamTimes(kodi::addon::PVRStreamTimes& streamTimes)
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
      streamTimes.SetStartTime(begTs);
    }
    else if (m_recordingStream && !m_recordingStreamInfo.IsNull())
    {
      begTs = m_recordingStreamInfo.RecordingStartTime();
      endTs = m_recordingStreamInfo.RecordingEndTime();
      streamTimes.SetStartTime(0); // for recordings, this must be zero
    }
    else
    {
      return PVR_ERROR_REJECTED;
    }
  }
  time_t now = time(NULL);
  if (now < endTs)
    endTs = now;
  streamTimes.SetPTSStart(0); // it is started from 0 by the ffmpeg demuxer
  streamTimes.SetPTSBegin(0); // earliest pts player can seek back
  streamTimes.SetPTSEnd(static_cast<int64_t>(difftime(endTs, begTs)) * STREAM_TIME_BASE);
  return PVR_ERROR_NO_ERROR;
}

bool PVRClientMythTV::CanPauseStream()
{
  // reject pause with dummy stream
  return (m_liveStream ? true : false);
}

bool PVRClientMythTV::CanSeekStream()
{
  // reject seek with dummy stream
  return (m_liveStream ? true : false);
}

bool PVRClientMythTV::OpenRecordedStream(const kodi::addon::PVRRecording& recording)
{
  if (!m_control || !m_eventHandler)
    return false;
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: title: %s, ID: %s, duration: %d", __FUNCTION__, recording.GetTitle().c_str(), recording.GetRecordingId().c_str(), recording.GetDuration());

  // Begin critical section
  Myth::OS::CLockGuard lock(*m_lock);
  if (m_recordingStream)
  {
    kodi::Log(ADDON_LOG_INFO, "%s: Recorded stream is busy", __FUNCTION__);
    return false;
  }

  MythProgramInfo prog;
  {
    Myth::OS::CLockGuard lock(*m_recordingsLock);
    ProgramInfoMap::iterator it = m_recordings.find(recording.GetRecordingId());
    if (it == m_recordings.end())
    {
      kodi::Log(ADDON_LOG_ERROR, "%s: Recording %s does not exist", __FUNCTION__, recording.GetRecordingId().c_str());
      return false;
    }
    prog = it->second;
  }

  if (prog.HostName() == m_control->GetServerHostName())
  {
    // Request the stream from our master using the opened event handler.
    m_recordingStream = new Myth::RecordingPlayback(*m_eventHandler);
    if (!m_recordingStream->IsOpen())
      kodi::QueueNotification(QUEUE_ERROR, "", kodi::GetLocalizedString(30302)); // MythTV backend unavailable
    else if (m_recordingStream->OpenTransfer(prog.GetPtr()))
    {
      m_recordingStreamInfo = prog;
      if (CMythSettings::GetExtraDebug())
        kodi::Log(ADDON_LOG_DEBUG, "%s: Done", __FUNCTION__);
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
      kodi::Log(ADDON_LOG_INFO, "%s: Option 'MasterBackendOverride' is enabled", __FUNCTION__);
      m_recordingStream = new Myth::RecordingPlayback(*m_eventHandler);
      if (m_recordingStream->IsOpen() && m_recordingStream->OpenTransfer(prog.GetPtr()))
      {
        m_recordingStreamInfo = prog;
        if (CMythSettings::GetExtraDebug())
          kodi::Log(ADDON_LOG_DEBUG, "%s: Done", __FUNCTION__);
        // Fill AV info for later use
        FillRecordingAVInfo(prog, m_recordingStream);
        return true;
      }
      delete m_recordingStream;
      m_recordingStream = nullptr;
      kodi::Log(ADDON_LOG_INFO, "%s: Failed to open recorded stream from master backend", __FUNCTION__);
      kodi::Log(ADDON_LOG_INFO, "%s: You should uncheck option 'MasterBackendOverride' from MythTV setup", __FUNCTION__);
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
      backend_port = (unsigned)CMythSettings::GetProtoPort();
    // Request the stream from slave host. A dedicated event handler will be opened.
    kodi::Log(ADDON_LOG_INFO, "%s: Connect to remote backend %s:%u", __FUNCTION__, backend_addr.c_str(), backend_port);
    m_recordingStream = new Myth::RecordingPlayback(backend_addr, backend_port);
    if (!m_recordingStream->IsOpen())
      kodi::QueueNotification(QUEUE_ERROR, "", kodi::GetLocalizedString(30304)); // No response from MythTV backend
    else if (m_recordingStream->OpenTransfer(prog.GetPtr()))
    {
      m_recordingStreamInfo = prog;
      if (CMythSettings::GetExtraDebug())
        kodi::Log(ADDON_LOG_DEBUG, "%s: Done", __FUNCTION__);
      // Fill AV info for later use
      FillRecordingAVInfo(prog, m_recordingStream);
      return true;
    }
  }

  delete m_recordingStream;
  m_recordingStream = nullptr;
  kodi::Log(ADDON_LOG_ERROR,"%s: Failed to open recorded stream", __FUNCTION__);
  return false;
}

void PVRClientMythTV::CloseRecordedStream()
{
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  // Begin critical section
  Myth::OS::CLockGuard lock(*m_lock);

  // Destroy my stream
  delete m_recordingStream;
  m_recordingStream = nullptr;
  // Reset my info
  m_recordingStreamInfo = MythProgramInfo();

  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: Done", __FUNCTION__);
}

int PVRClientMythTV::ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  // Keep unlocked
  return (m_recordingStream ? m_recordingStream->Read(pBuffer, iBufferSize) : -1);
}

int64_t PVRClientMythTV::SeekRecordedStream(int64_t iPosition, int iWhence)
{
  if (iWhence == SEEK_POSSIBLE)
    return 1;

  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: pos: %lld, whence: %d", __FUNCTION__, iPosition, iWhence);

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

  int64_t retval = (int64_t) m_recordingStream->Seek((int64_t)iPosition, whence);
  // PVR API needs zero when seeking beyond the end
  if (retval < 0 && m_recordingStream->GetSize() > 0)
    retval = 0;

  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: Done - position: %lld", __FUNCTION__, retval);

  return retval;
}

int64_t PVRClientMythTV::LengthRecordedStream()
{
  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s", __FUNCTION__);

  if (!m_recordingStream)
    return -1;

  int64_t retval = (int64_t) m_recordingStream->GetSize();

  if (CMythSettings::GetExtraDebug())
    kodi::Log(ADDON_LOG_DEBUG, "%s: Done - duration: %lld", __FUNCTION__, retval);

  return retval;
}

PVR_ERROR PVRClientMythTV::CallChannelMenuHook(const kodi::addon::PVRMenuhook& menuhook, const kodi::addon::PVRChannel& item)
{
  if (menuhook.GetHookId() == MENUHOOK_TRIGGER_CHANNEL_UPDATE)
  {
    kodi::addon::CInstancePVRClient::TriggerChannelUpdate();
    return PVR_ERROR_NO_ERROR;
  }

  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR PVRClientMythTV::CallEPGMenuHook(const kodi::addon::PVRMenuhook& menuhook, const kodi::addon::PVREPGTag& tag)
{
  time_t attime;
  unsigned int chanid;
  MythEPGInfo::BreakBroadcastID(tag.GetUniqueBroadcastId(), &chanid, &attime);
  MythEPGInfo epgInfo;
  Myth::ProgramMapPtr epg = m_control->GetProgramGuide(chanid, attime, attime);
  Myth::ProgramMap::reverse_iterator epgit = epg->rbegin(); // Get last found
  if (epgit != epg->rend())
  {
    epgInfo = MythEPGInfo(epgit->second);
    if (CMythSettings::GetExtraDebug())
      kodi::Log(ADDON_LOG_DEBUG, "%s: Found EPG program (%d) chanid: %u attime: %lu", __FUNCTION__, tag.GetUniqueBroadcastId(), chanid, attime);

    if (menuhook.GetHookId() == MENUHOOK_INFO_EPG)
    {
      std::vector<std::string> items(8);
      items[0].append("BID : [COLOR white]").append(std::to_string(tag.GetUniqueBroadcastId())).append("[/COLOR]");
      items[1].append("StartTime : [COLOR white]").append(Myth::TimeToString(epgInfo.StartTime())).append("[/COLOR]");
      items[2].append("EndTime : [COLOR white]").append(Myth::TimeToString(epgInfo.EndTime())).append("[/COLOR]");
      items[3].append("ChannelName : [COLOR white]").append(epgInfo.ChannelName()).append("[/COLOR]");
      items[4].append("ChannelNum : [COLOR white]").append(epgInfo.ChannelNumber()).append("[/COLOR]");
      items[5].append("CategoryName : [COLOR white]").append(epgInfo.Category()).append("[/COLOR]");
      char genreCode[8];
      int genre = m_categories.Category(epgInfo.Category());
      snprintf(genreCode, sizeof(genreCode), "0x%.2X", genre & 0xFF);
      items[6].append("CodeEIT : [COLOR white]").append(genreCode).append("[/COLOR]");
      items[7].append("SerieID : [COLOR white]").append(epgInfo.SeriesID()).append("[/COLOR]");
      kodi::gui::dialogs::Select::Show(epgInfo.Title(), items);

      return PVR_ERROR_NO_ERROR;
    }
  }
  else
  {
    kodi::QueueNotification(QUEUE_WARNING, "", kodi::GetLocalizedString(30312));
    kodi::Log(ADDON_LOG_DEBUG, "%s: EPG program not found (%d) chanid: %u attime: %lu", __FUNCTION__, tag.GetUniqueBroadcastId(), chanid, attime);
    return PVR_ERROR_INVALID_PARAMETERS;
  }
  return PVR_ERROR_FAILED;
}

PVR_ERROR PVRClientMythTV::CallRecordingMenuHook(const kodi::addon::PVRMenuhook& menuhook, const kodi::addon::PVRRecording& item)
{
  if (menuhook.GetHookId() == MENUHOOK_REC_DELETE_AND_RERECORD)
  {
    return DeleteAndForgetRecording(item);
  }

  if (menuhook.GetHookId() == MENUHOOK_KEEP_RECORDING)
  {
    Myth::OS::CLockGuard lock(*m_recordingsLock);
    ProgramInfoMap::iterator it = m_recordings.find(item.GetRecordingId());
    if (it == m_recordings.end())
    {
      kodi::Log(ADDON_LOG_ERROR,"%s: Recording not found", __FUNCTION__);
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
        std::string info = kodi::GetLocalizedString(menuhook.GetLocalizedStringId());
        info.append(": ").append(it->second.Title());
        kodi::QueueNotification(QUEUE_INFO, "", info);
        return PVR_ERROR_NO_ERROR;
      }
    }
    return PVR_ERROR_FAILED;
  }

  if (menuhook.GetHookId() == MENUHOOK_INFO_RECORDING)
  {
    MythProgramInfo pinfo;
    {
      Myth::OS::CLockGuard lock(*m_recordingsLock);
      ProgramInfoMap::iterator it = m_recordings.find(item.GetRecordingId());
      if (it == m_recordings.end())
      {
        kodi::Log(ADDON_LOG_ERROR,"%s: Recording not found", __FUNCTION__);
        return PVR_ERROR_INVALID_PARAMETERS;
      }
      pinfo = it->second;
    }
    if (pinfo.IsNull())
      return PVR_ERROR_REJECTED;

    std::vector<std::string> items(12);
    items[0].append("Status : [COLOR white]").append(Myth::RecStatusToString(m_control->CheckService(), pinfo.Status())).append("[/COLOR]");
    items[1].append("RecordID : [COLOR white]").append(Myth::IdToString(pinfo.RecordID())).append("[/COLOR]");
    items[2].append("StartTime : [COLOR white]").append(Myth::TimeToString(pinfo.RecordingStartTime())).append("[/COLOR]");
    items[3].append("EndTime : [COLOR white]").append(Myth::TimeToString(pinfo.RecordingEndTime())).append("[/COLOR]");
    items[4].append("ChannelName : [COLOR white]").append(pinfo.ChannelName()).append("[/COLOR]");
    items[5].append("FileName : [COLOR white]").append(pinfo.FileName()).append("[/COLOR]");
    items[6].append("StorageGroup : [COLOR white]").append(pinfo.StorageGroup()).append("[/COLOR]");
    items[7].append("HostName : [COLOR white]").append(pinfo.HostName()).append("[/COLOR]");

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

    kodi::gui::dialogs::Select::Show(item.GetTitle(), items);

    return PVR_ERROR_NO_ERROR;
  }

  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR PVRClientMythTV::CallTimerMenuHook(const kodi::addon::PVRMenuhook& menuhook, const kodi::addon::PVRTimer& item)
{
  if (menuhook.GetHookId() == MENUHOOK_TIMER_BACKEND_INFO && m_scheduleManager)
  {
    MythScheduledPtr prog = m_scheduleManager->FindUpComingByIndex(item.GetClientIndex());
    if (!prog)
    {
      MythScheduleList progs = m_scheduleManager->FindUpComingByRuleId(item.GetClientIndex());
      if (progs.end() != progs.begin())
        prog = progs.begin()->second;
    }
    if (prog)
    {
      std::vector<std::string> items(4);
      items[0].append("Status : [COLOR white]").append(Myth::RecStatusToString(m_control->CheckService(), prog->Status())).append("[/COLOR]");
      items[1].append("RecordID : [COLOR white]").append(Myth::IdToString(prog->RecordID())).append("[/COLOR]");
      items[2].append("StartTime : [COLOR white]").append(Myth::TimeToString(prog->RecordingStartTime())).append("[/COLOR]");
      items[3].append("EndTime : [COLOR white]").append(Myth::TimeToString(prog->RecordingEndTime())).append("[/COLOR]");
      kodi::gui::dialogs::Select::Show(item.GetTitle(), items);
    }
    return PVR_ERROR_NO_ERROR;
  }
  else if (menuhook.GetHookId() == MENUHOOK_SHOW_HIDE_NOT_RECORDING && m_scheduleManager)
  {
    bool flag = m_scheduleManager->ToggleShowNotRecording();
    HandleScheduleChange();
    std::string info = (flag ? kodi::GetLocalizedString(30310) : kodi::GetLocalizedString(30311)); //Enabled / Disabled
    info += ": ";
    info += kodi::GetLocalizedString(30421); //Show/hide rules with status 'Not Recording'
    kodi::QueueNotification(QUEUE_INFO, "", info);
    return PVR_ERROR_NO_ERROR;
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
  AVInfo info(*this, stream);
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
  if (!CMythSettings::GetUseAirdate() || airtt == 0)
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
