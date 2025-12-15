/*
 *  Copyright (C) 2018-2024 Jean-Luc Barrière
 *  Copyright (C) 2018 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "demux.h"
#include "demuxer/debug.h"

#include <climits>

#define LOGTAG                  "[DEMUX] "
#define POSMAP_PTS_INTERVAL     (PTS_TIME_BASE * 2)       // 2 units (90kHz)
#define DISCONTINUITY_THRESHOLD (PTS_TIME_BASE * 3)       // 3 units (90kHz)
#define READAV_TIMEOUT          10000                     // 10 secs
#define FIFO_TIMEOUT_USEC       10000                     // 10 millisecs

void DemuxLog(int level, char *msg)
{
  if (msg && level != DEMUX_DBG_NONE)
  {
    ADDON_LOG loglevel = ADDON_LOG_DEBUG;
    switch (level)
    {
    case DEMUX_DBG_ERROR:
      loglevel = ADDON_LOG_ERROR;
      break;
    case DEMUX_DBG_WARN:
    case DEMUX_DBG_INFO:
      loglevel = ADDON_LOG_INFO;
      break;
    case DEMUX_DBG_DEBUG:
    case DEMUX_DBG_PARSE:
    case DEMUX_DBG_ALL:
      loglevel = ADDON_LOG_DEBUG;
      break;
    }
    kodi::Log(loglevel, LOGTAG "%s", msg);
  }
}

Demux::Demux(kodi::addon::CInstancePVRClient& handler, Myth::Stream *file, time_t starttime)
  : Thread()
  , m_handler(handler)
  , m_file(file)
  , m_starttime((double)starttime)
  , m_channel(1)
  , m_demuxPacketBuffer(100)
  , m_av_buf_size(AV_BUFFER_SIZE)
  , m_av_pos(0)
  , m_av_buf(NULL)
  , m_av_rbs(NULL)
  , m_av_rbe(NULL)
  , m_AVContext(NULL)
  , m_mainStreamPID(0xffff)
  , m_pts(PTS_UNSET)
  , m_pinTime(0)
  , m_curTime(0)
  , m_endTime(0)
  , m_isChangePlaced(false)
{
  m_av_buf = (unsigned char*)malloc(sizeof(*m_av_buf) * (m_av_buf_size + 1));
  if (m_av_buf)
  {
    m_av_rbs = m_av_buf;
    m_av_rbe = m_av_buf;

    if (CMythSettings::GetExtraDebug())
      TSDemux::DBGLevel(DEMUX_DBG_DEBUG);
    else
      TSDemux::DBGLevel(DEMUX_DBG_ERROR);
    TSDemux::SetDBGMsgCallback(DemuxLog);

    m_AVContext = new TSDemux::AVContext(this, m_av_pos, m_channel);

    StartThread(true);
  }
  else
  {
    kodi::Log(ADDON_LOG_ERROR, LOGTAG "alloc AV buffer failed");
  }
}

Demux::~Demux()
{
  Abort();

  // Free AV context
  if (m_AVContext)
    delete m_AVContext;
  // Free AV buffer
  if (m_av_buf)
  {
    kodi::Log(ADDON_LOG_DEBUG, LOGTAG "free AV buffer: allocated size was %zu", m_av_buf_size);
    free(m_av_buf);
    m_av_buf = NULL;
  }
}

/*
 * Implement our AV reader
 */
const unsigned char* Demux::ReadAV(uint64_t pos, size_t n)
{
  // out of range
  if (n > m_av_buf_size)
    return NULL;

  // Already read ?
  size_t sz = m_av_rbe - m_av_buf;
  if (pos < m_av_pos || pos > (m_av_pos + sz))
  {
    // seek and reset buffer
    int64_t newpos = m_file->Seek((int64_t)pos, Myth::WHENCE_SET);
    if (newpos < 0)
      return NULL;
    m_av_pos = pos = (uint64_t)newpos;
    m_av_rbs = m_av_rbe = m_av_buf;
  }
  else
  {
    // move to the desired pos in buffer
    m_av_rbs = m_av_buf + (size_t)(pos - m_av_pos);
  }

  size_t dataread = m_av_rbe - m_av_rbs;
  if (dataread >= n)
    return m_av_rbs;
  // flush old data to free up space at the end
  memmove(m_av_buf, m_av_rbs, dataread);
  m_av_rbs = m_av_buf;
  m_av_rbe = m_av_rbs + dataread;
  m_av_pos = pos;
  // fill new data
  unsigned int len = (unsigned int)(m_av_buf_size - dataread);
  Myth::OS::Timeout timeout;
  while (!IsStopped())
  {
    int ret = m_file->Read(m_av_rbe, len);
    if (ret > 0)
    {
      m_av_rbe += ret;
      dataread += ret;
      len -= ret;
    }
    if (dataread >= n || ret < 0)
      break;
    if (!(timeout.IsSet()))
      timeout.Set(READAV_TIMEOUT);
    else if (!timeout.TimeLeft())
      break;
    usleep(100000);
  }
  return dataread >= n ? m_av_rbs : NULL;
}

