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
#include "MythScheduleHelperNoHelper.h"
#include "MythScheduleHelper75.h"
#include "MythScheduleHelper76.h"
#include "MythScheduleHelper85.h"
#include "../client.h"
#include "../tools.h"

#include <cstdio>
#include <cassert>
#include <math.h>

using namespace ADDON;
using namespace PLATFORM;

enum
{
  METHOD_UNKNOWN            = 0,
  METHOD_NOOP               = 1,
  METHOD_UPDATE_INACTIVE    = 2,
  METHOD_CREATE_OVERRIDE    = 3,
  METHOD_CREATE_DONTRECORD  = 4,
  METHOD_DELETE             = 5,
  METHOD_DISCREET_UPDATE    = 6,
  METHOD_FULL_UPDATE        = 7
};

static uint_fast32_t hashvalue(uint_fast32_t maxsize, const char *value)
{
  uint_fast32_t h = 0, g;

  while (*value)
  {
    h = (h << 4) + *value++;
    if ((g = h & 0xF0000000L))
    {
      h ^= g >> 24;
    }
    h &= ~g;
  }

  return h % maxsize;
}

///////////////////////////////////////////////////////////////////////////////
////
//// MythRecordingRuleNode
////

MythRecordingRuleNode::MythRecordingRuleNode(const MythRecordingRule &rule)
: m_rule(rule)
, m_mainRule()
, m_overrideRules()
{
}

bool MythRecordingRuleNode::IsOverrideRule() const
{
  return (m_rule.Type() == Myth::RT_DontRecord || m_rule.Type() == Myth::RT_OverrideRecord);
}

MythRecordingRule MythRecordingRuleNode::GetRule() const
{
  return m_rule;
}

MythRecordingRule MythRecordingRuleNode::GetMainRule() const
{
  if (IsOverrideRule())
    return m_mainRule;
  return m_rule;
}

bool MythRecordingRuleNode::HasOverrideRules() const
{
  return (!m_overrideRules.empty());
}

MythRecordingRuleList MythRecordingRuleNode::GetOverrideRules() const
{
  return m_overrideRules;
}

bool MythRecordingRuleNode::IsInactiveRule() const
{
  return m_rule.Inactive();
}


///////////////////////////////////////////////////////////////////////////////
////
//// MythScheduleManager
////

MythScheduleManager::MythScheduleManager(const std::string& server, unsigned protoPort, unsigned wsapiPort, const std::string& wsapiSecurityPin)
: m_lock()
, m_control(NULL)
, m_protoVersion(0)
, m_versionHelper(NULL)
, m_showNotRecording(false)
{
  m_control = new Myth::Control(server, protoPort, wsapiPort, wsapiSecurityPin);
  this->Update();
}

MythScheduleManager::~MythScheduleManager()
{
  SAFE_DELETE(m_versionHelper);
  SAFE_DELETE(m_control);
}

void MythScheduleManager::Setup()
{
  int old = m_protoVersion;
  m_protoVersion = m_control->CheckService();

  // On new connection the protocol version could change
  if (m_protoVersion != old)
  {
    SAFE_DELETE(m_versionHelper);
    if (m_protoVersion >= 85)
      m_versionHelper = new MythScheduleHelper85(this, m_control);
    if (m_protoVersion >= 76)
      m_versionHelper = new MythScheduleHelper76(this, m_control);
    else if (m_protoVersion >= 75)
      m_versionHelper = new MythScheduleHelper75(this, m_control);
    else
      m_versionHelper = new MythScheduleHelperNoHelper();
  }
}

uint32_t MythScheduleManager::MakeIndex(const MythProgramInfo& recording)
{
  // Recordings must keep same identifier even after refreshing cache (cf Update).
  // Numeric hash of UID is used to make the constant numeric identifier.
  // Since client share index range with 'node', I exclusively reserve the range
  // of values 0x80000000 - 0xFFFFFFFF for indexing of upcoming.
  uint32_t index = 0x80000000L | (recording.RecordID() << 16) | hashvalue(0xFFFF, recording.UID().c_str());
  return index;
}

uint32_t MythScheduleManager::MakeIndex(const MythRecordingRule& rule)
{
  // The range of values 0x0 - 0x7FFFFFFF is reserved for indexing of rule.
  return rule.RecordID() & 0x7FFFFFFFL;
}

unsigned MythScheduleManager::GetUpcomingCount() const
{
  CLockObject lock(m_lock);
  return (unsigned)m_recordings.size();
}

MythTimerEntryList MythScheduleManager::GetTimerEntries()
{
  CLockObject lock(m_lock);
  MythTimerEntryList entries;

  for (NodeList::iterator it = m_rules.begin(); it != m_rules.end(); ++it)
  {
    if ((*it)->IsOverrideRule())
      continue;
    MythTimerEntryPtr entry = MythTimerEntryPtr(new MythTimerEntry());
    if (m_versionHelper->FillTimerEntry(*entry, **it))
      entries.push_back(entry);
  }

  for (RecordingList::iterator it = m_recordings.begin(); it != m_recordings.end(); ++it)
  {
    MythTimerEntryPtr entry = MythTimerEntryPtr(new MythTimerEntry());
    if (m_versionHelper->FillTimerEntry(*entry, *(it->second)))
      entries.push_back(entry);
  }
  return entries;
}

