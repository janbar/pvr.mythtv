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
//// Version helper for database up to 1309 (0.27)
////
//// Remove the Timeslot and Weekslot recording rule types. These rule
//// types are too rigid and don't work when a broadcaster shifts the
//// starting time of a program by a few minutes. Users should now use
//// Channel recording rules in place of Timeslot and Weekslot rules. To
//// approximate the old functionality, two new schedule filters have been
//// added. In addition, the new "This time" and "This day and time"
//// filters are less strict and match any program starting within 10
//// minutes of the recording rule time.
//// Restrict the use of the FindDaily? and FindWeekly? recording rule types
//// (now simply called Daily and Weekly) to search and manual recording
//// rules. These rule types are rarely needed and limiting their use to
//// the most powerful cases simplifies the user interface for the more
//// common cases. Users should now use Daily and Weekly, custom search
//// rules in place of FindDaily? and FindWeekly? rules.
//// Any existing recording rules using the no longer supported or allowed
//// types are automatically converted to the suggested alternatives.
////

#include "MythScheduleHelper76.h"
#include "../client.h"
#include "../tools.h"

#include <cstdio>
#include <cassert>

using namespace ADDON;

bool MythScheduleHelper76::FillTimerEntry(MythTimerEntry& entry, const MythRecordingRuleNode& node) const
{
  // Assign timer type using rule attributes.
  // This SHOULD match what was applied in function 'NewFromTimer'

  MythRecordingRule rule = node.GetRule();

  if (true /*!m_showNotRecording*/)
  {
    time_t ruleNextActive;
    if (difftime(rule.NextRecording(), 0) > 0) 
      ruleNextActive = rule.NextRecording();
    else if (difftime(rule.LastRecorded(), 0) > 0) 
      ruleNextActive = rule.LastRecorded();
    else 
      ruleNextActive = rule.StartTime();

     time_t now = time(NULL);
    if (difftime(ruleNextActive, now) < -72000 ) //60s*60min*24h = 7200
    {
      XBMC->Log(LOG_DEBUG, "%s: Skipping Rule %s on %s as more than 24h old", 
        __FUNCTION__, rule.Title().c_str(), rule.Callsign().c_str());
      return false;
    }
  }

  XBMC->Log(LOG_DEBUG,"%s (MythScheduleHelper76): MythRecordingRuleNode-> %u (%s:%s)", __FUNCTION__
            , rule.RecordID(), rule.Title().c_str(), rule.Subtitle().c_str());

  switch (rule.Type())
  {
    case Myth::RT_SingleRecord:
      {
        // Fill recording status from its upcoming
        MythScheduleList recordings = m_manager->FindUpComingByRuleId(rule.RecordID());
        MythScheduleList::const_reverse_iterator it = recordings.rbegin();
        if (it != recordings.rend())
          entry.recordingStatus = it->second->Status();
        else
          return false; // Don't transfer single without upcoming
      }
      if (rule.SearchType() == Myth::ST_ManualSearch)
        entry.timerType = TIMER_TYPE_MANUAL_SEARCH;
      else
        entry.timerType = TIMER_TYPE_THIS_SHOWING;
      break;

    case Myth::RT_OneRecord:
      entry.timerType = TIMER_TYPE_RECORD_ONE;
      if (!(rule.Filter() & Myth::FM_ThisChannel))
        entry.isAnyChannel = true; // Identify this as an AnyChannel rule
      break;

    case Myth::RT_DailyRecord:
      entry.timerType = TIMER_TYPE_RECORD_DAILY;
      break;

    case Myth::RT_WeeklyRecord:
      entry.timerType = TIMER_TYPE_RECORD_WEEKLY;
      break;

    case Myth::RT_AllRecord:
      if ((rule.Filter() & Myth::FM_ThisDayAndTime))
        entry.timerType = TIMER_TYPE_RECORD_WEEKLY;
      else if ((rule.Filter() & Myth::FM_ThisTime))
        entry.timerType = TIMER_TYPE_RECORD_DAILY;
      else
      {
        if (rule.SearchType() == Myth::ST_NoSearch) 
        {
          entry.timerType = TIMER_TYPE_RECORD_ALL;
        }
        else if (rule.SearchType() == Myth::ST_TitleSearch)
        {
          entry.timerType = TIMER_TYPE_TEXT_SEARCH;
          entry.epgSearch = rule.Description();
          entry.isFullTextSearch = false;
        }
        else if (rule.SearchType() == Myth::ST_KeywordSearch)
        {
          entry.timerType = TIMER_TYPE_TEXT_SEARCH;
          entry.epgSearch = rule.Description();
          entry.isFullTextSearch = true;
        }
      }
      if (!(rule.Filter() & Myth::FM_ThisChannel))
        entry.isAnyChannel = true; // Identify this as an AnyChannel rule
      break;

    case Myth::RT_OverrideRecord:
      entry.timerType = TIMER_TYPE_OVERRIDE;
      break;

    case Myth::RT_DontRecord:
      entry.timerType = TIMER_TYPE_DONT_RECORD;
      break;

    default:
      entry.timerType = TIMER_TYPE_UNHANDLED;
      break;
  }

  switch (rule.SearchType())
  {
    case Myth::ST_PeopleSearch:
    case Myth::ST_PowerSearch:
      entry.epgSearch = rule.Description();
      entry.timerType = TIMER_TYPE_UNHANDLED;
      break;
    case Myth::ST_TitleSearch:
    case Myth::ST_KeywordSearch:
    case Myth::ST_NoSearch:
    case Myth::ST_ManualSearch:
    default:
      break;
  }
  entry.title = rule.Title();
  entry.chanid = rule.ChannelID();
  entry.callsign = rule.Callsign();

  // For all repeating fix timeslot as needed
  MythScheduleList recordings;
  switch (entry.timerType)
  {
    case TIMER_TYPE_RECORD_ONE:
    case TIMER_TYPE_RECORD_WEEKLY:
    case TIMER_TYPE_RECORD_DAILY:
    case TIMER_TYPE_RECORD_ALL:
    case TIMER_TYPE_TEXT_SEARCH:
    case TIMER_TYPE_UNHANDLED:
      if (difftime(rule.NextRecording(), 0) > 0)
      {
        // fill timeslot starting at next recording
        entry.startTime = entry.endTime = rule.NextRecording();
        timeadd(&entry.endTime, difftime(rule.EndTime(), rule.StartTime()));
      }
      else if (difftime(rule.LastRecorded(), 0) > 0)
      {
        // fill timeslot starting at last recorded
        entry.startTime = entry.endTime = rule.LastRecorded();
        timeadd(&entry.endTime, difftime(rule.EndTime(), rule.StartTime()));
      }
      else
      {
        entry.startTime = rule.StartTime();
        entry.endTime = rule.EndTime();
      }
      //Mythbackend (0.27) doesn't appear to consider currently active recordings, so check for one
      recordings = m_manager->FindUpComingByRuleId(rule.RecordID());
      for (MythScheduleList::const_reverse_iterator it = recordings.rbegin(); it !=recordings.rend(); ++it)
      {
        if ( (it->second->Status() == Myth::RS_RECORDING) ||
             (it->second->Status() == Myth::RS_TUNING) )
        {
            
            entry.recordingStatus = it->second->Status();
            entry.startTime = it->second->StartTime();
            entry.endTime = it->second->EndTime();
            XBMC->Log(LOG_DEBUG,"%s: Found active recording for rule %u", __FUNCTION__, rule.RecordID());
            break;
        }
      }
      break;
    default:
      entry.startTime = rule.StartTime();
      entry.endTime = rule.EndTime();
  }

  // fill others
  entry.startOffset = rule.StartOffset();
  entry.endOffset = rule.EndOffset();
  entry.dupMethod = rule.DuplicateControlMethod();
  entry.priority = rule.Priority();
  entry.autoExpire = rule.AutoExpire();
  entry.isInactive = rule.Inactive();
  entry.firstShowing = (rule.Filter() & Myth::FM_FirstShowing ? true : false);
  entry.recordingGroup = GetRuleRecordingGroupId(rule.RecordingGroup());
  entry.entryIndex = MythScheduleManager::MakeIndex(rule); // rule index
  if (node.IsOverrideRule())
    entry.parentIndex = MythScheduleManager::MakeIndex(node.GetMainRule());
  else
    entry.parentIndex = 0;
  return true;
}

