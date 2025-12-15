#pragma once
/*
 *  Copyright (C) 2018-2024 Jean-Luc Barrière
 *  Copyright (C) 2018 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "demuxer/tsDemuxer.h"

#include <mythstream.h>
#include <kodi/addon-instance/PVR.h>
#include "private/os/threads/thread.h"
#include "private/os/threads/mutex.h"
#include "settings.h"
#include "fifo.h"

#include <vector>
#include <list>
#include <set>

#define AV_BUFFER_SIZE          131072

class ATTR_DLL_LOCAL Demux : public TSDemux::TSDemuxer, Myth::OS::Thread
{
public:
  Demux(kodi::addon::CInstancePVRClient& handler, Myth::Stream *file, time_t starttime);
  ~Demux();

  const unsigned char* ReadAV(uint64_t pos, size_t n);

  void* Process();

  bool GetStreamProperties(std::vector<kodi::addon::PVRStreamProperties>& props);
  void Flush();
  void Abort();
  DEMUX_PACKET* Read();
  bool SeekTime(double time, bool backwards, double* startpts);

  int GetPlayingTime();
  time_t GetStartTime();
  int64_t GetStartPTS();
  int64_t GetEndPTS();

private:
  kodi::addon::CInstancePVRClient& m_handler;
  Myth::Stream *m_file;
  double m_starttime;
  uint16_t m_channel;
  FIFO<DEMUX_PACKET*> m_demuxPacketBuffer;
  Myth::OS::Mutex m_lock;
  std::vector<kodi::addon::PVRStreamProperties> m_streams;

  bool get_stream_data(TSDemux::STREAM_PKT* pkt);
  void reset_posmap();

  // PVR interfaces
  void populate_pvr_streams();
  bool update_pvr_stream(TSDemux::ElementaryStream* es);
  void push_stream_change();
  DEMUX_PACKET* stream_pvr_data(TSDemux::STREAM_PKT* pkt);
  bool push_stream_data(DEMUX_PACKET* dxp);

  // AV raw buffer
  size_t m_av_buf_size;         ///< size of av buffer
  uint64_t m_av_pos;            ///< absolute position in av
  unsigned char* m_av_buf;      ///< buffer
  unsigned char* m_av_rbs;      ///< raw data start in buffer
  unsigned char* m_av_rbe;      ///< raw data end in buffer

  // Playback context
  TSDemux::AVContext* m_AVContext;
  uint16_t m_mainStreamPID;     ///< PID of main stream
  uint64_t m_pts;               ///< presentation time of main stream
  int64_t m_pinTime;            ///< pinned relative position (90Khz)
  int64_t m_curTime;            ///< current relative position (90Khz)
  int64_t m_endTime;            ///< last relative marked position (90Khz))
  typedef struct
  {
    int64_t  time_pts;
    uint64_t av_pts;
    uint64_t av_pos;
  } AV_POSMAP_ITEM;
  std::list<AV_POSMAP_ITEM> m_posmap;

  bool m_isChangePlaced;
  std::set<uint16_t> m_nosetup;

};