MythScheduleManager::MSM_ERROR MythScheduleManager::SubmitTimer(const MythTimerEntry& entry)
{
  switch (entry.timerType)
  {
    case TIMER_TYPE_MANUAL_SEARCH:
    case TIMER_TYPE_THIS_SHOWING:
    case TIMER_TYPE_RECORD_ONE:
    case TIMER_TYPE_RECORD_WEEKLY:
    case TIMER_TYPE_RECORD_DAILY:
    case TIMER_TYPE_RECORD_ALL:
    case TIMER_TYPE_TEXT_SEARCH:
      break;
    default:
      return MSM_ERROR_NOT_IMPLEMENTED;
  }
  MythRecordingRule rule = m_versionHelper->NewFromTimer(entry, true);
  MSM_ERROR ret = AddRecordingRule(rule);
  return ret;
}

MythScheduleManager::MSM_ERROR MythScheduleManager::UpdateTimer(const MythTimerEntry& entry)
{
  switch (entry.timerType)
  {
    case TIMER_TYPE_UPCOMING:
    case TIMER_TYPE_DONT_RECORD:
    case TIMER_TYPE_OVERRIDE:
    {
      MythRecordingRule newrule = m_versionHelper->NewFromTimer(entry, false);
      return UpdateRecording(entry.entryIndex, newrule);
    }
    case TIMER_TYPE_MANUAL_SEARCH:
    case TIMER_TYPE_THIS_SHOWING:
    case TIMER_TYPE_RECORD_ONE:
    case TIMER_TYPE_RECORD_WEEKLY:
    case TIMER_TYPE_RECORD_DAILY:
    case TIMER_TYPE_RECORD_ALL:
    case TIMER_TYPE_TEXT_SEARCH:
    {
      MythRecordingRule newrule = m_versionHelper->NewFromTimer(entry, false);
      return UpdateRecordingRule(entry.entryIndex, newrule);
    }
    default:
      break;
  }
  return MSM_ERROR_NOT_IMPLEMENTED;
}

MythScheduleManager::MSM_ERROR MythScheduleManager::DeleteTimer(const MythTimerEntry& entry, bool force)
{
  switch (entry.timerType)
  {
    case TIMER_TYPE_UPCOMING:
      return DisableRecording(entry.entryIndex);
    case TIMER_TYPE_DONT_RECORD:
    case TIMER_TYPE_OVERRIDE:
      return DeleteModifier(entry.entryIndex);
    case TIMER_TYPE_THIS_SHOWING:
    case TIMER_TYPE_MANUAL_SEARCH:
      return DeleteRecordingRule(entry.entryIndex);
    case TIMER_TYPE_RECORD_ONE:
    case TIMER_TYPE_RECORD_WEEKLY:
    case TIMER_TYPE_RECORD_DAILY:
    case TIMER_TYPE_RECORD_ALL:
    case TIMER_TYPE_TEXT_SEARCH:
      if (force)
        return DeleteRecordingRule(entry.entryIndex);
      return MSM_ERROR_SUCCESS;
    default:
      break;
  }
  return MSM_ERROR_NOT_IMPLEMENTED;
}

MythScheduleManager::MSM_ERROR MythScheduleManager::DeleteModifier(uint32_t index)
{
  CLockObject lock(m_lock);
  MythTimerEntryList entries;

  MythScheduledPtr recording = FindUpComingByIndex(index);
  if (!recording)
    return MSM_ERROR_FAILED;
  if (recording->Status() == Myth::RS_RECORDED)
    return MSM_ERROR_FAILED;
  MythRecordingRuleNodePtr node = FindRuleById(recording->RecordID());
  if (node && node->IsOverrideRule())
  {
    XBMC->Log(LOG_DEBUG, "%s - Deleting modifier rule %u relates recording %u", __FUNCTION__, node->m_rule.RecordID(), index);
    if (!m_control->RemoveRecordSchedule(node->m_rule.RecordID()))
      XBMC->Log(LOG_ERROR, "%s - Deleting recording rule failed", __FUNCTION__);
  }
  // Another client could delete the rule at the same time. Therefore always SUCCESS even if database delete fails.
  return MSM_ERROR_SUCCESS;
}