MythRecordingRule MythScheduleHelper76::NewFromTimer(const MythTimerEntry& entry, bool withTemplate)
{
  // Create a recording rule regarding timer attributes. The match SHOULD be opposite to
  // that which is applied in function 'FillTimerEntry'

  MythRecordingRule rule;

  if (withTemplate)
  {
    // Base on template
    rule = NewFromTemplate(entry.epgInfo);
    // Override template with timer settings
    rule.SetStartOffset(rule.StartOffset() + entry.startOffset);
    rule.SetEndOffset(rule.EndOffset() + entry.endOffset);
    if (entry.dupMethod != GetRuleDupMethodDefault())
    {
      rule.SetDuplicateControlMethod(entry.dupMethod);
      rule.SetCheckDuplicatesInType(Myth::DI_InAll);
    }
    if (entry.priority != GetRulePriorityDefault())
      rule.SetPriority(entry.priority);
    if (entry.autoExpire != GetRuleExpirationDefault())
      rule.SetAutoExpire(entry.autoExpire);
    if (entry.recordingGroup != RECGROUP_DFLT_ID)
      rule.SetRecordingGroup(GetRuleRecordingGroupName(entry.recordingGroup));
  }
  else
  {
    rule.SetStartOffset(entry.startOffset);
    rule.SetEndOffset(entry.endOffset);
    rule.SetDuplicateControlMethod(entry.dupMethod);
    rule.SetPriority(entry.priority);
    rule.SetAutoExpire(entry.autoExpire);
    rule.SetRecordingGroup(GetRuleRecordingGroupName(entry.recordingGroup));
  }

  switch (entry.timerType)
  {
    case TIMER_TYPE_MANUAL_SEARCH:
    {
      if (entry.HasChannel() && entry.HasTimeSlot()) //Manual only
      {
        rule.SetType(Myth::RT_SingleRecord);
        rule.SetSearchType(Myth::ST_ManualSearch); // Using timeslot
        rule.SetChannelID(entry.chanid);
        rule.SetCallsign(entry.callsign);
        rule.SetStartTime(entry.startTime);
        rule.SetEndTime(entry.endTime);
        rule.SetTitle(entry.title);
        rule.SetSubtitle(entry.subtitle);
        rule.SetDescription(entry.description);
        rule.SetInactive(entry.isInactive);
        return rule;
      }
      break;
    }

    case TIMER_TYPE_THIS_SHOWING:
    {
      rule.SetType(Myth::RT_SingleRecord);
      rule.SetSearchType(Myth::ST_NoSearch);
      rule.SetInactive(entry.isInactive);
      if (!entry.epgInfo.IsNull()) //EPG initial entry
      {
        rule.SetChannelID(entry.epgInfo.ChannelID());
        rule.SetCallsign(entry.epgInfo.Callsign());
        rule.SetStartTime(entry.epgInfo.StartTime());
        rule.SetEndTime(entry.epgInfo.EndTime());
        rule.SetTitle(entry.epgInfo.Title());
        rule.SetSubtitle(entry.epgInfo.Subtitle());
        rule.SetDescription(entry.epgInfo.Description());
        rule.SetCategory(entry.epgInfo.Category());
        rule.SetProgramID(entry.epgInfo.ProgramID());
        rule.SetSeriesID(entry.epgInfo.SeriesID());
        return rule;
      }
      if (!entry.isAnyChannel && entry.HasChannel() && entry.HasTimeSlot()) //Valid edit case
      {
        rule.SetChannelID(entry.chanid);
        rule.SetCallsign(entry.callsign);
        rule.SetStartTime(entry.startTime);
        rule.SetEndTime(entry.endTime);
        rule.SetTitle(entry.title);
        rule.SetSubtitle(entry.subtitle);
        rule.SetDescription(entry.description);
        rule.SetCategory(entry.category);
        return rule;
      }
      if (entry.isAnyChannel)
        XBMC->Log(LOG_ERROR, "%s (MythScheduleHelper76) AnyChannel is not supported for this Timer Type.", __FUNCTION__);
      break;
    }

    case TIMER_TYPE_RECORD_ONE:
    {
      rule.SetType(Myth::RT_OneRecord);
      rule.SetSearchType(Myth::ST_NoSearch);
      if (!entry.isAnyChannel) rule.SetFilter(Myth::FM_ThisChannel);
      rule.SetInactive(entry.isInactive);

      if (!entry.epgInfo.IsNull()) //EPG initial entry
      {
        rule.SetStartTime(entry.epgInfo.StartTime());
        rule.SetEndTime(entry.epgInfo.EndTime());
        if (entry.isAnyChannel) //Grab channel info while we still have it
        {
          rule.SetChannelID(entry.epgInfo.ChannelID());
          rule.SetCallsign(entry.epgInfo.Callsign());
        }
        rule.SetTitle(entry.epgInfo.Title());
        rule.SetSubtitle(entry.epgInfo.Subtitle());
        rule.SetDescription(entry.epgInfo.Description());
        rule.SetCategory(entry.epgInfo.Category());
        rule.SetProgramID(entry.epgInfo.ProgramID());
        rule.SetSeriesID(entry.epgInfo.SeriesID());
        return rule;
      }
      if (entry.HasChannel() || entry.isAnyChannel) //Valid edit case
      {
        rule.SetChannelID(entry.chanid);
        rule.SetCallsign(entry.callsign);
        rule.SetStartTime(entry.startTime);
        rule.SetEndTime(entry.endTime);
        rule.SetTitle(entry.title);
        rule.SetSubtitle(entry.subtitle);
        rule.SetDescription(entry.description);
        rule.SetCategory(entry.category);
        return rule;
      }
      break;
    }

    case TIMER_TYPE_RECORD_WEEKLY:
    {
      rule.SetType(Myth::RT_AllRecord);
      rule.SetFilter(Myth::FM_ThisChannel + Myth::FM_ThisDayAndTime);
      rule.SetSearchType(Myth::ST_NoSearch);
      rule.SetInactive(entry.isInactive);
      rule.SetDuplicateControlMethod(Myth::DM_CheckNone);
      if (!entry.epgInfo.IsNull()) //EPG Version
      {
        rule.SetChannelID(entry.epgInfo.ChannelID());
        rule.SetCallsign(entry.epgInfo.Callsign());
        rule.SetStartTime(entry.epgInfo.StartTime());
        rule.SetEndTime(entry.epgInfo.EndTime());
        rule.SetTitle(entry.epgInfo.Title());
        rule.SetSubtitle(entry.epgInfo.Subtitle());
        rule.SetDescription(entry.epgInfo.Description());
        rule.SetCategory(entry.epgInfo.Category());
        rule.SetProgramID(entry.epgInfo.ProgramID());
        rule.SetSeriesID(entry.epgInfo.SeriesID());
        return rule;
      }
      if (!entry.isAnyChannel && entry.HasChannel() && entry.HasTimeSlot()) //Manual Version and edit case
      {
        rule.SetChannelID(entry.chanid);
        rule.SetCallsign(entry.callsign);
        rule.SetStartTime(entry.startTime);
        rule.SetEndTime(entry.endTime);
        rule.SetTitle(entry.title);
        rule.SetSubtitle(entry.subtitle);
        rule.SetDescription(entry.description);
        rule.SetCategory(entry.category);
        return rule;
      }
      if (entry.isAnyChannel)
        XBMC->Log(LOG_ERROR, "%s (MythScheduleHelper76) AnyChannel is not supported for this Timer Type.", __FUNCTION__);
      break;
    }

    case TIMER_TYPE_RECORD_DAILY:
    {
      rule.SetType(Myth::RT_AllRecord);
      rule.SetFilter(Myth::FM_ThisChannel + Myth::FM_ThisTime);
      rule.SetSearchType(Myth::ST_NoSearch);
      rule.SetInactive(entry.isInactive);
      rule.SetDuplicateControlMethod(Myth::DM_CheckNone);
      if (!entry.epgInfo.IsNull()) //EPG Version
      {
        rule.SetChannelID(entry.epgInfo.ChannelID());
        rule.SetCallsign(entry.epgInfo.Callsign());
        rule.SetStartTime(entry.epgInfo.StartTime());
        rule.SetEndTime(entry.epgInfo.EndTime());
        rule.SetTitle(entry.epgInfo.Title());
        rule.SetSubtitle(entry.epgInfo.Subtitle());
//        rule.SetDescription(entry.epgInfo.Description()); TODO Invalid Overloaded Type??
        rule.SetCategory(entry.epgInfo.Category());
        rule.SetProgramID(entry.epgInfo.ProgramID());
        rule.SetSeriesID(entry.epgInfo.SeriesID());
        return rule;
      }
      if (!entry.isAnyChannel && entry.HasChannel() && entry.HasTimeSlot()) //Manual version and edit case
      {
        rule.SetChannelID(entry.chanid);
        rule.SetCallsign(entry.callsign);
        rule.SetStartTime(entry.startTime);
        rule.SetEndTime(entry.endTime);
        rule.SetTitle(entry.title);
        rule.SetSubtitle(entry.subtitle);
        rule.SetDescription(entry.description);
        rule.SetCategory(entry.category);
        return rule;
      }
      if (entry.isAnyChannel)
        XBMC->Log(LOG_ERROR, "%s (MythScheduleHelper76) AnyChannel is not supported for this Timer Type.", __FUNCTION__);
      break;
    }

    case TIMER_TYPE_RECORD_ALL:
    {
      rule.SetType(Myth::RT_AllRecord);
      rule.SetSearchType(Myth::ST_NoSearch);
      if (!entry.isAnyChannel) rule.SetFilter(Myth::FM_ThisChannel);
      rule.SetInactive(entry.isInactive);
      if (!entry.epgInfo.IsNull()) // EPG only
      {
        if (entry.isAnyChannel) //Grab channel info while we still have it
        {
          rule.SetChannelID(entry.epgInfo.ChannelID());
          rule.SetCallsign(entry.epgInfo.Callsign());
        }
        rule.SetStartTime(entry.epgInfo.StartTime());
        rule.SetEndTime(entry.epgInfo.EndTime());
        rule.SetTitle(entry.epgInfo.Title());
        rule.SetSubtitle(entry.epgInfo.Subtitle());
//        rule.SetDescription(entry.epgInfo.Description()); TODO Invalid Overloaded Type??
        rule.SetCategory(entry.epgInfo.Category());
        rule.SetProgramID(entry.epgInfo.ProgramID());
        rule.SetSeriesID(entry.epgInfo.SeriesID());
        return rule;
      }
      if (entry.HasChannel() || entry.isAnyChannel) //Valid edit case
      {
        rule.SetChannelID(entry.chanid);
        rule.SetCallsign(entry.callsign);
        rule.SetStartTime(entry.startTime);
        rule.SetEndTime(entry.endTime);
        rule.SetTitle(entry.title);
        rule.SetSubtitle(entry.subtitle);
        rule.SetDescription(entry.description);
        rule.SetCategory(entry.category);
        return rule;
      }
      break;
    }

    case TIMER_TYPE_TEXT_SEARCH: //Manual only but fields pre-populated by Kodi Core usng EPG data
    {
      rule.SetType(Myth::RT_AllRecord);
      if (!entry.isAnyChannel) 
      {
        rule.SetFilter(Myth::FM_ThisChannel);
        rule.SetChannelID(entry.chanid);
        rule.SetCallsign(entry.callsign);
      }
      rule.SetTitle(entry.title);
      if (entry.isFullTextSearch)
        rule.SetSearchType(Myth::ST_KeywordSearch);
      else
        rule.SetSearchType(Myth::ST_TitleSearch);
      rule.SetInactive(entry.isInactive);

      if (!entry.epgSearch.empty())
      {
        // Backend uses subtitle/description to store search parameters
        rule.SetSubtitle(""); // Backend uses Subtitle for table join SQL for power searches (not needed for keyword or title)
        rule.SetDescription(entry.epgSearch); // Backend uses description to store Keyword or Title search string and SQL for Power Search
        return rule;
      }
      else
        XBMC->Log(LOG_ERROR, "%s (MythScheduleHelper76) Empty search string not supported.", __FUNCTION__);
      break;
    }

    case TIMER_TYPE_DONT_RECORD:
    case TIMER_TYPE_OVERRIDE:
    case TIMER_TYPE_UPCOMING:
    case TIMER_TYPE_UPCOMING_MANUAL:
    case TIMER_TYPE_ZOMBIE:
      // any type
      rule.SetType(Myth::RT_NotRecording);
      rule.SetChannelID(entry.chanid);
      rule.SetCallsign(entry.callsign);
      rule.SetStartTime(entry.startTime);
      rule.SetEndTime(entry.endTime);
      rule.SetTitle(entry.title);
      rule.SetSubtitle(entry.subtitle);
      rule.SetDescription(entry.description);
      rule.SetInactive(entry.isInactive);
      return rule;

    default:
      break;
  }
  rule.SetType(Myth::RT_UNKNOWN);
  XBMC->Log(LOG_ERROR, "%s (MythScheduleHelper76) - Invalid timer %u: TYPE=%d CHANID=%u SIGN=%s ST=%u ET=%u", __FUNCTION__, entry.entryIndex,
          entry.timerType, entry.chanid, entry.callsign.c_str(), (unsigned)entry.startTime, (unsigned)entry.endTime);
  return rule;
}

