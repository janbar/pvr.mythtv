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
#include "MythPrivate.h"

#include <kodi/xbmc_pvr_types.h>

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
  TIMER_TYPE_RECORD_SERIES,       // Record series
  TIMER_TYPE_SEARCH_KEYWORD,      // Search keyword
  TIMER_TYPE_SEARCH_PEOPLE,       // Search people
  // Keep last
  TIMER_TYPE_UPCOMING,            // Upcoming
  TIMER_TYPE_RULE_INACTIVE,       // Parent Rule is disable
  TIMER_TYPE_UPCOMING_ALTERNATE,  // Upcoming will record at another time
  TIMER_TYPE_UPCOMING_RECORDED,   // Upcoming currently recorded
  TIMER_TYPE_UPCOMING_EXPIRED,    // Upcoming previously recorded and expired
  TIMER_TYPE_OVERRIDE,            // Override
  TIMER_TYPE_DONT_RECORD,         // Don't record
  TIMER_TYPE_UNHANDLED,           // Unhandled rule
  TIMER_TYPE_UPCOMING_MANUAL,     // Upcoming manual
  TIMER_TYPE_ZOMBIE,              // Zombie
} TimerTypeId;

struct MythTimerEntry
{
  bool          isRule;
  bool          isInactive;
  TimerTypeId   timerType;
  bool          epgCheck;
  MythEPGInfo   epgInfo;
  uint32_t      chanid;
  std::string   callsign;
  time_t        startTime;
  time_t        endTime;
  std::string   epgSearch;
  std::string   title;
  std::string   description;
  std::string   category;
  int           startOffset;
  int           endOffset;
  int           priority;
  Myth::DM_t    dupMethod;
  int           expiration;
  bool          firstShowing;
  unsigned      recordingGroup;
  uint32_t      entryIndex;
  uint32_t      parentIndex;
  Myth::RS_t    recordingStatus;
  MythTimerEntry()
  : isRule(false)
  , isInactive(false)
  , timerType(TIMER_TYPE_UNHANDLED)
  , epgCheck(false)
  , chanid(0)
  , startTime(0)
  , endTime(0)
  , startOffset(0)
  , endOffset(0)
  , priority(0)
  , dupMethod(Myth::DM_CheckNone)
  , expiration(0)
  , firstShowing(false)
  , recordingGroup(0)
  , entryIndex(0)
  , parentIndex(0)
  , recordingStatus(Myth::RS_UNKNOWN) { }
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

class MythTimerType;
typedef MYTH_SHARED_PTR<MythTimerType> MythTimerTypePtr;
typedef std::vector<MythTimerTypePtr> MythTimerTypeList;

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
  bool HasConflict() const;
  bool IsRecording() const;

private:
  MythRecordingRule m_rule;
  MythRecordingRule m_mainRule;
  MythRecordingRuleList m_overrideRules;
  bool m_hasConflict;                       //!< @brief Has upcoming recording in conflict status
  bool m_isRecording;                       //!< @brief Is currently recording
};

class MythScheduleManager
{
public:
  enum MSM_ERROR {
    MSM_ERROR_FAILED = -1,
    MSM_ERROR_NOT_IMPLEMENTED = 0,
    MSM_ERROR_SUCCESS = 1
  };

  MythScheduleManager(const std::string& server, unsigned protoPort, unsigned wsapiPort, const std::string& wsapiSecurityPin);
  ~MythScheduleManager();

  // Called by GetTimers
  unsigned GetUpcomingCount() const;
  MythTimerEntryList GetTimerEntries();

  MSM_ERROR SubmitTimer(const MythTimerEntry& entry);
  MSM_ERROR UpdateTimer(const MythTimerEntry& entry);
  MSM_ERROR DeleteTimer(const MythTimerEntry& entry);


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

  MythTimerTypeList GetTimerTypes();
  bool FillTimerEntryWithRule(MythTimerEntry& entry, const MythRecordingRuleNode& node) const;
  bool FillTimerEntryWithUpcoming(MythTimerEntry& entry, const MythProgramInfo& recording) const;
  MythRecordingRule NewFromTimer(const MythTimerEntry& entry, bool withTemplate);

  MythRecordingRuleList GetTemplateRules() const;

  bool ToggleShowNotRecording();
  bool ShowNotRecording();

  class VersionHelper
  {
  public:
    friend class MythScheduleManager;

    VersionHelper() { }
    virtual ~VersionHelper() { }

    virtual MythTimerTypeList GetTimerTypes() const = 0;
    virtual bool SameTimeslot(const MythRecordingRule& first, const MythRecordingRule& second) const = 0;
    virtual bool FillTimerEntryWithRule(MythTimerEntry& entry, const MythRecordingRuleNode& node) const = 0;
    virtual bool FillTimerEntryWithUpcoming(MythTimerEntry& entry, const MythProgramInfo& recording) const = 0;
    virtual MythRecordingRule NewFromTemplate(const MythEPGInfo& epgInfo) = 0;
    virtual MythRecordingRule NewFromTimer(const MythTimerEntry& entry, bool withTemplate) = 0;
    virtual MythRecordingRule MakeDontRecord(const MythRecordingRule& rule, const MythProgramInfo& recording) = 0;
    virtual MythRecordingRule MakeOverride(const MythRecordingRule& rule, const MythProgramInfo& recording) = 0;
  };

  static uint32_t MakeIndex(const MythProgramInfo& recording);
  static uint32_t MakeIndex(const MythRecordingRule& rule);

private:
  mutable Myth::OS::CMutex *m_lock;
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

  NodeList* m_rules;
  NodeById* m_rulesById;
  NodeByIndex* m_rulesByIndex;
  RecordingList* m_recordings;
  RecordingIndexByRuleId* m_recordingIndexByRuleId;
  MythRecordingRuleList* m_templates;

};

///////////////////////////////////////////////////////////////////////////////
////
//// MythTimerType
////

class MythTimerType
{
public:
  typedef std::vector<std::pair<int, std::string> > AttributeList;

  MythTimerType(TimerTypeId id, unsigned attributes, const std::string& description,
          const AttributeList& priorityList, int priorityDefault,
          const AttributeList& dupMethodList, int dupMethodDefault,
          const AttributeList& expirationList, int expirationDefault,
          const AttributeList& recGroupList, int recGroupDefault);
  virtual ~MythTimerType() {}
  void Fill(PVR_TIMER_TYPE* type) const;

private:
  TimerTypeId m_id;
  unsigned m_attributes;
  std::string m_description;
  AttributeList m_priorityList;
  int m_priorityDefault;
  AttributeList m_dupMethodList;
  int m_dupMethodDefault;
  AttributeList m_expirationList;
  int m_expirationDefault;
  AttributeList m_recGroupList;
  int m_recGroupDefault;
};