MythScheduleManager::MSM_ERROR MythScheduleManager::DisableRecording(uint32_t index)
{
  CLockObject lock(m_lock);

  MythScheduledPtr recording = FindUpComingByIndex(index);
  if (!recording)
    return MSM_ERROR_FAILED;

  if (recording->Status() == Myth::RS_INACTIVE)
    return MSM_ERROR_SUCCESS;

  MythRecordingRuleNodePtr node = FindRuleById(recording->RecordID());
  if (node)
  {
    XBMC->Log(LOG_DEBUG, "%s - %u : %s:%s on channel %s program %s",
              __FUNCTION__, index, recording->Title().c_str(), recording->Subtitle().c_str(), recording->Callsign().c_str(), recording->UID().c_str());
    XBMC->Log(LOG_DEBUG, "%s - %u : Found rule %u type %d with recording status %d",
              __FUNCTION__, index, (unsigned)node->m_rule.RecordID(), (int)node->m_rule.Type(), recording->Status());
    int method = METHOD_UNKNOWN;
    MythRecordingRule handle = node->m_rule.DuplicateRecordingRule();

    // Method depends of its rule type
    switch (node->m_rule.Type())
    {
      case Myth::RT_SingleRecord:
        switch (recording->Status())
        {
          case Myth::RS_RECORDING:
          case Myth::RS_TUNING:
            method = METHOD_DELETE;
            break;
          case Myth::RS_PREVIOUS_RECORDING:
          case Myth::RS_EARLIER_RECORDING:
            method = METHOD_CREATE_DONTRECORD;
            break;
          default:
            method = METHOD_UPDATE_INACTIVE;
            break;
        }
        break;

      case Myth::RT_DontRecord:
      case Myth::RT_OverrideRecord:
        method = METHOD_DELETE;
        break;

      case Myth::RT_OneRecord:
      case Myth::RT_ChannelRecord:
      case Myth::RT_AllRecord:
      case Myth::RT_DailyRecord:
      case Myth::RT_WeeklyRecord:
      case Myth::RT_FindDailyRecord:
      case Myth::RT_FindWeeklyRecord:
        method = METHOD_CREATE_DONTRECORD;
        break;

      default:
        method = METHOD_UNKNOWN;
        break;
    }

    XBMC->Log(LOG_DEBUG, "%s - %u : Dealing with the problem using method %d", __FUNCTION__, index, method);
    switch (method)
    {
      case METHOD_UPDATE_INACTIVE:
        handle.SetInactive(true);
        if (!m_control->UpdateRecordSchedule(*(handle.GetPtr())))
          return MSM_ERROR_FAILED;
        node->m_rule = handle; // sync node
        return MSM_ERROR_SUCCESS;

      case METHOD_CREATE_DONTRECORD:
        handle = m_versionHelper->MakeDontRecord(handle, *recording);
        XBMC->Log(LOG_DEBUG, "%s - %u : Creating Override for %u (%s: %s) on %u (%s)"
                  , __FUNCTION__, index, (unsigned)handle.ParentID(), handle.Title().c_str(),
                  handle.Subtitle().c_str(), handle.ChannelID(), handle.Callsign().c_str());

        if (!m_control->AddRecordSchedule(*(handle.GetPtr())))
          return MSM_ERROR_FAILED;
        node->m_overrideRules.push_back(handle); // sync node
        return MSM_ERROR_SUCCESS;

      case METHOD_DELETE:
        return DeleteRecordingRule(handle.RecordID());

      default:
        return MSM_ERROR_NOT_IMPLEMENTED;
    }
  }
  return MSM_ERROR_FAILED;
}

MythScheduleManager::MSM_ERROR MythScheduleManager::EnableRecording(uint32_t index)
{
  CLockObject lock(m_lock);

  MythScheduledPtr recording = FindUpComingByIndex(index);
  if (!recording)
    return MSM_ERROR_FAILED;

  MythRecordingRuleNodePtr node = FindRuleById(recording->RecordID());
  if (node)
  {
    XBMC->Log(LOG_DEBUG, "%s - %u : %s:%s on channel %s program %s",
              __FUNCTION__, index, recording->Title().c_str(), recording->Subtitle().c_str(), recording->Callsign().c_str(), recording->UID().c_str());
    XBMC->Log(LOG_DEBUG, "%s - %u : Found rule %u type %d disabled by status %d",
              __FUNCTION__, index, (unsigned)node->m_rule.RecordID(), (int)node->m_rule.Type(), recording->Status());
    int method = METHOD_UNKNOWN;
    MythRecordingRule handle = node->m_rule.DuplicateRecordingRule();

    switch (recording->Status())
    {
      case Myth::RS_NEVER_RECORD:
      case Myth::RS_PREVIOUS_RECORDING:
      case Myth::RS_EARLIER_RECORDING:
      case Myth::RS_CURRENT_RECORDING:
        // Add override to record anyway
        method = METHOD_CREATE_OVERRIDE;
        break;

      default:
        // Enable parent rule
        method = METHOD_UPDATE_INACTIVE;
        break;
    }

    XBMC->Log(LOG_DEBUG, "%s - %u : Dealing with the problem using method %d", __FUNCTION__, index, method);
    switch (method)
    {
      case METHOD_UPDATE_INACTIVE:
        handle.SetInactive(false);
        if (!m_control->UpdateRecordSchedule(*(handle.GetPtr())))
          return MSM_ERROR_FAILED;
        node->m_rule = handle; // sync node
        return MSM_ERROR_SUCCESS;

      case METHOD_CREATE_OVERRIDE:
        handle = m_versionHelper->MakeOverride(handle, *recording);
        XBMC->Log(LOG_DEBUG, "%s - %u : Creating Override for %u (%s:%s) on %u (%s)"
                  , __FUNCTION__, index, (unsigned)handle.ParentID(), handle.Title().c_str(),
                  handle.Subtitle().c_str(), handle.ChannelID(), handle.Callsign().c_str());

        if (!m_control->AddRecordSchedule(*(handle.GetPtr())))
          return MSM_ERROR_FAILED;
        node->m_overrideRules.push_back(handle); // sync node
        return MSM_ERROR_SUCCESS;

      default:
        return MSM_ERROR_NOT_IMPLEMENTED;
    }
  }
  return MSM_ERROR_FAILED;
}

