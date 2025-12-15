/*
 *  Copyright (C) 2013-2021 Jean-Luc Barrière
 *  Copyright (C) 2005-2015 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "MythScheduleManager.h"

#define RECGROUP_DFLT_ID            0
#define RECGROUP_DFLT_NAME          "Default"
#define EXPIRATION_NEVER_EXPIRE_ID  0
#define EXPIRATION_ALLOW_EXPIRE_ID  1

// No helper

class MythScheduleHelperNoHelper : public MythScheduleManager::VersionHelper
{
public:
  explicit MythScheduleHelperNoHelper(MythScheduleManager *m_manager);
  virtual ~MythScheduleHelperNoHelper();

  // Implements VersionHelper
  virtual MythTimerTypeList GetTimerTypes() const;
  virtual bool SameTimeslot(const MythRecordingRule& first, const MythRecordingRule& second) const;
  virtual bool FillTimerEntryWithRule(MythTimerEntry& entry, const MythRecordingRuleNode& node) const;
  virtual bool FillTimerEntryWithUpcoming(MythTimerEntry& entry, const MythProgramInfo& recording) const;
  virtual MythRecordingRule NewFromTemplate(const MythEPGInfo& epgInfo);
  virtual MythRecordingRule NewFromTimer(const MythTimerEntry& entry, bool withTemplate);
  virtual MythRecordingRule MakeDontRecord(const MythRecordingRule& rule, const MythProgramInfo& recording);
  virtual MythRecordingRule MakeOverride(const MythRecordingRule& rule, const MythProgramInfo& recording);

  struct RuleExpiration
  {
    bool  autoExpire;
    int   maxEpisodes;
    bool  maxNewest;
    RuleExpiration(bool autoexpire, int maxepisodes, bool newest)
    : autoExpire(autoexpire), maxEpisodes(maxepisodes), maxNewest(newest) { }
  };

  // Members return default id for setting
  virtual int GetRulePriorityDefaultId() const { return 0; }
  virtual int GetRuleDupMethodDefaultId() const { return static_cast<int>(Myth::DM_CheckNone); }
  virtual int GetRuleExpirationDefaultId() const { return EXPIRATION_NEVER_EXPIRE_ID; }
  virtual int GetRuleRecordingGroupDefaultId() const { return RECGROUP_DFLT_ID; }
  // Members convert id to/from data
  virtual int GetRuleExpirationId(const RuleExpiration& expiration) const;
  virtual RuleExpiration GetRuleExpiration(int id) const;
  virtual int GetRuleRecordingGroupId(const std::string& name) const;
  virtual std::string GetRuleRecordingGroupName(int id) const;

protected:
  Myth::Control* GetControl() const { return (m_manager ? m_manager->m_control : NULL); }
  typedef std::map<int, std::pair<RuleExpiration, std::string> > RuleExpirationMap;
  // Following members aren't thread safe: SHOULD be called by a locked method
  virtual const MythTimerType::AttributeList& GetRulePriorityList() const;
  virtual const MythTimerType::AttributeList& GetRuleDupMethodList() const;
  virtual const RuleExpirationMap& GetRuleExpirationMap() const;
  virtual const MythTimerType::AttributeList& GetRuleExpirationNameList() const;
  virtual const MythTimerType::AttributeList& GetRuleRecordingGroupList() const;

  mutable Myth::OS::Mutex *m_lock;
  mutable MythScheduleManager *m_manager;

  mutable bool                            m_timerTypeListInit;
  mutable MythTimerTypeList               m_timerTypeList;

  mutable bool                            m_priorityListInit;
  mutable MythTimerType::AttributeList    m_priorityList;

  mutable bool                            m_dupMethodListInit;
  mutable MythTimerType::AttributeList    m_dupMethodList;

  mutable bool                            m_expirationMapInit;
  mutable RuleExpirationMap               m_expirationMap;

  mutable bool                            m_expirationListInit;
  mutable MythTimerType::AttributeList    m_expirationList;

  mutable bool                            m_expirationByKeyInit;
  mutable std::map<uint32_t, int>         m_expirationByKey;

  mutable bool                            m_recGroupListInit;
  mutable MythTimerType::AttributeList    m_recGroupList;

  mutable bool                            m_recGroupByNameInit;
  mutable std::map<std::string, int>      m_recGroupByName;

  mutable bool                            m_recGroupByIdInit;
  mutable std::map<int, std::string>      m_recGroupById;

};
