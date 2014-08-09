/*
 *      Copyright (C) 2005-2013 Team XBMC
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
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301 USA
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "libXBMC_pvr.h"
#include "xbmc_codec_types.h"

#include "avinfo.h"
#include "client.h"

#define LOGTAG                  "[AVINFO] "

using namespace ADDON;

void AVInfoLog(int level, char *msg)
{
  if (msg && level != DEMUX_DBG_NONE)
  {
    bool doLog = g_bExtraDebug;
    addon_log_t loglevel = LOG_DEBUG;
    switch (level)
    {
    case DEMUX_DBG_ERROR:
      loglevel = LOG_ERROR;
      doLog = true;
      break;
    case DEMUX_DBG_WARN:
    case DEMUX_DBG_INFO:
      loglevel = LOG_INFO;
      break;
    case DEMUX_DBG_DEBUG:
    case DEMUX_DBG_PARSE:
    case DEMUX_DBG_ALL:
      loglevel = LOG_DEBUG;
      break;
    }
    if (XBMC && doLog)
      XBMC->Log(loglevel, LOGTAG"%s", msg);
  }
}

AVInfo::AVInfo(Myth::Stream* file)
: m_file(file)
, m_channel(1)
, m_av_buf_size(AV_BUFFER_SIZE)
, m_av_pos(0)
, m_av_buf(NULL)
, m_av_rbs(NULL)
, m_av_rbe(NULL)
, m_AVContext(NULL)
, m_mainStreamPID(0xffff)
, m_DTS(PTS_UNSET)
, m_PTS(PTS_UNSET)
, m_AVStatus(0)
{
  m_av_buf = (unsigned char*)malloc(sizeof(*m_av_buf) * (m_av_buf_size + 1));
  if (m_av_buf)
  {
    m_av_rbs = m_av_buf;
    m_av_rbe = m_av_buf;

    if (g_bExtraDebug)
      demux_dbg_level(DEMUX_DBG_DEBUG);
    else
      demux_dbg_level(DEMUX_DBG_ERROR);
    demux_set_dbg_msgcallback(AVInfoLog);

    m_AVContext = new AVContext(this, m_av_pos, m_channel);

    Process();
  }
  else
  {
    XBMC->Log(LOG_ERROR, LOGTAG"alloc AV buffer failed");
  }
}

AVInfo::~AVInfo()
{
  // Free AV context
  if (m_AVContext)
    SAFE_DELETE(m_AVContext);
  // Free AV buffer
  if (m_av_buf)
  {
    if (g_bExtraDebug)
      XBMC->Log(LOG_DEBUG, LOGTAG"free AV buffer: allocated size was %zu", m_av_buf_size);
    free(m_av_buf);
    m_av_buf = NULL;
  }
}

/*
 * Implement our AV reader
 */
const unsigned char* AVInfo::ReadAV(uint64_t pos, size_t n)
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
  int wait = 5000;
  while (wait > 0)
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
    wait -= 1000;
    usleep(100000);
  }
  return dataread >= n ? m_av_rbs : NULL;
}

void AVInfo::Process()
{
  if (!m_AVContext)
  {
    XBMC->Log(LOG_ERROR, LOGTAG"%s: no AVContext", __FUNCTION__);
    return;
  }

  int ret = 0;
  bool analyzed = false;

  while (!analyzed)
  {
    {
      ret = m_AVContext->TSResync();
    }
    if (ret != AVCONTEXT_CONTINUE)
      break;

    ret = m_AVContext->ProcessTSPacket();

    if (m_AVContext->HasPIDStreamData())
    {
      ElementaryStream::STREAM_PKT pkt;
      while (get_stream_data(&pkt))
      {
        if (pkt.streamChange)
        {
          // Update stream properties. Analyzing will be closed once setup is completed for all streams.
          if (update_pvr_stream(pkt.pid) && m_nosetup.empty())
              analyzed = true;
        }
      }
    }
    if (m_AVContext->HasPIDPayload())
    {
      ret = m_AVContext->ProcessTSPayload();
      if (ret == AVCONTEXT_PROGRAM_CHANGE)
      {
        populate_pvr_streams();
      }
    }

    if (ret < 0)
      XBMC->Log(LOG_NOTICE, LOGTAG"%s: error %d", __FUNCTION__, ret);

    if (ret == AVCONTEXT_TS_ERROR)
      m_AVContext->Shift();
    else
      m_AVContext->GoNext();
  }

  m_AVStatus = ret;
  m_file->Seek(0, Myth::WHENCE_SET);
  XBMC->Log(LOG_DEBUG, LOGTAG"%s: terminated with status %d", __FUNCTION__, ret);
}

bool AVInfo::GetMainStream(ADDON::XbmcPvrStream* stream) const
{
  if (m_AVStatus < 0 || !m_nosetup.empty())
    return false;
  ADDON::XbmcPvrStream* found = m_streams.GetStreamById(m_mainStreamPID);
  if (found == NULL)
    return false;
  *stream = *found;
  return true;
}

bool AVInfo::GetStreamProperties(PVR_STREAM_PROPERTIES* props)
{
  if (m_AVStatus < 0 || !m_nosetup.empty())
    return false;
  return m_streams.GetProperties(props);
}

