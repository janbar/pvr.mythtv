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

///////////////////////////////////////////////////////////////////////////////
////
//// Version Helper for unknown version (no helper)
////

#include "MythScheduleHelperNoHelper.h"

const std::vector<MythScheduleManager::TimerType>& MythScheduleHelperNoHelper::GetTimerTypes() const
{
  static std::vector<MythScheduleManager::TimerType> typeList;
  return typeList;
}

const MythScheduleManager::RulePriorityList& MythScheduleHelperNoHelper::GetRulePriorityList() const
{
  static bool _init = false;
  static MythScheduleManager::RulePriorityList _list;
  if (!_init)
  {
    _init = true;
    _list.push_back(std::make_pair(0, "0"));
  }
  return _list;
}

const MythScheduleManager::RuleDupMethodList& MythScheduleHelperNoHelper::GetRuleDupMethodList() const
{
  static bool _init = false;
  static MythScheduleManager::RuleDupMethodList _list;
  if (!_init)
  {
    _init = true;
    _list.push_back(std::make_pair(Myth::DM_CheckNone, 30501));
  }
  return _list;
}

const MythScheduleManager::RuleExpirationList& MythScheduleHelperNoHelper::GetRuleExpirationList() const
{
  static bool _init = false;
  static MythScheduleManager::RuleExpirationList _list;
  if (!_init)
  {
    _init = true;
    _list.push_back(std::make_pair(0, 30506));
  }
  return _list;
}

const MythScheduleManager::RuleRecordingGroupList& MythScheduleHelperNoHelper::GetRuleRecordingGroupList() const
{
  static bool _init = false;
  static MythScheduleManager::RuleRecordingGroupList _list;
  if (!_init)
  {
    _init = true;
    _list.push_back(std::make_pair(RECGROUP_DFLT_ID, RECGROUP_DFLT_NAME));
  }
  return _list;
}

int MythScheduleHelperNoHelper::GetRuleRecordingGroupId(const std::string& name) const
{
  static bool _init = false;
  static std::map<std::string, int> _map;
  if (!_init)
  {
    _init = true;
    const MythScheduleManager::RuleRecordingGroupList& groupList = GetRuleRecordingGroupList();
    for (MythScheduleManager::RuleRecordingGroupList::const_iterator it = groupList.begin(); it != groupList.end(); ++it)
      _map.insert(std::make_pair(it->second, it->first));
  }
  std::map<std::string, int>::const_iterator it = _map.find(name);
  if (it != _map.end())
    return it->second;
  return RECGROUP_DFLT_ID;
}

const std::string& MythScheduleHelperNoHelper::GetRuleRecordingGroupName(int id) const
{
  static bool _init = false;
  static std::map<int, std::string> _map;
  static std::string _empty = "";
  if (!_init)
  {
    _init = true;
    const MythScheduleManager::RuleRecordingGroupList& groupList = GetRuleRecordingGroupList();
    for (MythScheduleManager::RuleRecordingGroupList::const_iterator it = groupList.begin(); it != groupList.end(); ++it)
    {
      _map.insert(std::make_pair(it->first, it->second));
    }
  }
  std::map<int, std::string>::const_iterator it = _map.find(id);
  if (it != _map.end())
    return it->second;
  return _empty;
}

bool MythScheduleHelperNoHelper::SameTimeslot(const MythRecordingRule &first, const MythRecordingRule &second) const
{
  (void)first;
  (void)second;
  return false;
}


bool MythScheduleHelperNoHelper::FillTimerEntry(MythTimerEntry& entry, const MythRecordingRuleNode& node) const
{
  (void)node;
  entry.timerType = TIMER_TYPE_UNHANDLED;
  return true;
}

bool MythScheduleHelperNoHelper::FillTimerEntry(MythTimerEntry& entry, const MythProgramInfo& recording) const
{
  (void)recording;
  entry.timerType = TIMER_TYPE_UNHANDLED;
  return true;
}

MythRecordingRule MythScheduleHelperNoHelper::NewFromTemplate(const MythEPGInfo& epgInfo)
{
  (void)epgInfo;
  return MythRecordingRule();
}

MythRecordingRule MythScheduleHelperNoHelper::NewFromTimer(const MythTimerEntry& entry, bool withTemplate)
{
  (void)entry;
  (void)withTemplate;
  return MythRecordingRule();
}

MythRecordingRule MythScheduleHelperNoHelper::MakeDontRecord(const MythRecordingRule& rule, const MythProgramInfo& recording)
{
  MythRecordingRule modifier;
  modifier.SetType(Myth::RT_NotRecording);
  return modifier;
}

MythRecordingRule MythScheduleHelperNoHelper::MakeOverride(const MythRecordingRule& rule, const MythProgramInfo& recording)
{
  MythRecordingRule modifier;
  modifier.SetType(Myth::RT_NotRecording);
  return modifier;
}

MythScheduleManager::RuleSummaryInfo MythScheduleHelperNoHelper::GetSummaryInfo(const MythRecordingRule &rule) const
{
  MythScheduleManager::RuleSummaryInfo meta;
  (void)rule;
  meta.isRepeating = false;
  meta.weekDays = 0;
  meta.marker = "";
  return meta;
}

MythRecordingRule MythScheduleHelperNoHelper::NewSingleRecord(const MythEPGInfo& epgInfo)
{
  (void)epgInfo;
  return MythRecordingRule();
}

MythRecordingRule MythScheduleHelperNoHelper::NewDailyRecord(const MythEPGInfo& epgInfo)
{
  (void)epgInfo;
  return MythRecordingRule();
}

MythRecordingRule MythScheduleHelperNoHelper::NewWeeklyRecord(const MythEPGInfo& epgInfo)
{
  (void)epgInfo;
  return MythRecordingRule();
}

MythRecordingRule MythScheduleHelperNoHelper::NewChannelRecord(const MythEPGInfo& epgInfo)
{
  (void)epgInfo;
  return MythRecordingRule();
}

MythRecordingRule MythScheduleHelperNoHelper::NewOneRecord(const MythEPGInfo& epgInfo)
{
  (void)epgInfo;
  return MythRecordingRule();
}