MythScheduleManager::RuleSummaryInfo MythScheduleHelper76::GetSummaryInfo(const MythRecordingRule &rule) const
{
  MythScheduleManager::RuleSummaryInfo meta;
  time_t st = rule.StartTime();
  meta.isRepeating = false;
  meta.weekDays = 0;
  meta.marker = "";
  switch (rule.Type())
  {
    case Myth::RT_DailyRecord:
    case Myth::RT_FindDailyRecord:
      meta.isRepeating = true;
      meta.weekDays = 0x7F;
      meta.marker = "d";
      break;
    case Myth::RT_WeeklyRecord:
    case Myth::RT_FindWeeklyRecord:
      meta.isRepeating = true;
      meta.weekDays = 1 << ((weekday(&st) + 6) % 7);
      meta.marker = "w";
      break;
    case Myth::RT_ChannelRecord:
      meta.isRepeating = true;
      meta.weekDays = 0x7F;
      meta.marker = "C";
      break;
    case Myth::RT_AllRecord:
      meta.isRepeating = true;
      if ((rule.Filter() & Myth::FM_ThisDayAndTime))
      {
        meta.weekDays = 1 << ((weekday(&st) + 6) % 7);
        meta.marker = "w";
      }
      else if ((rule.Filter() & Myth::FM_ThisTime))
      {
        meta.weekDays = 0x7F;
        meta.marker = "d";
      }
      else
      {
        meta.weekDays = 0x7F;
        meta.marker = "A";
      }
      break;
    case Myth::RT_OneRecord:
      meta.isRepeating = false;
      meta.weekDays = 0;
      meta.marker = "1";
      break;
    case Myth::RT_DontRecord:
      meta.isRepeating = false;
      meta.weekDays = 0;
      meta.marker = "x";
      break;
    case Myth::RT_OverrideRecord:
      meta.isRepeating = false;
      meta.weekDays = 0;
      meta.marker = "o";
      break;
    default:
      break;
  }
  return meta;
}

