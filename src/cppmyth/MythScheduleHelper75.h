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

#include "MythScheduleHelperNoHelper.h"

// MythTV 0.26

class MythScheduleHelper75 : public MythScheduleHelperNoHelper
{
public:
  MythScheduleHelper75(MythScheduleManager *manager, Myth::Control *control)
  : m_manager(manager)
  , m_control(control) { }

  virtual const std::vector<MythScheduleManager::TimerType>& GetTimerTypes() const;
  virtual const MythScheduleManager::RulePriorityList& GetRulePriorityList() const;
  virtual const MythScheduleManager::RuleDupMethodList& GetRuleDupMethodList() const;
  virtual int GetRuleDupMethodDefault() const { return Myth::DM_CheckSubtitleThenDescription; }
  virtual const MythScheduleManager::RuleExpirationList& GetRuleExpirationList() const;
  virtual int GetRuleExpirationDefault() const { return 1; }
  virtual const MythScheduleManager::RuleRecordingGroupList& GetRuleRecordingGroupList() const;

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
protected:
  MythScheduleManager *m_manager;
  Myth::Control *m_control;
};