void* Demux::Process()
{
  if (!m_AVContext)
  {
    kodi::Log(ADDON_LOG_ERROR, LOGTAG "%s: no AVContext", __FUNCTION__);
    return NULL;
  }

  int ret = 0;

  while (!IsStopped())
  {
    {
      Myth::OS::LockGuard guard(m_lock);
      ret = m_AVContext->TSResync();
    }
    if (ret != TSDemux::AVCONTEXT_CONTINUE)
      break;

    ret = m_AVContext->ProcessTSPacket();

    if (m_AVContext->HasPIDStreamData())
    {
      TSDemux::STREAM_PKT pkt;
      while (get_stream_data(&pkt))
      {
        push_stream_data(stream_pvr_data(&pkt));
      }
    }
    if (m_AVContext->HasPIDPayload())
    {
      ret = m_AVContext->ProcessTSPayload();
      if (ret == TSDemux::AVCONTEXT_PROGRAM_CHANGE)
      {
        populate_pvr_streams();
        push_stream_change();
      }
    }

    if (ret < 0)
      kodi::Log(ADDON_LOG_INFO, LOGTAG "%s: error %d", __FUNCTION__, ret);

    if (ret == TSDemux::AVCONTEXT_TS_ERROR)
      m_AVContext->Shift();
    else
      m_AVContext->GoNext();
  }

  kodi::Log(ADDON_LOG_DEBUG, LOGTAG "%s: stopped with status %d", __FUNCTION__, ret);
  return NULL;
}

bool Demux::GetStreamProperties(std::vector<kodi::addon::PVRStreamProperties>& props)
{
  if (!m_nosetup.empty())
    kodi::Log(ADDON_LOG_WARNING, LOGTAG "%s: incomplete setup", __FUNCTION__);

  Myth::OS::LockGuard guard(m_lock);
  m_isChangePlaced = false;
  for (std::vector<kodi::addon::PVRStreamProperties>::const_iterator it = m_streams.begin(); it != m_streams.end(); ++it)
    props.push_back(*it);

  return true;
}

void Demux::Flush(void)
{
  Myth::OS::LockGuard guard(m_lock);
  DEMUX_PACKET* pkt(NULL);
  while (m_demuxPacketBuffer.pop(pkt))
    m_handler.FreeDemuxPacket(pkt);
}

void Demux::Abort()
{
  StopThread(true);
  Flush();
  m_streams.clear();
}

DEMUX_PACKET* Demux::Read()
{
  DEMUX_PACKET* packet(NULL);
  if (IsStopped())
    return packet;
  if (m_demuxPacketBuffer.pop(packet, 100))
    return packet;
  return m_handler.AllocateDemuxPacket(0);
}

bool Demux::SeekTime(double time, bool backwards, double* startpts)
{
  // positions map  must be filled to estimate offset
  if (m_posmap.empty())
    return false;

  StopThread(true);

  Myth::OS::LockGuard guard(m_lock);

  // Current PTS must be valid to estimate offset
  int64_t firstpts = m_posmap.front().av_pts;

  // time is in MSEC not PTS_TIME_BASE. Rescale time to PTS (90Khz)
  int64_t pts = (int64_t)(time * PTS_TIME_BASE / 1000);
  int64_t time_pts = pts - firstpts;
  const AV_POSMAP_ITEM * pos = nullptr;

  kodi::Log(ADDON_LOG_INFO, LOGTAG "%s: bw=%d desired=%" PRId64 " beg=%" PRId64 " cur=%" PRId64 " end=%" PRId64, __FUNCTION__, backwards,
          pts, firstpts, firstpts + m_curTime, firstpts + m_endTime);

  if (backwards)
  {
    std::list<AV_POSMAP_ITEM>::const_reverse_iterator it;
    for (it = m_posmap.rbegin(); it != m_posmap.rend(); ++it)
    {
      if (it->time_pts <= time_pts)
        break;
    }
    if (it != m_posmap.rend())
      pos = &(*it);
    else if (m_curTime > POSMAP_PTS_INTERVAL && it != m_posmap.rbegin())
      pos = &(*(--it));
  }
  else
  {
    std::list<AV_POSMAP_ITEM>::const_iterator it;
    for (it = m_posmap.begin(); it != m_posmap.end(); ++it)
    {
      if (it->time_pts >= time_pts)
        break;
    }
    if (it != m_posmap.end())
      pos = &(*it);
    else if (m_curTime < m_endTime && it != m_posmap.begin())
      pos = &(*(--it));
  }

  if (pos)
  {
    Flush();
    m_AVContext->GoPosition(pos->av_pos);
    m_AVContext->ResetPackets();
    m_curTime = m_pinTime = pos->time_pts;
    m_pts = pos->av_pts;
    *startpts = (double)m_pts * STREAM_TIME_BASE / PTS_TIME_BASE;
    kodi::Log(ADDON_LOG_INFO, LOGTAG "seek to %" PRId64, (firstpts + m_curTime));
  }
  else
  {
    kodi::Log(ADDON_LOG_WARNING, LOGTAG "seek aborted");
  }

  StartThread(true);

  return (pos != nullptr);
}