MythRecordingRule MythScheduleHelper76::NewDailyRecord(const MythEPGInfo& epgInfo)
{
  unsigned int filter;
  MythRecordingRule rule = this->NewFromTemplate(epgInfo);

  rule.SetType(Myth::RT_AllRecord);
  filter = Myth::FM_ThisChannel + Myth::FM_ThisTime;
  rule.SetFilter(filter);

  if (!epgInfo.IsNull())
  {
    rule.SetSearchType(Myth::ST_NoSearch);
    rule.SetChannelID(epgInfo.ChannelID());
    rule.SetStartTime(epgInfo.StartTime());
    rule.SetEndTime(epgInfo.EndTime());
    rule.SetTitle(epgInfo.Title());
    rule.SetSubtitle(epgInfo.Subtitle());
    rule.SetCategory(epgInfo.Category());
    rule.SetDescription(epgInfo.Description());
    rule.SetCallsign(epgInfo.Callsign());
    rule.SetProgramID(epgInfo.ProgramID());
    rule.SetSeriesID(epgInfo.SeriesID());
  }
  else
  {
    // No EPG! Create custom daily for this channel
    rule.SetType(Myth::RT_DailyRecord);
    rule.SetFilter(Myth::FM_ThisChannel);
    // Using timeslot
    rule.SetSearchType(Myth::ST_ManualSearch);
  }
  rule.SetDuplicateControlMethod(Myth::DM_CheckSubtitleAndDescription);
  rule.SetCheckDuplicatesInType(Myth::DI_InAll);
  rule.SetInactive(false);
  return rule;
}