bool AVInfo::get_stream_data(ElementaryStream::STREAM_PKT* pkt)
{
  ElementaryStream* es = m_AVContext->GetPIDStream();
  if (!es)
    return false;

  if (!es->GetStreamPacket(pkt))
    return false;

  if (pkt->duration > 180000)
  {
    pkt->duration = 0;
  }
  else if (pkt->pid == m_mainStreamPID)
  {
    // Sync main DTS & PTS
    m_DTS = pkt->dts;
    m_PTS = pkt->pts;
  }
  return true;
}

static inline int stream_identifier(int composition_id, int ancillary_id)
{
  return ((composition_id & 0xff00) >> 8)
    | ((composition_id & 0xff) << 8)
    | ((ancillary_id & 0xff00) << 16)
    | ((ancillary_id & 0xff) << 24);
}

static void recode_language(const char* muxLanguage, char* strLanguage)
{
  strLanguage[0] = muxLanguage[0];
  strLanguage[1] = muxLanguage[1];
  strLanguage[2] = muxLanguage[2];
  strLanguage[3] = 0;
}

void AVInfo::populate_pvr_streams()
{
  uint16_t mainPid = 0xffff;
  int mainType = XBMC_CODEC_TYPE_UNKNOWN;
  std::vector<XbmcPvrStream> new_streams;
  const std::vector<ElementaryStream*> es_streams = m_AVContext->GetStreams();
  for (std::vector<ElementaryStream*>::const_iterator it = es_streams.begin(); it != es_streams.end(); it++)
  {
    const char* codec_name = (*it)->GetStreamCodecName();
    xbmc_codec_t codec = CODEC->GetCodecByName(codec_name);
    if (codec.codec_type != XBMC_CODEC_TYPE_UNKNOWN)
    {
      // Find the main stream:
      // The best candidate would be the first video. Else the first audio
      switch (mainType)
      {
      case XBMC_CODEC_TYPE_VIDEO:
        break;
      case XBMC_CODEC_TYPE_AUDIO:
        if (codec.codec_type != XBMC_CODEC_TYPE_VIDEO)
          break;
      default:
        mainPid = (*it)->pid;
        mainType = codec.codec_type;
      }

      XbmcPvrStream new_stream;
      m_streams.GetStreamData((*it)->pid, &new_stream);

      new_stream.iCodecId       = codec.codec_id;
      new_stream.iCodecType     = codec.codec_type;
      recode_language((*it)->stream_info.language, new_stream.strLanguage);
      new_stream.iIdentifier    = stream_identifier((*it)->stream_info.composition_id, (*it)->stream_info.ancillary_id);
      new_stream.iFPSScale      = (*it)->stream_info.fps_scale;
      new_stream.iFPSRate       = (*it)->stream_info.fps_rate;
      new_stream.iHeight        = (*it)->stream_info.height;
      new_stream.iWidth         = (*it)->stream_info.width;
      new_stream.fAspect        = (*it)->stream_info.aspect;
      new_stream.iChannels      = (*it)->stream_info.channels;
      new_stream.iSampleRate    = (*it)->stream_info.sample_rate;
      new_stream.iBlockAlign    = (*it)->stream_info.block_align;
      new_stream.iBitRate       = (*it)->stream_info.bit_rate;
      new_stream.iBitsPerSample = (*it)->stream_info.bits_Per_sample;

      new_streams.push_back(new_stream);
      m_AVContext->StartStreaming((*it)->pid);

      // Add stream to no setup set
      if (!(*it)->has_stream_info)
        m_nosetup.insert((*it)->pid);

      if (g_bExtraDebug)
        XBMC->Log(LOG_DEBUG, LOGTAG"%s: register PES %.4x %s", __FUNCTION__, (*it)->pid, codec_name);
    }
  }
  m_streams.UpdateStreams(new_streams);
  // Renew main stream
  m_mainStreamPID = mainPid;
}

bool AVInfo::update_pvr_stream(uint16_t pid)
{
  ElementaryStream* es = m_AVContext->GetStream(pid);
  if (!es)
    return false;

  if (g_bExtraDebug)
    XBMC->Log(LOG_DEBUG, LOGTAG"%s: update info PES %.4x %s", __FUNCTION__, es->pid, es->GetStreamCodecName());

  XbmcPvrStream* stream = m_streams.GetStreamById(es->pid);
  if (stream)
  {
    recode_language(es->stream_info.language, stream->strLanguage);
    stream->iIdentifier    = stream_identifier(es->stream_info.composition_id, es->stream_info.ancillary_id);
    stream->iFPSScale      = es->stream_info.fps_scale;
    stream->iFPSRate       = es->stream_info.fps_rate;
    stream->iHeight        = es->stream_info.height;
    stream->iWidth         = es->stream_info.width;
    stream->fAspect        = es->stream_info.aspect;
    stream->iChannels      = es->stream_info.channels;
    stream->iSampleRate    = es->stream_info.sample_rate;
    stream->iBlockAlign    = es->stream_info.block_align;
    stream->iBitRate       = es->stream_info.bit_rate;
    stream->iBitsPerSample = es->stream_info.bits_Per_sample;

    if (es->has_stream_info)
    {
      // Now stream is setup. Remove it from no setup set
      std::set<uint16_t>::iterator it = m_nosetup.find(es->pid);
      if (it != m_nosetup.end())
      {
        m_nosetup.erase(it);
        if (m_nosetup.empty())
          XBMC->Log(LOG_DEBUG, LOGTAG"%s: setup is completed", __FUNCTION__);
      }
    }
    return true;
  }
  return false;
}