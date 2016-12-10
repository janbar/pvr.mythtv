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

  virtual MythTimerTypeList GetTimerTypes() const;
  virtual bool SameTimeslot(const MythRecordingRule& first, const MythRecordingRule& second) const;
  virtual bool FillTimerEntryWithRule(MythTimerEntry& entry, const MythRecordingRuleNode& node) const;
  virtual bool FillTimerEntryWithUpcoming(MythTimerEntry& entry, const MythProgramInfo& recording) const;
  virtual MythRecordingRule NewFromTemplate(const MythEPGInfo& epgInfo);
  virtual MythRecordingRule NewFromTimer(const MythTimerEntry& entry, bool withTemplate);
  virtual MythRecordingRule UpdateFromTimer(const MythTimerEntry& entry);
  virtual MythRecordingRule MakeDontRecord(const MythRecordingRule& rule, const MythProgramInfo& recording);
  virtual MythRecordingRule MakeOverride(const MythRecordingRule& rule, const MythProgramInfo& recording);

  // Members return default id for setting
  virtual int GetRuleDupMethodDefaultId() const { return Myth::DM_CheckSubtitleThenDescription; }
  virtual int GetRuleExpirationDefaultId() const { return EXPIRATION_ALLOW_EXPIRE_ID; }

protected:
  // Following members aren't thread safe: SHOULD be called by a locked method
  virtual const MythTimerType::AttributeList& GetRulePriorityList() const;
  virtual const MythTimerType::AttributeList& GetRuleDupMethodList() const;
  virtual const RuleExpirationMap& GetRuleExpirationMap() const;
  virtual const MythTimerType::AttributeList& GetRuleRecordingGroupList() const;

  MythScheduleManager *m_manager;
  Myth::Control *m_control;
};