MythScheduleManager::MSM_ERROR MythScheduleManager::UpdateRecording(uint32_t index, MythRecordingRule& newrule)
{
  CLockObject lock(m_lock);

  if (newrule.Type() == Myth::RT_UNKNOWN)
    return MSM_ERROR_FAILED;

  MythScheduledPtr recording = FindUpComingByIndex(index);
  if (!recording)
    return MSM_ERROR_FAILED;

  MythRecordingRuleNodePtr node = FindRuleById(recording->RecordID());
  if (node)
  {
    XBMC->Log(LOG_DEBUG, "%s - %u : Found rule %u type %d and recording status %d",
              __FUNCTION__, index, (unsigned)node->m_rule.RecordID(), (int)node->m_rule.Type(), recording->Status());

    // Prior handle inactive
    if (!node->m_rule.Inactive() && newrule.Inactive())
    {
      XBMC->Log(LOG_DEBUG, "%s - Disable recording", __FUNCTION__);
      return DisableRecording(index);
    }

    int method = METHOD_UNKNOWN;
    MythRecordingRule handle = node->m_rule.DuplicateRecordingRule();

    switch (node->m_rule.Type())
    {
      case Myth::RT_NotRecording:
      case Myth::RT_TemplateRecord:
        method = METHOD_UNKNOWN;
        break;

      case Myth::RT_DontRecord:
        method = METHOD_NOOP;
        break;

      case Myth::RT_OverrideRecord:
        method = METHOD_DISCREET_UPDATE;
        handle.SetInactive(newrule.Inactive());
        handle.SetPriority(newrule.Priority());
        handle.SetAutoExpire(newrule.AutoExpire());
        handle.SetStartOffset(newrule.StartOffset());
        handle.SetEndOffset(newrule.EndOffset());
        handle.SetRecordingGroup(newrule.RecordingGroup());
        break;

      case Myth::RT_SingleRecord:
        switch (recording->Status())
        {
          case Myth::RS_RECORDING:
          case Myth::RS_TUNING:
            method = METHOD_DISCREET_UPDATE;
            handle.SetEndTime(newrule.EndTime());
            handle.SetEndOffset(newrule.EndOffset());
            break;
          case Myth::RS_NEVER_RECORD:
          case Myth::RS_PREVIOUS_RECORDING:
          case Myth::RS_EARLIER_RECORDING:
          case Myth::RS_CURRENT_RECORDING:
            // Add override to record anyway
            method = METHOD_CREATE_OVERRIDE;
            handle.SetPriority(newrule.Priority());
            handle.SetAutoExpire(newrule.AutoExpire());
            handle.SetStartOffset(newrule.StartOffset());
            handle.SetEndOffset(newrule.EndOffset());
            handle.SetRecordingGroup(newrule.RecordingGroup());
            break;
          default:
            method = METHOD_FULL_UPDATE;
          break;
        }
        break;

      default:
        method = METHOD_CREATE_OVERRIDE;
        handle.SetPriority(newrule.Priority());
        handle.SetAutoExpire(newrule.AutoExpire());
        handle.SetStartOffset(newrule.StartOffset());
        handle.SetEndOffset(newrule.EndOffset());
        handle.SetRecordingGroup(newrule.RecordingGroup());
        break;
    }

    XBMC->Log(LOG_DEBUG, "%s - %u : Dealing with the problem using method %d", __FUNCTION__, index, method);
    switch (method)
    {
      case METHOD_NOOP:
        return MSM_ERROR_SUCCESS;

      case METHOD_DISCREET_UPDATE:
        if (!m_control->UpdateRecordSchedule(*(handle.GetPtr())))
          return MSM_ERROR_FAILED;
        node->m_rule = handle; // sync node
        return MSM_ERROR_SUCCESS;

      case METHOD_CREATE_OVERRIDE:
        handle = m_versionHelper->MakeOverride(handle, *recording);
        XBMC->Log(LOG_DEBUG, "%s - %u : Creating Override for %u (%s: %s) on %u (%s)"
                  , __FUNCTION__, index, (unsigned)node->m_rule.RecordID(), node->m_rule.Title().c_str(),
                  node->m_rule.Subtitle().c_str(), recording->ChannelID(), recording->Callsign().c_str());

        if (!m_control->AddRecordSchedule(*(handle.GetPtr())))
          return MSM_ERROR_FAILED;
        node->m_overrideRules.push_back(handle); // sync node
        //if (!m_con.UpdateSchedules(handle.RecordID()))
        //  return MSM_ERROR_FAILED;
        return MSM_ERROR_SUCCESS;

      case METHOD_FULL_UPDATE:
        handle = newrule;
        handle.SetRecordID(node->m_rule.RecordID());
        if (!m_control->UpdateRecordSchedule(*(handle.GetPtr())))
          return MSM_ERROR_FAILED;
        node->m_rule = handle; // sync node
        return MSM_ERROR_SUCCESS;

      default:
        return MSM_ERROR_NOT_IMPLEMENTED;
    }
  }
  return MSM_ERROR_FAILED;
}

