#pragma once
/*
 *      Copyright (C) 2005-2015 Team XBMC
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <mythcontrol.h>
#include "MythRecordingRule.h"
#include "MythProgramInfo.h"
#include "MythEPGInfo.h"
#include "MythChannel.h"

#include <kodi/xbmc_pvr_types.h>
#include <platform/threads/mutex.h>

#include <vector>
#include <list>
#include <map>

typedef enum
{
  TIMER_TYPE_MANUAL_SEARCH  = 1,  // Manual record
  TIMER_TYPE_THIS_SHOWING   = 2,  // Record This showing
  TIMER_TYPE_RECORD_ONE,          // Record one showing
  TIMER_TYPE_RECORD_WEEKLY,       // Record one showing every week
  TIMER_TYPE_RECORD_DAILY,        // Record one showing every day
  TIMER_TYPE_RECORD_ALL,          // Record all showings
  TIMER_TYPE_TEXT_SEARCH,         // EPG Text Search All Showings
  // KEEP LAST
  TIMER_TYPE_UPCOMING,            // Upcoming
  TIMER_TYPE_OVERRIDE,            // Override
  TIMER_TYPE_DONT_RECORD,         // Don't record
  TIMER_TYPE_UNHANDLED,           // Unhandled rule
  TIMER_TYPE_UPCOMING_MANUAL,     // Upcoming manual
  TIMER_TYPE_ZOMBIE,              // Zombie
} TimerTypeId;

struct MythTimerEntry
{
  bool          isInactive;
  TimerTypeId   timerType;
  MythEPGInfo   epgInfo;
  uint32_t      chanid;
  std::string   callsign;
  bool          isAnyChannel;
  time_t        startTime;
  time_t        endTime;
  std::string   epgSearch;
  bool          isFullTextSearch;
  std::string   title;
  std::string   subtitle;
  std::string   description;
  std::string   category;
  uint32_t      season;
  uint32_t      episode;
  int           startOffset;
  int           endOffset;
  int           priority;
  Myth::DM_t    dupMethod;
  bool          autoExpire;
  bool          firstShowing;
  unsigned      recordingGroup;
  uint32_t      entryIndex;
  uint32_t      parentIndex;
  Myth::RS_t    recordingStatus;
  MythTimerEntry()
  : isInactive(true), timerType(TIMER_TYPE_UNHANDLED)
  , chanid(0), isAnyChannel(false), startTime(0), endTime(0), isFullTextSearch(false)
  , season(0), episode(0), startOffset(0), endOffset(0), priority(0)
  , dupMethod(Myth::DM_CheckNone), autoExpire(false), firstShowing(false), recordingGroup(0)
  , entryIndex(0), parentIndex(0), recordingStatus(Myth::RS_UNKNOWN) { }
  bool HasChannel() const { return (chanid > 0 && !callsign.empty() ? true : false); }
  bool HasTimeSlot() const { return (startTime > 0 && endTime >= startTime ? true : false); }
};

class MythRecordingRuleNode;
typedef MYTH_SHARED_PTR<MythRecordingRuleNode> MythRecordingRuleNodePtr;
typedef std::vector<MythRecordingRule> MythRecordingRuleList;

typedef MYTH_SHARED_PTR<MythProgramInfo> MythScheduledPtr;
typedef std::vector<std::pair<uint32_t, MythScheduledPtr> > MythScheduleList;

typedef MYTH_SHARED_PTR<MythTimerEntry> MythTimerEntryPtr;
typedef std::vector<MythTimerEntryPtr> MythTimerEntryList;

class MythRecordingRuleNode
{
public:
  friend class MythScheduleManager;

  MythRecordingRuleNode(const MythRecordingRule &rule);

  bool IsOverrideRule() const;
  MythRecordingRule GetRule() const;
  MythRecordingRule GetMainRule() const;

  bool HasOverrideRules() const;
  MythRecordingRuleList GetOverrideRules() const;

  bool IsInactiveRule() const;

private:
  MythRecordingRule m_rule;
  MythRecordingRule m_mainRule;
  MythRecordingRuleList m_overrideRules;
};

class MythScheduleManager
{
public:
  enum MSM_ERROR {
    MSM_ERROR_FAILED = -1,
    MSM_ERROR_NOT_IMPLEMENTED = 0,
    MSM_ERROR_SUCCESS = 1
  };

  typedef struct
  {
    bool        isRepeating;
    int         weekDays;
    const char  *marker;
  } RuleSummaryInfo;
  typedef std::vector<std::pair<int, std::string> > RulePriorityList; // value & symbol
  typedef std::vector<std::pair<int, int> > RuleDupMethodList; // value & localized string id
  typedef std::vector<std::pair<int, int> > RuleExpirationList; // value & localized string id
  typedef std::vector<std::pair<int, std::string> > RuleRecordingGroupList; // value & group name

  MythScheduleManager(const std::string& server, unsigned protoPort, unsigned wsapiPort, const std::string& wsapiSecurityPin);
  ~MythScheduleManager();

  // Called by GetTimers
  unsigned GetUpcomingCount() const;
  MythTimerEntryList GetTimerEntries();

  MSM_ERROR SubmitTimer(const MythTimerEntry& entry);
  MSM_ERROR UpdateTimer(const MythTimerEntry& entry);
  MSM_ERROR DeleteTimer(const MythTimerEntry& entry, bool force);


  MSM_ERROR DeleteModifier(uint32_t index);
  MSM_ERROR DisableRecording(uint32_t index);
  MSM_ERROR EnableRecording(uint32_t index);
  MSM_ERROR UpdateRecording(uint32_t index, MythRecordingRule &newrule);

  // Called by AddTimer
  MSM_ERROR AddRecordingRule(MythRecordingRule &rule);
  MSM_ERROR DeleteRecordingRule(uint32_t index);
  MSM_ERROR UpdateRecordingRule(uint32_t index, MythRecordingRule &newrule);

  MythRecordingRuleNodePtr FindRuleById(uint32_t recordid) const;
  MythRecordingRuleNodePtr FindRuleByIndex(uint32_t index) const;
  MythScheduleList FindUpComingByRuleId(uint32_t recordid) const;
  MythScheduledPtr FindUpComingByIndex(uint32_t index) const;

  bool OpenControl();
  void CloseControl();
  void Update();

  struct TimerType : public PVR_TIMER_TYPE
  {
    TimerType(TimerTypeId id, unsigned attributes, const std::string& description,
            const RulePriorityList& priorityList, int priorityDefault,
            const RuleDupMethodList& dupMethodList, int dupMethodDefault,
            const RuleExpirationList& expirationList, int expirationDefault,
            const RuleRecordingGroupList& recGroupList, int recGroupDefault);
  };

  const std::vector<TimerType>& GetTimerTypes();
  const RulePriorityList& GetRulePriorityList();
  int GetRulePriorityDefault();
  const RuleDupMethodList& GetRuleDupMethodList();
  int GetRuleDupMethodDefault();
  const RuleExpirationList& GetRuleExpirationList();
  int GetRuleExpirationDefault();
  const RuleRecordingGroupList& GetRuleRecordingGroupList();
  int GetRuleRecordingGroupDefault();


  MythRecordingRule NewFromTimer(const MythTimerEntry& entry, bool withTemplate);

  // deprecated
  RuleSummaryInfo GetSummaryInfo(const MythRecordingRule &rule) const;
  MythRecordingRule NewSingleRecord(const MythEPGInfo& epgInfo);
  MythRecordingRule NewDailyRecord(const MythEPGInfo& epgInfo);
  MythRecordingRule NewWeeklyRecord(const MythEPGInfo& epgInfo);
  MythRecordingRule NewChannelRecord(const MythEPGInfo& epgInfo);
  MythRecordingRule NewOneRecord(const MythEPGInfo& epgInfo);

  MythRecordingRuleList GetTemplateRules() const;

  bool ToggleShowNotRecording();

  class VersionHelper
  {
  public:
    friend class MythScheduleManager;

    VersionHelper() { }
    virtual ~VersionHelper();

    virtual const std::vector<TimerType>& GetTimerTypes() const = 0;
    virtual const RulePriorityList& GetRulePriorityList() const = 0;
    virtual int GetRulePriorityDefault() const = 0;
    virtual const RuleDupMethodList& GetRuleDupMethodList() const = 0;
    virtual int GetRuleDupMethodDefault() const = 0;
    virtual const RuleExpirationList& GetRuleExpirationList() const = 0;
    virtual int GetRuleExpirationDefault() const = 0;
    virtual const RuleRecordingGroupList& GetRuleRecordingGroupList() const = 0;
    virtual int GetRuleRecordingGroupDefault() const = 0;

    virtual bool SameTimeslot(const MythRecordingRule& first, const MythRecordingRule& second) const = 0;
    virtual bool FillTimerEntry(MythTimerEntry& entry, const MythRecordingRuleNode& node) const = 0;
    virtual bool FillTimerEntry(MythTimerEntry& entry, const MythProgramInfo& recording) const = 0;
    virtual MythRecordingRule NewFromTemplate(const MythEPGInfo& epgInfo) = 0;
    virtual MythRecordingRule NewFromTimer(const MythTimerEntry& entry, bool withTemplate) = 0;
    virtual MythRecordingRule MakeDontRecord(const MythRecordingRule& rule, const MythProgramInfo& recording) = 0;
    virtual MythRecordingRule MakeOverride(const MythRecordingRule& rule, const MythProgramInfo& recording) = 0;

    // deprecated
    virtual RuleSummaryInfo GetSummaryInfo(const MythRecordingRule& rule) const = 0;
    virtual MythRecordingRule NewSingleRecord(const MythEPGInfo& epgInfo) = 0;
    virtual MythRecordingRule NewDailyRecord(const MythEPGInfo& epgInfo) = 0;
    virtual MythRecordingRule NewWeeklyRecord(const MythEPGInfo& epgInfo) = 0;
    virtual MythRecordingRule NewChannelRecord(const MythEPGInfo& epgInfo) = 0;
    virtual MythRecordingRule NewOneRecord(const MythEPGInfo& epgInfo) = 0;
  };

  static uint32_t MakeIndex(const MythProgramInfo& recording);
  static uint32_t MakeIndex(const MythRecordingRule& rule);

private:
  mutable PLATFORM::CMutex m_lock;
  Myth::Control *m_control;

  int m_protoVersion;
  VersionHelper *m_versionHelper;
  void Setup();

  // The list of rule nodes
  typedef std::list<MythRecordingRuleNodePtr> NodeList;
  // To find a rule node by its key (recordId)
  typedef std::map<uint32_t, MythRecordingRuleNodePtr> NodeById;
  // To find a rule node by its key (recordId)
  typedef std::map<uint32_t, MythRecordingRuleNodePtr> NodeByIndex;
  // Store and find up coming recordings by index
  typedef std::map<uint32_t, MythScheduledPtr> RecordingList;
  // To find all indexes of schedule by rule Id : pair < Rule Id , index of schedule >
  typedef std::multimap<uint32_t, uint32_t> RecordingIndexByRuleId;

  NodeList m_rules;
  NodeById m_rulesById;
  NodeByIndex m_rulesByIndex;
  RecordingList m_recordings;
  RecordingIndexByRuleId m_recordingIndexByRuleId;
  MythRecordingRuleList m_templates;

  bool m_showNotRecording;
};


///////////////////////////////////////////////////////////////////////////////
////
//// VersionHelper
////

inline MythScheduleManager::VersionHelper::~VersionHelper() { }