MythRecordingRule MythScheduleHelper76::NewWeeklyRecord(const MythEPGInfo& epgInfo)
{
  unsigned int filter;
  MythRecordingRule rule = this->NewFromTemplate(epgInfo);

  rule.SetType(Myth::RT_AllRecord);
  filter = Myth::FM_ThisChannel + Myth::FM_ThisDayAndTime;
  rule.SetFilter(filter);

  if (!epgInfo.IsNull())
  {
    rule.SetSearchType(Myth::ST_NoSearch);
    rule.SetChannelID(epgInfo.ChannelID());
    rule.SetStartTime(epgInfo.StartTime());
    rule.SetEndTime(epgInfo.EndTime());
    rule.SetTitle(epgInfo.Title());
    rule.SetSubtitle(epgInfo.Subtitle());
    rule.SetCategory(epgInfo.Category());
    rule.SetDescription(epgInfo.Description());
    rule.SetCallsign(epgInfo.Callsign());
    rule.SetProgramID(epgInfo.ProgramID());
    rule.SetSeriesID(epgInfo.SeriesID());
  }
  else
  {
    // No EPG! Create custom weekly for this channel
    rule.SetType(Myth::RT_WeeklyRecord);
    rule.SetFilter(Myth::FM_ThisChannel);
    // Using timeslot
    rule.SetSearchType(Myth::ST_ManualSearch);
  }
  rule.SetDuplicateControlMethod(Myth::DM_CheckSubtitleAndDescription);
  rule.SetCheckDuplicatesInType(Myth::DI_InAll);
  rule.SetInactive(false);
  return rule;
}