MythScheduleManager::MSM_ERROR MythScheduleManager::AddRecordingRule(MythRecordingRule &rule)
{
  if (rule.Type() == Myth::RT_UNKNOWN || rule.Type() == Myth::RT_NotRecording)
    return MSM_ERROR_FAILED;
  if (!m_control->AddRecordSchedule(*(rule.GetPtr())))
    return MSM_ERROR_FAILED;
  return MSM_ERROR_SUCCESS;
}

MythScheduleManager::MSM_ERROR MythScheduleManager::DeleteRecordingRule(uint32_t index)
{
  CLockObject lock(m_lock);

  MythRecordingRuleNodePtr node = FindRuleByIndex(index);
  if (node)
  {
    XBMC->Log(LOG_DEBUG, "%s - Found rule %u type %d", __FUNCTION__, (unsigned)node->m_rule.RecordID(), (int)node->m_rule.Type());

    // Delete overrides and their related recording
    if (node->HasOverrideRules())
    {
      for (MythRecordingRuleList::iterator ito = node->m_overrideRules.begin(); ito != node->m_overrideRules.end(); ++ito)
      {
        XBMC->Log(LOG_DEBUG, "%s - Found override rule %u type %d", __FUNCTION__, (unsigned)ito->RecordID(), (int)ito->Type());
        MythScheduleList rec = FindUpComingByRuleId(ito->RecordID());
        for (MythScheduleList::iterator itr = rec.begin(); itr != rec.end(); ++itr)
        {
          XBMC->Log(LOG_DEBUG, "%s - Found overriden recording %s status %d", __FUNCTION__, itr->second->UID().c_str(), itr->second->Status());
          if (itr->second->Status() == Myth::RS_RECORDING || itr->second->Status() == Myth::RS_TUNING)
          {
            XBMC->Log(LOG_DEBUG, "%s - Stop recording %s", __FUNCTION__, itr->second->UID().c_str());
            m_control->StopRecording(*(itr->second->GetPtr()));
          }
        }
        XBMC->Log(LOG_DEBUG, "%s - Deleting recording rule %u (modifier of rule %u)", __FUNCTION__, (unsigned)ito->RecordID(), (unsigned)node->m_rule.RecordID());
        if (!m_control->RemoveRecordSchedule(ito->RecordID()))
          XBMC->Log(LOG_ERROR, "%s - Deleting recording rule failed", __FUNCTION__);
      }
    }
    // Delete related recordings
    MythScheduleList rec = FindUpComingByRuleId(node->m_rule.RecordID());
    for (MythScheduleList::iterator itr = rec.begin(); itr != rec.end(); ++itr)
    {
      XBMC->Log(LOG_DEBUG, "%s - Found recording %s status %d", __FUNCTION__, itr->second->UID().c_str(), itr->second->Status());
      if (itr->second->Status() == Myth::RS_RECORDING || itr->second->Status() == Myth::RS_TUNING)
      {
        XBMC->Log(LOG_DEBUG, "%s - Stop recording %s", __FUNCTION__, itr->second->UID().c_str());
        m_control->StopRecording(*(itr->second->GetPtr()));
      }
    }
    // Delete rule
    XBMC->Log(LOG_DEBUG, "%s - Deleting recording rule %u", __FUNCTION__, node->m_rule.RecordID());
    if (!m_control->RemoveRecordSchedule(node->m_rule.RecordID()))
      XBMC->Log(LOG_ERROR, "%s - Deleting recording rule failed", __FUNCTION__);
  }
  // Another client could delete the rule at the same time. Therefore always SUCCESS even if database delete fails.
  return MSM_ERROR_SUCCESS;
}