int Demux::GetPlayingTime()
{
  Myth::OS::LockGuard guard(m_lock);
  double time_ms = (double)m_curTime * 1000 / PTS_TIME_BASE;
  if (time_ms > INT_MAX)
    return INT_MAX;
  return (int)time_ms;
}

time_t Demux::GetStartTime()
{
  Myth::OS::LockGuard guard(m_lock);
  return (time_t)m_starttime;
}

int64_t Demux::GetStartPTS()
{
  Myth::OS::LockGuard guard(m_lock);
  if (m_posmap.empty())
    return PTS_UNSET;
  return (int64_t)((double)(m_posmap.front().av_pts) * STREAM_TIME_BASE / PTS_TIME_BASE);
}

int64_t Demux::GetEndPTS()
{
  Myth::OS::LockGuard guard(m_lock);
  if (m_posmap.empty())
    return PTS_UNSET;
  return (int64_t)((double)(m_posmap.back().av_pts) * STREAM_TIME_BASE / PTS_TIME_BASE);
}

bool Demux::get_stream_data(TSDemux::STREAM_PKT* pkt)
{
  TSDemux::ElementaryStream* es = m_AVContext->GetPIDStream();
  if (!es)
    return false;

  if (!es->GetStreamPacket(pkt))
    return false;

  if (pkt->streamChange)
  {
    // Update stream properties. Change will be pushed once setup is completed for all streams.
    if (update_pvr_stream(es) && m_nosetup.empty())
      push_stream_change();
  }

  // start by mainstream frame
  if (m_posmap.empty() && pkt->pid != m_mainStreamPID)
    return false;

  // check for a valid duration
  if (pkt->duration > POSMAP_PTS_INTERVAL)
  {
    pkt->duration = 0;
  }
  else if (pkt->pid == m_mainStreamPID)
  {
    Myth::OS::LockGuard guard(m_lock);

    // no discontinuity is permitted to allow seeking in the stream, therefore
    // it is necessary to manage this case which could arise when passing to the
    // next chained stream.
    // on discontinuity the posmap must be cleared and starttime must be reset
    // to the current playing time.
    if (!m_posmap.empty())
    {
      int64_t diffts = (pkt->pts > m_pts ? pkt->pts - m_pts : m_pts - pkt->pts);
      if (diffts > DISCONTINUITY_THRESHOLD)
      {
        // reset posmap
        kodi::Log(ADDON_LOG_WARNING, LOGTAG "need to reset posmap: %" PRId64 " -> %" PRId64, m_pts, pkt->pts);
        reset_posmap();
      }
    }

    // Sync main PTS
    m_pts = pkt->pts;

    // Fill duration map for main stream
    m_curTime += pkt->duration;
    if (m_curTime >= m_pinTime)
    {
      m_pinTime += POSMAP_PTS_INTERVAL;
      if (m_curTime > m_endTime)
      {
        AV_POSMAP_ITEM item;
        item.time_pts = m_curTime;
        item.av_pts = pkt->pts;
        item.av_pos = m_AVContext->GetPosition();
        m_posmap.push_back(item);
        m_endTime = m_curTime;
      }
    }
  }
  return true;
}

void Demux::reset_posmap()
{
  Myth::OS::LockGuard guard(m_lock);
  if (m_posmap.empty())
    return;
  // adjust start time to current time
  m_starttime += (double)m_curTime / PTS_TIME_BASE;
  m_pinTime = m_curTime = m_endTime = 0;
  m_posmap.clear();
}

