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

#include "MythScheduleManager.h"

#define RECGROUP_DFLT_ID    0
#define RECGROUP_DFLT_NAME  "Default"

// No helper

class MythScheduleHelperNoHelper : public MythScheduleManager::VersionHelper
{
public:

  virtual const std::vector<MythScheduleManager::TimerType>& GetTimerTypes() const;
  virtual const MythScheduleManager::RulePriorityList& GetRulePriorityList() const;
  virtual int GetRulePriorityDefault() const { return 0; }
  virtual const MythScheduleManager::RuleDupMethodList& GetRuleDupMethodList() const;
  virtual int GetRuleDupMethodDefault() const { return Myth::DM_CheckNone; }
  virtual const MythScheduleManager::RuleExpirationList& GetRuleExpirationList() const;
  virtual int GetRuleExpirationDefault() const { return 0; }
  virtual const MythScheduleManager::RuleRecordingGroupList& GetRuleRecordingGroupList() const;
  virtual int GetRuleRecordingGroupId(const std::string& name) const;
  virtual const std::string& GetRuleRecordingGroupName(int id) const;
  virtual int GetRuleRecordingGroupDefault() const { return RECGROUP_DFLT_ID; }

  virtual bool SameTimeslot(const MythRecordingRule& first, const MythRecordingRule& second) const;
  virtual bool FillTimerEntry(MythTimerEntry& entry, const MythRecordingRuleNode& node) const;
  virtual bool FillTimerEntry(MythTimerEntry& entry, const MythProgramInfo& recording) const;
  virtual MythRecordingRule NewFromTemplate(const MythEPGInfo& epgInfo);
  virtual MythRecordingRule NewFromTimer(const MythTimerEntry& entry, bool withTemplate);
  virtual MythRecordingRule MakeDontRecord(const MythRecordingRule& rule, const MythProgramInfo& recording);
  virtual MythRecordingRule MakeOverride(const MythRecordingRule& rule, const MythProgramInfo& recording);


  // deprecated
  virtual MythScheduleManager::RuleSummaryInfo GetSummaryInfo(const MythRecordingRule& rule) const;
  virtual MythRecordingRule NewSingleRecord(const MythEPGInfo& epgInfo);
  virtual MythRecordingRule NewDailyRecord(const MythEPGInfo& epgInfo);
  virtual MythRecordingRule NewWeeklyRecord(const MythEPGInfo& epgInfo);
  virtual MythRecordingRule NewChannelRecord(const MythEPGInfo& epgInfo);
  virtual MythRecordingRule NewOneRecord(const MythEPGInfo& epgInfo);
};