MythScheduleManager::MSM_ERROR MythScheduleManager::UpdateRecordingRule(uint32_t index, MythRecordingRule& newrule)
{
  CLockObject lock(m_lock);

  if (newrule.Type() == Myth::RT_UNKNOWN)
    return MSM_ERROR_FAILED;

  MythRecordingRuleNodePtr node = FindRuleByIndex(index);
  if (node)
  {
    XBMC->Log(LOG_DEBUG, "%s - Found rule %u type %d",
              __FUNCTION__, (unsigned)node->m_rule.RecordID(), (int)node->m_rule.Type());
    int method = METHOD_UNKNOWN;
    MythRecordingRule handle = node->m_rule.DuplicateRecordingRule();

    switch (node->m_rule.Type())
    {
      case Myth::RT_NotRecording:
      case Myth::RT_TemplateRecord:
        method = METHOD_UNKNOWN;
        break;

      case Myth::RT_DontRecord:
        method = METHOD_NOOP;
        break;

      case Myth::RT_OverrideRecord:
        method = METHOD_DISCREET_UPDATE;
        handle.SetInactive(newrule.Inactive());
        handle.SetPriority(newrule.Priority());
        handle.SetAutoExpire(newrule.AutoExpire());
        handle.SetStartOffset(newrule.StartOffset());
        handle.SetEndOffset(newrule.EndOffset());
        handle.SetRecordingGroup(newrule.RecordingGroup());
        break;

      case Myth::RT_SingleRecord:
        {
          MythScheduleList recordings = FindUpComingByRuleId(handle.RecordID());
          MythScheduleList::const_reverse_iterator it = recordings.rbegin();
          if (it != recordings.rend())
            return UpdateRecording(MakeIndex(*(it->second)), newrule);
          method = METHOD_UNKNOWN;
        }
        break;

      default:
        // When inactive we can replace with the new rule
        if (handle.Inactive() || newrule.Inactive())
          method = METHOD_FULL_UPDATE;
        else
        {
          method = METHOD_DISCREET_UPDATE;
          handle.SetPriority(newrule.Priority());
          handle.SetAutoExpire(newrule.AutoExpire());
          handle.SetStartOffset(newrule.StartOffset());
          handle.SetEndOffset(newrule.EndOffset());
          handle.SetRecordingGroup(newrule.RecordingGroup());
          handle.SetCheckDuplicatesInType(newrule.CheckDuplicatesInType());
          handle.SetDuplicateControlMethod(newrule.DuplicateControlMethod());
          handle.SetFilter(newrule.Filter());
          handle.SetChannelID(newrule.ChannelID());
          handle.SetCallsign(newrule.Callsign());
          handle.SetSearchType(newrule.SearchType());
          handle.SetTitle(newrule.Title());
          handle.SetSubtitle(newrule.Subtitle());
          handle.SetDescription(newrule.Description());
        }
        break;
    }

    XBMC->Log(LOG_DEBUG, "%s - Dealing with the problem using method %d", __FUNCTION__, method);
    switch (method)
    {
      case METHOD_NOOP:
        return MSM_ERROR_SUCCESS;

      case METHOD_DISCREET_UPDATE:
        if (!m_control->UpdateRecordSchedule(*(handle.GetPtr())))
          return MSM_ERROR_FAILED;
        node->m_rule = handle; // sync node
        return MSM_ERROR_SUCCESS;

      case METHOD_FULL_UPDATE:
        handle = newrule;
        handle.SetRecordID(node->m_rule.RecordID());
        if (!m_control->UpdateRecordSchedule(*(handle.GetPtr())))
          return MSM_ERROR_FAILED;
        node->m_rule = handle; // sync node
        return MSM_ERROR_SUCCESS;

      default:
        return MSM_ERROR_NOT_IMPLEMENTED;
    }
  }
  return MSM_ERROR_FAILED;
}

MythRecordingRuleNodePtr MythScheduleManager::FindRuleById(uint32_t recordid) const
{
  CLockObject lock(m_lock);

  NodeById::const_iterator it = m_rulesById.find(recordid);
  if (it != m_rulesById.end())
    return it->second;
  return MythRecordingRuleNodePtr();
}

MythRecordingRuleNodePtr MythScheduleManager::FindRuleByIndex(uint32_t index) const
{
  CLockObject lock(m_lock);

  NodeByIndex::const_iterator it = m_rulesByIndex.find(index);
  if (it != m_rulesByIndex.end())
    return it->second;
  return MythRecordingRuleNodePtr();
}

MythScheduleList MythScheduleManager::FindUpComingByRuleId(uint32_t recordid) const
{
  CLockObject lock(m_lock);

  MythScheduleList found;
  std::pair<RecordingIndexByRuleId::const_iterator, RecordingIndexByRuleId::const_iterator> range = m_recordingIndexByRuleId.equal_range(recordid);
  if (range.first != m_recordingIndexByRuleId.end())
  {
    for (RecordingIndexByRuleId::const_iterator it = range.first; it != range.second; ++it)
    {
      RecordingList::const_iterator recordingIt = m_recordings.find(it->second);
      if (recordingIt != m_recordings.end())
        found.push_back(std::make_pair(it->second, recordingIt->second));
    }
  }
  return found;
}

