/*
 *  Copyright (C) 2013-2021 Jean-Luc Barrière
 *  Copyright (C) 2005-2014 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "cppmyth.h"
#include "artworksmanager.h"
#include "categories.h"
#include "settings.h"

#include <kodi/addon-instance/PVR.h>
#include <mythsharedptr.h>
#include <mythcontrol.h>
#include <mytheventhandler.h>
#include <mythlivetvplayback.h>
#include <mythrecordingplayback.h>
#include <mythdebug.h>

#include <string>
#include <vector>
#include <map>

class FileStreaming;
class TaskHandler;

class ATTRIBUTE_HIDDEN PVRClientMythTV : public kodi::addon::CInstancePVRClient, public Myth::EventSubscriber
{
public:
  PVRClientMythTV(KODI_HANDLE instance, const std::string& version);
  virtual ~PVRClientMythTV();

  // Server
  typedef enum
  {
    CONN_ERROR_NO_ERROR,
    CONN_ERROR_NOT_CONNECTED,
    CONN_ERROR_SERVER_UNREACHABLE,
    CONN_ERROR_UNKNOWN_VERSION,
    CONN_ERROR_API_UNAVAILABLE,
  } CONN_ERROR;

  bool Connect();
  CONN_ERROR GetConnectionError() const;
//   const char *GetConnectionString();

  PVR_ERROR GetCapabilities(kodi::addon::PVRCapabilities& capabilities) override;
  PVR_ERROR GetBackendName(std::string& name) override;
  PVR_ERROR GetBackendVersion(std::string& version) override;
  PVR_ERROR GetBackendHostname(std::string& hostname) override;
  PVR_ERROR GetConnectionString(std::string& connection) override;

  PVR_ERROR GetDriveSpace(uint64_t& total, uint64_t& used) override;
  PVR_ERROR OnSystemSleep() override;
  PVR_ERROR OnSystemWake() override;
  PVR_ERROR OnPowerSavingActivated() override;
  PVR_ERROR OnPowerSavingDeactivated() override;

  // Implements EventSubscriber
  void HandleBackendMessage(Myth::EventMessagePtr msg) override;
  void HandleChannelChange();
  void HandleScheduleChange();
  void HandleAskRecording(const Myth::EventMessage& msg);
  void HandleRecordingListChange(const Myth::EventMessage& msg);
  void PromptDeleteRecording(const MythProgramInfo &prog);
  void RunHouseKeeping();

  // EPG
  PVR_ERROR GetEPGForChannel(int channelUid, time_t start, time_t end, kodi::addon::PVREPGTagsResultSet& results) override;

  // Channels
  PVR_ERROR GetChannelsAmount(int& amount) override;
  PVR_ERROR GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results) override;

  // Channel groups
  PVR_ERROR GetChannelGroupsAmount(int& amount) override;
  PVR_ERROR GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results) override;
  PVR_ERROR GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group, kodi::addon::PVRChannelGroupMembersResultSet& results) override;

  // Recordings
  PVR_ERROR GetRecordingsAmount(bool deleted, int& amount) override;
  PVR_ERROR GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results) override;
  PVR_ERROR DeleteRecording(const kodi::addon::PVRRecording& recording) override;
  PVR_ERROR SetRecordingPlayCount(const kodi::addon::PVRRecording& recording, int count) override;
  PVR_ERROR SetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording, int lastplayedposition) override;
  PVR_ERROR GetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording, int& position) override;
  PVR_ERROR GetRecordingEdl(const kodi::addon::PVRRecording& recording, std::vector<kodi::addon::PVREDLEntry>& edl) override;
  PVR_ERROR UndeleteRecording(const kodi::addon::PVRRecording& recording) override;
  PVR_ERROR DeleteAllRecordingsFromTrash() override;
  PVR_ERROR GetRecordingSize(const kodi::addon::PVRRecording& recording, int64_t& size) override;

  // Timers
  PVR_ERROR GetTimersAmount(int& amount) override;
  PVR_ERROR GetTimers(kodi::addon::PVRTimersResultSet& results) override;
  PVR_ERROR AddTimer(const kodi::addon::PVRTimer& timer) override;
  PVR_ERROR DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete) override;
  PVR_ERROR UpdateTimer(const kodi::addon::PVRTimer& timer) override;
  PVR_ERROR GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types) override;

  // LiveTV
  bool OpenLiveStream(const kodi::addon::PVRChannel& channel) override;
  void CloseLiveStream() override;
  int ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize) override;
  int64_t SeekLiveStream(int64_t iPosition, int iWhence) override;
  int64_t LengthLiveStream() override;
  PVR_ERROR GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus) override;
  bool IsRealTimeStream() override { return m_liveStream ? true : false; }
  PVR_ERROR GetStreamTimes(kodi::addon::PVRStreamTimes& streamTimes) override;
  bool CanPauseStream() override;
  bool CanSeekStream() override;

  // Recording playback
  bool OpenRecordedStream(const kodi::addon::PVRRecording& recinfo) override;
  void CloseRecordedStream() override;
  int ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize) override;
  int64_t SeekRecordedStream(int64_t iPosition, int iWhence) override;
  int64_t LengthRecordedStream() override;

  // Menu hook
  PVR_ERROR CallChannelMenuHook(const kodi::addon::PVRMenuhook& menuhook, const kodi::addon::PVRChannel& item) override;
  PVR_ERROR CallEPGMenuHook(const kodi::addon::PVRMenuhook& menuhook, const kodi::addon::PVREPGTag& tag) override;
  PVR_ERROR CallRecordingMenuHook(const kodi::addon::PVRMenuhook& menuhook, const kodi::addon::PVRRecording& item) override;
  PVR_ERROR CallTimerMenuHook(const kodi::addon::PVRMenuhook& menuhook, const kodi::addon::PVRTimer& item) override;

  // Backend settings
  void SetDebug(bool silent = false);
  bool GetLiveTVPriority();
  void SetLiveTVPriority(bool enabled);
  void BlockBackendShutdown();
  void AllowBackendShutdown();

private:
  CONN_ERROR m_connectionError;
  Myth::EventHandler *m_eventHandler;
  Myth::Control *m_control;
  Myth::LiveTVPlayback *m_liveStream;
  Myth::RecordingPlayback *m_recordingStream;
  MythProgramInfo m_recordingStreamInfo;
  FileStreaming *m_dummyStream;
  bool m_hang;
  bool m_powerSaving;
  bool m_stopTV;

  unsigned GetBackendAPIVersion();

  /// Returns true when streaming recorded or live
  bool IsPlaying() const;

  // Backend
  ArtworkManager *m_artworksManager;
  MythScheduleManager *m_scheduleManager;
  mutable Myth::OS::CMutex *m_lock;

  // Frontend
  TaskHandler *m_todo;

  // Categories
  Categories m_categories;

  // Channels
  typedef std::map<unsigned int, MythChannel> ChannelIdMap;
  ChannelIdMap m_channelsById;
  struct PVRChannelItem
  {
    unsigned int iUniqueId;
    unsigned int iChannelNumber;
    unsigned int iSubChannelNumber;
    bool bIsRadio;
    bool operator <(const PVRChannelItem& other) const { return this->iUniqueId < other.iUniqueId; }
  };
  typedef std::vector<PVRChannelItem> PVRChannelList;
  typedef std::map<std::string, PVRChannelList> PVRChannelGroupMap;
  PVRChannelList m_PVRChannels;
  PVRChannelGroupMap m_PVRChannelGroups;
  typedef std::map<unsigned int, unsigned int> PVRChannelMap;
  PVRChannelMap m_PVRChannelUidById;
  mutable Myth::OS::CMutex *m_channelsLock;
  int FillChannelsAndChannelGroups();
  MythChannel FindChannel(uint32_t channelId) const;
  int FindPVRChannelUid(uint32_t channelId) const;

  // Recordings
  ProgramInfoMap m_recordings;
  mutable Myth::OS::CMutex *m_recordingsLock;
  unsigned m_recordingChangePinCount;
  bool m_recordingsAmountChange;
  int m_recordingsAmount;
  bool m_deletedRecAmountChange;
  int m_deletedRecAmount;
  int GetRecordingsAmount();
  PVR_ERROR GetRecordings(kodi::addon::PVRRecordingsResultSet& results);
  int GetDeletedRecordingsAmount();
  PVR_ERROR GetDeletedRecordings(kodi::addon::PVRRecordingsResultSet& results);
  PVR_ERROR DeleteAndForgetRecording(const kodi::addon::PVRRecording& recording);
  void ForceUpdateRecording(ProgramInfoMap::iterator it);
  int FillRecordings();
  MythChannel FindRecordingChannel(const MythProgramInfo& programInfo) const;
  bool IsMyLiveRecording(const MythProgramInfo& programInfo);

  // Timers
  std::map<unsigned int, MYTH_SHARED_PTR<kodi::addon::PVRTimer> > m_PVRtimerMemorandum;
  MythTimerEntry PVRtoTimerEntry(const kodi::addon::PVRTimer &timer, bool checkEPG);

  /**
   * \brief Returns full title of MythTV program
   *
   * Make formatted title based on original title and subtitle of program.
   * \see class MythProgramInfo , class MythEPGInfo
   */
  static std::string MakeProgramTitle(const std::string& title, const std::string& subtitle);

  /**
   *
   * \brief Parse and fill AV stream infos for a recorded program
   */
  void FillRecordingAVInfo(MythProgramInfo& programInfo, Myth::Stream *stream);

  /// Get the time that should be reported for this recording
  time_t GetRecordingTime(time_t airdate, time_t startDate);
};