MythRecordingRule MythScheduleHelper76::NewChannelRecord(const MythEPGInfo& epgInfo)
{
  unsigned int filter;
  MythRecordingRule rule = this->NewFromTemplate(epgInfo);

  rule.SetType(Myth::RT_AllRecord);
  filter = Myth::FM_ThisChannel;
  rule.SetFilter(filter);

  if (!epgInfo.IsNull())
  {
    rule.SetSearchType(Myth::ST_NoSearch);
    rule.SetChannelID(epgInfo.ChannelID());
    rule.SetStartTime(epgInfo.StartTime());
    rule.SetEndTime(epgInfo.EndTime());
    rule.SetTitle(epgInfo.Title());
    rule.SetSubtitle(epgInfo.Subtitle());
    rule.SetCategory(epgInfo.Category());
    rule.SetDescription(epgInfo.Description());
    rule.SetCallsign(epgInfo.Callsign());
    rule.SetProgramID(epgInfo.ProgramID());
    rule.SetSeriesID(epgInfo.SeriesID());
  }
  else
  {
    // Not feasible
    rule.SetType(Myth::RT_NotRecording);
  }
  rule.SetDuplicateControlMethod(Myth::DM_CheckSubtitleAndDescription);
  rule.SetCheckDuplicatesInType(Myth::DI_InAll);
  rule.SetInactive(false);
  return rule;
}

