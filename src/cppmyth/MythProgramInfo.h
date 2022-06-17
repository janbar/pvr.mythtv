/*
 *  Copyright (C) 2013-2021 Jean-Luc Barrière
 *  Copyright (C) 2005-2014 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <mythtypes.h>

class MythProgramInfo;
typedef std::map<std::string, MythProgramInfo> ProgramInfoMap;

class MythProgramInfo
{
public:

  MythProgramInfo();
  MythProgramInfo(Myth::ProgramPtr proginfo);

  bool IsNull() const;
  Myth::ProgramPtr GetPtr() const;
  bool operator ==(const MythProgramInfo &other);
  bool operator !=(const MythProgramInfo &other);

  /// Reset custom flags and properties
  void ResetProps() {  m_flags = 0; m_props.reset(new Props()); }
  /// Copy reference of properties from other
  void CopyProps(const MythProgramInfo &other) { m_props = other.m_props; }
  // Custom flags
  bool IsVisible() const;
  bool IsDeleted() const;
  bool IsLiveTV() const;
  bool HasCoverart() const;
  bool HasFanart() const;
  // Custom props
  void SetPropsVideoFrameRate(float fps);
  float GetPropsVideoFrameRate() const;
  void SetPropsVideoAspec(float aspec);
  float GetPropsVideoAspec() const;
  void SetPropsSerie(bool flag);
  bool GetPropsSerie() const;
  void SetPropsBookmark(int seconds);
  int GetPropsBookmark() const;
  // Program fields
  std::string UID() const;
  std::string ProgramID() const;
  std::string SerieID() const;
  std::string Title() const;
  std::string Subtitle() const;
  std::string HostName() const;
  std::string FileName() const;
  std::string Description() const;
  int Duration() const;
  std::string Category() const;
  time_t StartTime() const;
  time_t EndTime() const;
  bool IsWatched() const;
  bool IsDeletePending() const;
  bool HasBookmark() const;
  uint32_t ChannelID() const;
  std::string ChannelName() const;
  std::string Callsign() const;
  Myth::RS_t Status() const;
  std::string RecordingGroup() const;
  uint32_t RecordID() const;
  time_t RecordingStartTime() const;
  time_t RecordingEndTime() const;
  int Priority() const;
  std::string StorageGroup() const;
  std::string Inetref() const;
  uint16_t Season() const;
  uint16_t Episode() const;
  std::string Airdate() const;
  bool IsDamaged(uint32_t schemaVersion) const;
  int64_t FileSize() const;

  std::string GroupingTitle() const;

private:
  Myth::ProgramPtr m_proginfo;
  mutable int32_t m_flags;
  mutable std::string m_groupingTitle;

  class Props
  {
  public:
    Props()
    : m_videoFrameRate(0)
    , m_videoAspec(0)
    , m_serie(false)
    , m_bookmark(0)
    {}
    ~Props() {}

    float m_videoFrameRate;
    float m_videoAspec;
    bool m_serie;               ///< true if program is serie else false
    int m_bookmark;             ///< last played position in seconds
  };
  MYTH_SHARED_PTR<Props> m_props;

  bool IsSetup() const;
};
