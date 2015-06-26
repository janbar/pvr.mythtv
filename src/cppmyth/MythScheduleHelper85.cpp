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
//// Version helper for backend version 85 (0.28-pre)
////

#include "MythScheduleHelper85.h"
#include "../client.h"
#include "../tools.h"

#include <cstdio>
#include <cassert>

using namespace ADDON;

bool MythScheduleHelper85::FillTimerEntry(MythTimerEntry& entry, const MythProgramInfo& recording) const
{
  //Only include timers which have an inactive status if the user has requested it (flag m_showNotRecording)
  switch (recording.Status())
  {
    //Upcoming recordings which are disabled due to being lower priority duplicates or already recorded
    case Myth::RS_EARLIER_RECORDING:  //will record earlier
    case Myth::RS_LATER_SHOWING:      //will record later
    case Myth::RS_CURRENT_RECORDING:  //Already in the current library
    case Myth::RS_PREVIOUS_RECORDING: //Previoulsy recorded but no longer in the library
      if (true /*!m_showNotRecording*/)
      {
        XBMC->Log(LOG_DEBUG, "%s (MythScheduleHelper85): Skipping %s:%s on %s status %d", __FUNCTION__,
                  recording.Title().c_str(), recording.Subtitle().c_str(), recording.ChannelName().c_str(),
                  recording.Status());
        return false;
      }
    default:
      break;
  }

  MythRecordingRuleNodePtr node = m_manager->FindRuleById(recording.RecordID());
  if (node)
  {
    MythRecordingRule rule = node->GetRule();
    XBMC->Log(LOG_DEBUG,"%s (MythScheduleHelper85): MythProgramInfo-> %u (%s:%s)", __FUNCTION__
                  , rule.RecordID(), rule.Title().c_str(), rule.Subtitle().c_str());
    // Relate the main rule as parent
    entry.parentIndex = MythScheduleManager::MakeIndex(node->GetMainRule());
    switch (rule.Type())
    {
      case Myth::RT_SingleRecord:
        return false; // Discard upcoming. We show only main rule.
      case Myth::RT_DontRecord:
        entry.recordingStatus = Myth::RS_UNKNOWN; // Show modifier status
        entry.timerType = TIMER_TYPE_DONT_RECORD;
        entry.isInactive = rule.Inactive();
        break;
      case Myth::RT_OverrideRecord:
        entry.recordingStatus = Myth::RS_UNKNOWN; // Show modifier status
        entry.timerType = TIMER_TYPE_OVERRIDE;
        entry.isInactive = rule.Inactive();
        break;
      default:
        entry.recordingStatus = recording.Status();
        if (node->GetMainRule().SearchType() == Myth::ST_ManualSearch)
          entry.timerType = TIMER_TYPE_UPCOMING_MANUAL;
        else
          entry.timerType = TIMER_TYPE_UPCOMING;
    }
    entry.startOffset = rule.StartOffset();
    entry.endOffset = rule.EndOffset();
    entry.priority = rule.Priority();
    entry.autoExpire = rule.AutoExpire();
  }
  else
    entry.timerType = TIMER_TYPE_ZOMBIE;

  entry.description = "";
  entry.chanid = recording.ChannelID();
  entry.callsign = recording.Callsign();
  entry.startTime = recording.StartTime();
  entry.endTime = recording.EndTime();
  entry.title.assign(recording.Title());
  if (!recording.Subtitle().empty())
    entry.title.append(" - ").append(recording.Subtitle());
  if (recording.Season() || recording.Episode())
    entry.title.append(" - ").append(Myth::IntToString(recording.Season())).append(".").append(Myth::IntToString(recording.Episode()));
  entry.recordingGroup = GetRuleRecordingGroupId(recording.RecordingGroup());
  entry.entryIndex = MythScheduleManager::MakeIndex(recording); // upcoming index
  return true;
}