MythScheduledPtr MythScheduleManager::FindUpComingByIndex(uint32_t index) const
{
  CLockObject lock(m_lock);

  RecordingList::const_iterator it = m_recordings.find(index);
  if (it != m_recordings.end())
    return it->second;
  return MythScheduledPtr();
}

bool MythScheduleManager::OpenControl()
{
  if (m_control)
    return m_control->Open();
  return false;
}

void MythScheduleManager::CloseControl()
{
  if (m_control)
    m_control->Close();
}

void MythScheduleManager::Update()
{
  CLockObject lock(m_lock);

  // Setup VersionHelper for the new set
  this->Setup();
  Myth::RecordScheduleListPtr records = m_control->GetRecordScheduleList();
  m_rules.clear();
  m_rulesById.clear();
  m_rulesByIndex.clear();
  m_templates.clear();
  for (Myth::RecordScheduleList::iterator it = records->begin(); it != records->end(); ++it)
  {
    MythRecordingRule rule(*it);
    if (rule.Type() == Myth::RT_TemplateRecord)
    {
      m_templates.push_back(rule);
    }
    else
    {
      MythRecordingRuleNodePtr node = MythRecordingRuleNodePtr(new MythRecordingRuleNode(rule));
      m_rules.push_back(node);
      m_rulesById.insert(NodeById::value_type(rule.RecordID(), node));
      m_rulesByIndex.insert(NodeByIndex::value_type(MakeIndex(rule), node));
    }
  }

  for (NodeList::iterator it = m_rules.begin(); it != m_rules.end(); ++it)
  {
    // Is override rule ? Then find main rule and link to it
    if ((*it)->IsOverrideRule())
    {
      // First check parentid. Then fallback searching the same timeslot
      NodeById::iterator itp = m_rulesById.find((*it)->m_rule.ParentID());
      if (itp != m_rulesById.end())
      {
        itp->second->m_overrideRules.push_back((*it)->m_rule);
        (*it)->m_mainRule = itp->second->m_rule;
      }
      else
      {
        for (NodeList::iterator itm = m_rules.begin(); itm != m_rules.end(); ++itm)
          if (!(*itm)->IsOverrideRule() && m_versionHelper->SameTimeslot((*it)->m_rule, (*itm)->m_rule))
          {
            (*itm)->m_overrideRules.push_back((*it)->m_rule);
            (*it)->m_mainRule = (*itm)->m_rule;
          }

      }
    }
  }

  m_recordings.clear();
  m_recordingIndexByRuleId.clear();
  // Add upcoming recordings
  Myth::ProgramListPtr recordings = m_control->GetUpcomingList();
  for (Myth::ProgramList::iterator it = recordings->begin(); it != recordings->end(); ++it)
  {
    MythScheduledPtr scheduled = MythScheduledPtr(new MythProgramInfo(*it));
    uint32_t index = MakeIndex(*scheduled);
    m_recordings.insert(RecordingList::value_type(index, scheduled));
    m_recordingIndexByRuleId.insert(RecordingIndexByRuleId::value_type(scheduled->RecordID(), index));
  }

  if (g_bExtraDebug)
  {
    for (NodeList::iterator it = m_rules.begin(); it != m_rules.end(); ++it)
      XBMC->Log(LOG_DEBUG, "%s - Rule node - recordid: %u, parentid: %u, type: %d, overriden: %s", __FUNCTION__,
              (unsigned)(*it)->m_rule.RecordID(), (unsigned)(*it)->m_rule.ParentID(),
              (int)(*it)->m_rule.Type(), ((*it)->HasOverrideRules() ? "Yes" : "No"));
    for (RecordingList::iterator it = m_recordings.begin(); it != m_recordings.end(); ++it)
      XBMC->Log(LOG_DEBUG, "%s - Recording - recordid: %u, index: %u, status: %d, title: %s", __FUNCTION__,
              (unsigned)it->second->RecordID(), (unsigned)it->first, it->second->Status(), it->second->Title().c_str());
  }
}