static inline int stream_identifier(int composition_id, int ancillary_id)
{
  return (composition_id & 0xffff) | ((ancillary_id & 0xffff) << 16);
}

static void recode_language(const char* muxLanguage, char* strLanguage)
{
  /*
   * While XBMC does'nt support them.
   * Fix unsupported language codes (EN 300 468 Annex F & J)
   * 'qaa'        : Original audio
   * 'qad','NAR'  : Audio Description
   */
  if (strncmp(muxLanguage, "qaa", 3) == 0 ||
      strncmp(muxLanguage, "qad", 3) == 0 ||
      strncmp(muxLanguage, "NAR", 3) == 0)
  {
    strLanguage[0] = 0;
    strLanguage[1] = 0;
    strLanguage[2] = 0;
    strLanguage[3] = 0;
  }
  else
  {
    strLanguage[0] = muxLanguage[0];
    strLanguage[1] = muxLanguage[1];
    strLanguage[2] = muxLanguage[2];
    strLanguage[3] = 0;
  }
}

void Demux::populate_pvr_streams()
{
  Myth::OS::LockGuard guard(m_lock);

  uint16_t mainPid = 0xffff;
  int mainType = PVR_CODEC_TYPE_UNKNOWN;
  const std::vector<TSDemux::ElementaryStream*> es_streams = m_AVContext->GetStreams();
  for (std::vector<TSDemux::ElementaryStream*>::const_iterator it = es_streams.begin(); it != es_streams.end(); ++it)
  {
    const char* codec_name = (*it)->GetStreamCodecName();
    kodi::addon::PVRCodec codec = m_handler.GetCodecByName(codec_name);
    if (codec.GetCodecType() != PVR_CODEC_TYPE_UNKNOWN)
    {
      kodi::addon::PVRStreamProperties _stream;
      // Find the main stream:
      // The best candidate would be the first video. Else the first audio
      switch (mainType)
      {
      case PVR_CODEC_TYPE_VIDEO:
        break;
      case PVR_CODEC_TYPE_AUDIO:
        if (codec.GetCodecType() != PVR_CODEC_TYPE_VIDEO)
          break;
      default:
        mainPid = (*it)->pid;
        mainType = codec.GetCodecType();
      }

      _stream.SetPID((*it)->pid);
      _stream.SetCodecId(codec.GetCodecId());
      _stream.SetCodecType(codec.GetCodecType());
      char strLanguage[4];
      recode_language((*it)->stream_info.language, strLanguage);
      _stream.SetLanguage(strLanguage);
      _stream.SetSubtitleInfo(stream_identifier((*it)->stream_info.composition_id, (*it)->stream_info.ancillary_id));
      _stream.SetFPSScale((*it)->stream_info.fps_scale);
      _stream.SetFPSRate((*it)->stream_info.fps_rate);
      _stream.SetHeight((*it)->stream_info.height);
      _stream.SetWidth((*it)->stream_info.width);
      _stream.SetAspect((*it)->stream_info.aspect);
      _stream.SetChannels((*it)->stream_info.channels);
      _stream.SetSampleRate((*it)->stream_info.sample_rate);
      _stream.SetBlockAlign((*it)->stream_info.block_align);
      _stream.SetBitRate((*it)->stream_info.bit_rate);
      _stream.SetBitsPerSample((*it)->stream_info.bits_per_sample);
      m_streams.push_back(_stream);

      m_AVContext->StartStreaming((*it)->pid);

      // Add stream to no setup set
      if (!(*it)->has_stream_info)
        m_nosetup.insert((*it)->pid);

      kodi::Log(ADDON_LOG_DEBUG, LOGTAG "%s: register PES %.4x %s", __FUNCTION__, (*it)->pid, codec_name);
    }
  }
  // Renew main stream
  m_mainStreamPID = mainPid;
}

