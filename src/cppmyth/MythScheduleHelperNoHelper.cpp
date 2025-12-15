/*
 *  Copyright (C) 2013-2021 Jean-Luc Barrière
 *  Copyright (C) 2005-2015 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

///////////////////////////////////////////////////////////////////////////////
////
//// Version Helper for unknown version (no helper)
////

#include "MythScheduleHelperNoHelper.h"
#include "private/os/threads/mutex.h"

#include <kodi/General.h>

MythScheduleHelperNoHelper::MythScheduleHelperNoHelper(MythScheduleManager *manager)
: m_lock(new Myth::OS::Mutex)
, m_manager(manager)
, m_timerTypeListInit(false)
, m_priorityListInit(false)
, m_dupMethodListInit(false)
, m_expirationMapInit(false)
, m_expirationListInit(false)
, m_expirationByKeyInit(false)
, m_recGroupListInit(false)
, m_recGroupByNameInit(false)
, m_recGroupByIdInit(false) {
}

MythScheduleHelperNoHelper::~MythScheduleHelperNoHelper()
{
  m_lock->Lock();
  m_manager = NULL;
  delete m_lock;
}

MythTimerTypeList MythScheduleHelperNoHelper::GetTimerTypes() const
{
  Myth::OS::LockGuard lock(*m_lock);
  return m_timerTypeList;
}

bool MythScheduleHelperNoHelper::SameTimeslot(const MythRecordingRule &first, const MythRecordingRule &second) const
{
  (void)first;
  (void)second;
  return false;
}


bool MythScheduleHelperNoHelper::FillTimerEntryWithRule(MythTimerEntry& entry, const MythRecordingRuleNode& node) const
{
  (void)node;
  entry.isRule = true;
  entry.timerType = TIMER_TYPE_UNHANDLED;
  return true;
}

bool MythScheduleHelperNoHelper::FillTimerEntryWithUpcoming(MythTimerEntry& entry, const MythProgramInfo& recording) const
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

static inline uint32_t expiration_key(const MythScheduleHelperNoHelper::RuleExpiration& expiration)
{
  if (expiration.maxEpisodes > 0 && expiration.maxEpisodes < 0x100)
    return (expiration.maxEpisodes & 0xFF) | (expiration.maxNewest ? 0x100 : 0x0);
  else
    return (expiration.autoExpire ? 0x200 : 0x0);
}

int MythScheduleHelperNoHelper::GetRuleExpirationId(const RuleExpiration& expiration) const
{
  Myth::OS::LockGuard lock(*m_lock);
  if (!m_expirationByKeyInit)
  {
    m_expirationByKeyInit = true;
    const RuleExpirationMap& expirationMap = GetRuleExpirationMap();
    for (const auto& expiration : expirationMap)
      m_expirationByKey.emplace(expiration_key(expiration.second.first), expiration.first);
  }
  std::map<uint32_t, int>::const_iterator it = m_expirationByKey.find(expiration_key(expiration));
  if (it != m_expirationByKey.end())
    return it->second;
  return GetRuleExpirationDefaultId();
}

MythScheduleHelperNoHelper::RuleExpiration MythScheduleHelperNoHelper::GetRuleExpiration(int id) const
{
  Myth::OS::LockGuard lock(*m_lock);
  static RuleExpiration _empty(false, 0, false);
  RuleExpirationMap::const_iterator it = GetRuleExpirationMap().find(id);
  if (it != m_expirationMap.end())
    return it->second.first;
  return _empty;
}

int MythScheduleHelperNoHelper::GetRuleRecordingGroupId(const std::string& name) const
{
  Myth::OS::LockGuard lock(*m_lock);
  if (!m_recGroupByNameInit)
  {
    m_recGroupByNameInit = true;
    const MythTimerType::AttributeList& groupList = GetRuleRecordingGroupList();
    for (const auto& group : groupList)
      m_recGroupByName.emplace(group.GetDescription(), group.GetValue());
  }
  std::map<std::string, int>::const_iterator it = m_recGroupByName.find(name);
  if (it != m_recGroupByName.end())
    return it->second;
  return RECGROUP_DFLT_ID;
}

std::string MythScheduleHelperNoHelper::GetRuleRecordingGroupName(int id) const
{
  Myth::OS::LockGuard lock(*m_lock);
  static std::string _empty = "";
  if (!m_recGroupByIdInit)
  {
    m_recGroupByIdInit = true;
    const MythTimerType::AttributeList& groupList = GetRuleRecordingGroupList();
    for (const auto& group : groupList)
    {
      m_recGroupById.emplace(group.GetValue(), group.GetDescription());
    }
  }
  std::map<int, std::string>::const_iterator it = m_recGroupById.find(id);
  if (it != m_recGroupById.end())
    return it->second;
  return _empty;
}

const MythTimerType::AttributeList& MythScheduleHelperNoHelper::GetRulePriorityList() const
{
  if (!m_priorityListInit)
  {
    m_priorityListInit = true;
    m_priorityList.emplace_back(0, "0");
  }
  return m_priorityList;
}

const MythTimerType::AttributeList& MythScheduleHelperNoHelper::GetRuleDupMethodList() const
{
  if (!m_dupMethodListInit)
  {
    m_dupMethodListInit = true;
    m_dupMethodList.emplace_back(static_cast<int>(Myth::DM_CheckNone), kodi::addon::GetLocalizedString(30501)); // Don't match duplicates
  }
  return m_dupMethodList;
}

const MythScheduleHelperNoHelper::RuleExpirationMap& MythScheduleHelperNoHelper::GetRuleExpirationMap() const
{
  if (!m_expirationMapInit)
  {
    m_expirationMapInit = true;
    m_expirationMap.insert(std::make_pair(EXPIRATION_NEVER_EXPIRE_ID, std::make_pair(RuleExpiration(false, 0, false), kodi::addon::GetLocalizedString(30506)))); // Allow recordings to expire
    m_expirationMap.insert(std::make_pair(EXPIRATION_ALLOW_EXPIRE_ID, std::make_pair(RuleExpiration(true, 0, false), kodi::addon::GetLocalizedString(30507)))); // Allow recordings to expire
  }
  return m_expirationMap;
}

const MythTimerType::AttributeList& MythScheduleHelperNoHelper::GetRuleExpirationNameList() const
{
  if (!m_expirationListInit)
  {
    m_expirationListInit = true;
    const RuleExpirationMap& expirationMap = GetRuleExpirationMap();
    for (const auto& expiration : expirationMap)
      m_expirationList.emplace_back(expiration.first, expiration.second.second);
  }
  return m_expirationList;
}

const MythTimerType::AttributeList& MythScheduleHelperNoHelper::GetRuleRecordingGroupList() const
{
  if (!m_recGroupListInit)
  {
    m_recGroupListInit = true;
    m_recGroupList.emplace_back(RECGROUP_DFLT_ID, RECGROUP_DFLT_NAME);
  }
  return m_recGroupList;
}