MythScheduleManager::TimerType::TimerType(TimerTypeId id, unsigned attributes, const std::string& description,
            const RulePriorityList& priorityList, int priorityDefault,
            const RuleDupMethodList& dupMethodList, int dupMethodDefault,
            const RuleExpirationList& expirationList, int expirationDefault,
            const RuleRecordingGroupList& recGroupList, int recGroupDefault)
{
  iId = id;
  iAttributes = attributes;
  PVR_STRCPY(strDescription, description.c_str());
  // Initialize priorities
  iPrioritiesSize = priorityList.size();
  assert(iPrioritiesSize <= PVR_ADDON_TIMERTYPE_VALUES_ARRAY_SIZE);
  unsigned index = 0;
  for (MythScheduleManager::RulePriorityList::const_iterator it = priorityList.begin(); it != priorityList.end(); ++it, ++index)
  {
    priorities[index].iValue = it->first;
    PVR_STRCPY(priorities[index].strDescription, it->second.c_str());
  }
  iPrioritiesDefault = priorityDefault;
  // Initialize duplicate methodes
  iPreventDuplicateEpisodesSize = dupMethodList.size();
  assert(iPreventDuplicateEpisodesSize <= PVR_ADDON_TIMERTYPE_VALUES_ARRAY_SIZE);
  index = 0;
  for (MythScheduleManager::RuleDupMethodList::const_iterator it = dupMethodList.begin(); it != dupMethodList.end(); ++it, ++index)
  {
    preventDuplicateEpisodes[index].iValue = it->first;
    PVR_STRCPY(preventDuplicateEpisodes[index].strDescription, XBMC->GetLocalizedString(it->second));
  }
  iPreventDuplicateEpisodesDefault = dupMethodDefault;
  // Initialize expirations
  iLifetimesSize = expirationList.size();
  assert(iLifetimesSize <= PVR_ADDON_TIMERTYPE_VALUES_ARRAY_SIZE);
  index = 0;
  for (MythScheduleManager::RuleExpirationList::const_iterator it = expirationList.begin(); it != expirationList.end(); ++it, ++index)
  {
    lifetimes[index].iValue = it->first;
    PVR_STRCPY(lifetimes[index].strDescription, XBMC->GetLocalizedString(it->second));
  }
  iLifetimesDefault = expirationDefault;
  // Initialize recording groups
  iRecordingGroupSize = recGroupList.size();
  assert(iRecordingGroupSize <= PVR_ADDON_TIMERTYPE_VALUES_ARRAY_SIZE);
  index = 0;
  for (MythScheduleManager::RuleRecordingGroupList::const_iterator it = recGroupList.begin(); it != recGroupList.end(); ++it, ++index)
  {
    recordingGroup[index].iValue = it->first;
    PVR_STRCPY(recordingGroup[index].strDescription, it->second.c_str());
  }
  iRecordingGroupDefault = recGroupDefault;
}

const std::vector<MythScheduleManager::TimerType>& MythScheduleManager::GetTimerTypes()
{
  return m_versionHelper->GetTimerTypes();
}

const MythScheduleManager::RulePriorityList& MythScheduleManager::GetRulePriorityList()
{
  return m_versionHelper->GetRulePriorityList();
}

int MythScheduleManager::GetRulePriorityDefault()
{
  return m_versionHelper->GetRulePriorityDefault();
}

const MythScheduleManager::RuleDupMethodList& MythScheduleManager::GetRuleDupMethodList()
{
  return m_versionHelper->GetRuleDupMethodList();
}

int MythScheduleManager::GetRuleDupMethodDefault()
{
  return m_versionHelper->GetRuleDupMethodDefault();
}

const MythScheduleManager::RuleExpirationList& MythScheduleManager::GetRuleExpirationList()
{
  return m_versionHelper->GetRuleExpirationList();
}

int MythScheduleManager::GetRuleExpirationDefault()
{
  return m_versionHelper->GetRuleExpirationDefault();
}

const MythScheduleManager::RuleRecordingGroupList& MythScheduleManager::GetRuleRecordingGroupList()
{
  return m_versionHelper->GetRuleRecordingGroupList();
}

int MythScheduleManager::GetRuleRecordingGroupDefault()
{
  return m_versionHelper->GetRuleRecordingGroupDefault();
}

MythRecordingRule MythScheduleManager::NewFromTimer(const MythTimerEntry& entry, bool withTemplate)
{
  return m_versionHelper->NewFromTimer(entry, withTemplate);
}

MythScheduleManager::RuleSummaryInfo MythScheduleManager::GetSummaryInfo(const MythRecordingRule &rule) const
{
  return m_versionHelper->GetSummaryInfo(rule);
}

MythRecordingRule MythScheduleManager::NewSingleRecord(const MythEPGInfo& epgInfo)
{
  return m_versionHelper->NewSingleRecord(epgInfo);
}

MythRecordingRule MythScheduleManager::NewDailyRecord(const MythEPGInfo& epgInfo)
{
  return m_versionHelper->NewDailyRecord(epgInfo);
}

MythRecordingRule MythScheduleManager::NewWeeklyRecord(const MythEPGInfo& epgInfo)
{
  return m_versionHelper->NewWeeklyRecord(epgInfo);
}

MythRecordingRule MythScheduleManager::NewChannelRecord(const MythEPGInfo& epgInfo)
{
  return m_versionHelper->NewChannelRecord(epgInfo);
}

MythRecordingRule MythScheduleManager::NewOneRecord(const MythEPGInfo& epgInfo)
{
  return m_versionHelper->NewOneRecord(epgInfo);
}

MythRecordingRuleList MythScheduleManager::GetTemplateRules() const
{
  return m_templates;
}

bool MythScheduleManager::ToggleShowNotRecording()
{
  m_showNotRecording ^= true;
  return m_showNotRecording;
}