MythRecordingRule MythScheduleHelper76::NewOneRecord(const MythEPGInfo& epgInfo)
{
  unsigned int filter;
  MythRecordingRule rule = this->NewFromTemplate(epgInfo);

  rule.SetType(Myth::RT_OneRecord);
  filter = Myth::FM_ThisEpisode;
  rule.SetFilter(filter);

  if (!epgInfo.IsNull())
  {
    rule.SetSearchType(Myth::ST_NoSearch);
    rule.SetChannelID(epgInfo.ChannelID());
    rule.SetStartTime(epgInfo.StartTime());
    rule.SetEndTime(epgInfo.EndTime());
    rule.SetTitle(epgInfo.Title());
    rule.SetSubtitle(epgInfo.Subtitle());
    rule.SetCategory(epgInfo.Category());
    rule.SetDescription(epgInfo.Description());
    rule.SetCallsign(epgInfo.Callsign());
    rule.SetProgramID(epgInfo.ProgramID());
    rule.SetSeriesID(epgInfo.SeriesID());
  }
  else
  {
    // Not feasible
    rule.SetType(Myth::RT_NotRecording);
  }
  rule.SetDuplicateControlMethod(Myth::DM_CheckSubtitleAndDescription);
  rule.SetCheckDuplicatesInType(Myth::DI_InAll);
  rule.SetInactive(false);
  return rule;
}