bool Demux::update_pvr_stream(TSDemux::ElementaryStream* es)
{
  const char* codec_name = es->GetStreamCodecName();
  kodi::addon::PVRCodec codec = m_handler.GetCodecByName(codec_name);
  kodi::Log(ADDON_LOG_INFO, LOGTAG "update info PES %d %s", es->pid, codec_name);

  Myth::OS::LockGuard guard(m_lock);

  // find stream index for pid
  for (std::vector<kodi::addon::PVRStreamProperties>::iterator it = m_streams.begin(); it != m_streams.end(); ++it)
  {
    if (it->GetPID() == es->pid)
    {
      it->SetCodecId(codec.GetCodecId());
      it->SetCodecType(codec.GetCodecType());
      char strLanguage[4];
      recode_language(es->stream_info.language, strLanguage);
      it->SetLanguage(strLanguage);
      it->SetSubtitleInfo(stream_identifier(es->stream_info.composition_id, es->stream_info.ancillary_id));
      it->SetFPSScale(es->stream_info.fps_scale);
      it->SetFPSRate(es->stream_info.fps_rate);
      it->SetHeight(es->stream_info.height);
      it->SetWidth(es->stream_info.width);
      it->SetAspect(es->stream_info.aspect);
      it->SetChannels(es->stream_info.channels);
      it->SetSampleRate(es->stream_info.sample_rate);
      it->SetBlockAlign(es->stream_info.block_align);
      it->SetBitRate(es->stream_info.bit_rate);
      it->SetBitsPerSample(es->stream_info.bits_per_sample);

      if (es->has_stream_info)
      {
        // Log infos
        switch(it->GetCodecType())
        {
        case PVR_CODEC_TYPE_VIDEO:
          kodi::Log(ADDON_LOG_INFO, LOGTAG "%d: %dx%d %3.3f", es->pid,
                  it->GetWidth(), it->GetHeight(), (double)it->GetFPSRate()/(double)it->GetFPSScale());
          break;
        case PVR_CODEC_TYPE_AUDIO:
          kodi::Log(ADDON_LOG_INFO, LOGTAG "%d: %d %dch %d %s", es->pid,
                  it->GetBitRate(), it->GetChannels(), it->GetSampleRate(),
                  it->GetLanguage().c_str());
          break;
        default:
          kodi::Log(ADDON_LOG_INFO, LOGTAG "%d: %s %s", es->pid, codec_name,
                  it->GetLanguage().c_str());
          break;
        }

        // Now stream has setup. Remove it from no setup set
        std::set<uint16_t>::iterator it = m_nosetup.find(es->pid);
        if (it != m_nosetup.end())
        {
          m_nosetup.erase(it);
          if (m_nosetup.empty())
            kodi::Log(ADDON_LOG_INFO, LOGTAG "setup is completed");
        }
      }
      else
      {
        kodi::Log(ADDON_LOG_WARNING, LOGTAG "%d: no stream info", es->pid);
      }
      return true;
    }
  }
  return false;
}

void Demux::push_stream_change()
{
  if (!m_isChangePlaced)
  {
    DEMUX_PACKET* dxp  = m_handler.AllocateDemuxPacket(0);
    dxp->iStreamId    = DEMUX_SPECIALID_STREAMCHANGE;

    if (push_stream_data(dxp))
    {
      m_isChangePlaced = true;
      kodi::Log(ADDON_LOG_DEBUG, LOGTAG "%s: done", __FUNCTION__);
    }
  }
}

DEMUX_PACKET* Demux::stream_pvr_data(TSDemux::STREAM_PKT* pkt)
{
  if (!pkt)
    return NULL;

  DEMUX_PACKET* dxp = m_handler.AllocateDemuxPacket(pkt->size);
  if (dxp)
  {
    if (pkt->size > 0 && pkt->data)
      memcpy(dxp->pData, pkt->data, pkt->size);

    dxp->iStreamId = (int)(pkt->pid);
    dxp->iSize = pkt->size;
    dxp->duration = (double)pkt->duration * STREAM_TIME_BASE / PTS_TIME_BASE;
    if (pkt->dts != PTS_UNSET)
      dxp->dts = (double)pkt->dts * STREAM_TIME_BASE / PTS_TIME_BASE;
    else
      dxp->dts = STREAM_NOPTS_VALUE;
    if (pkt->pts != PTS_UNSET)
      dxp->pts = (double)pkt->pts * STREAM_TIME_BASE / PTS_TIME_BASE;
    else
      dxp->pts = STREAM_NOPTS_VALUE;
  }
  return dxp;
}

bool Demux::push_stream_data(DEMUX_PACKET* dxp)
{
  if (dxp)
  {
    bool ret = false;
    unsigned c = 0;
    while (!IsStopped() && !(ret = m_demuxPacketBuffer.push(dxp)))
      usleep((++c > 10 ? 10 * FIFO_TIMEOUT_USEC : FIFO_TIMEOUT_USEC));
    if (ret)
      return true;
    m_handler.FreeDemuxPacket(dxp);
  }
  return false;
}
