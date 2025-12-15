/*
 *      Copyright (C) 2014 Jean-Luc Barriere
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
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301 USA
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "mythwsapi.h"
#include "private/debug.h"
#include "private/socket.h"
#include "private/wsrequest.h"
#include "private/wsresponse.h"
#include "private/jsonparser.h"
#include "private/mythjsonbinder.h"
#include "private/os/threads/mutex.h"
#include "private/cppdef.h"
#include "private/builtin.h"
#include "private/uriparser.h"
#include "private/urlencoder.h"

#define BOOLSTR(a)  ((a) ? "true" : "false")
#define FETCHSIZE   100
#define FETCHSIZE_L 1000

using namespace Myth;

#define WS_ROOT_MYTH          "/Myth"
#define WS_ROOT_CAPTURE       "/Capture"
#define WS_ROOT_CHANNEL       "/Channel"
#define WS_ROOT_GUIDE         "/Guide"
#define WS_ROOT_CONTENT       "/Content"
#define WS_ROOT_DVR           "/Dvr"

std::string WSAPI::encode_param(const std::string& str)
{
  return urlencode(str);
}

WSAPI::WSAPI(const std::string& server, unsigned port, const std::string& securityPin)
: m_mutex(new OS::Mutex)
, m_server(server)
, m_port(port)
, m_securityPin(securityPin)
, m_checked(false)
, m_version()
, m_serverHostName()
{
  m_checked = InitWSAPI();
}

WSAPI::~WSAPI()
{
  SAFE_DELETE(m_mutex);
}

bool WSAPI::InitWSAPI()
{
  bool status = false;
  // Reset array of version
  memset(m_serviceVersion, 0, sizeof(m_serviceVersion));
  // Check the core service Myth
  WSServiceVersion_t& mythwsv = m_serviceVersion[WS_Myth];
  if (!GetServiceVersion(WS_Myth, mythwsv))
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return false;
  }
  if (mythwsv.ranking > MYTH_API_VERSION_MAX_RANKING) {}
  else if (mythwsv.ranking >= 0x00020000)
    status = CheckServerHostName2_0() && CheckVersion2_0();

  // If everything is fine then check other services
  if (status)
  {
    if (GetServiceVersion(WS_Capture, m_serviceVersion[WS_Capture]) &&
        GetServiceVersion(WS_Channel, m_serviceVersion[WS_Channel]) &&
        GetServiceVersion(WS_Guide, m_serviceVersion[WS_Guide]) &&
        GetServiceVersion(WS_Content, m_serviceVersion[WS_Content]) &&
        GetServiceVersion(WS_Dvr, m_serviceVersion[WS_Dvr]))
    {
      DBG(DBG_INFO, "%s: MythTV API service is available: %s:%d(%s) protocol(%d) schema(%d)\n",
              __FUNCTION__, m_serverHostName.c_str(), m_port, m_version.version.c_str(),
              (unsigned)m_version.protocol, (unsigned)m_version.schema);
      return true;
    }
  }
  DBG(DBG_ERROR, "%s: MythTV API service is not supported or unavailable: %s:%d (%u.%u)\n",
          __FUNCTION__, m_server.c_str(), m_port, mythwsv.major, mythwsv.minor);
  return false;
}

bool WSAPI::GetServiceVersion(WSServiceId_t id, WSServiceVersion_t& wsv)
{
  static const char * WSServiceRoot[WS_INVALID + 1] =
  {
    WS_ROOT_MYTH,         ///< WS_Myth
    WS_ROOT_CAPTURE,      ///< WS_Capture
    WS_ROOT_CHANNEL,      ///< WS_Channel
    WS_ROOT_GUIDE,        ///< WS_Guide
    WS_ROOT_CONTENT,      ///< WS_Content
    WS_ROOT_DVR,          ///< WS_Dvr
    "/?",                 ///< WS_INVALID
  };
  std::string url(WSServiceRoot[id]);
  url.append("/version");
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService(url);
  WSResponse resp(req);
  if (resp.IsSuccessful())
  {
    // Parse content response
    const JSON::Document json(resp);
    const JSON::Node& root = json.GetRoot();
    if (json.IsValid() && root.IsObject())
    {
      const JSON::Node& field = root.GetObjectValue("String");
      if (field.IsString())
      {
        const std::string& val = field.GetStringValue();
        if (sscanf(val.c_str(), "%d.%d", &(wsv.major), &(wsv.minor)) == 2)
        {
          wsv.ranking = ((wsv.major & 0xFFFF) << 16) | (wsv.minor & 0xFFFF);
          return true;
        }
      }
    }
  }
  wsv.major = 0;
  wsv.minor = 0;
  wsv.ranking = 0;
  return false;
}

bool WSAPI::CheckServerHostName2_0()
{
  m_serverHostName.clear();

  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Myth/GetHostName");
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return false;
  }
  // Parse content response
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (json.IsValid() && root.IsObject())
  {
    const JSON::Node& field = root.GetObjectValue("String");
    if (field.IsString())
    {
      const std::string& val = field.GetStringValue();
      m_serverHostName = val;
      m_namedCache[val] = m_server;
      return true;
    }
  }
  return false;
}

bool WSAPI::CheckVersion2_0()
{
  m_version.protocol = 0;
  m_version.schema = 0;
  m_version.version.clear();
  WSServiceVersion_t& wsv = m_serviceVersion[WS_Myth];

  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Myth/GetConnectionInfo");
  if (!m_securityPin.empty())
  {
    // Skip if null or empty
    req.SetContentParam("Pin", m_securityPin);
  }
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return false;
  }
  // Parse content response
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (json.IsValid() && root.IsObject())
  {
    const JSON::Node& con = root.GetObjectValue("ConnectionInfo");
    if (con.IsObject())
    {
      const JSON::Node& ver = con.GetObjectValue("Version");
      JSON::BindObject(ver, &m_version, MythDTO::getVersionBindArray(wsv.ranking));
      if (m_version.protocol)
        return true;
    }
  }
  return false;
}

unsigned WSAPI::CheckService()
{
  OS::LockGuard lock(*m_mutex);
  if (m_checked || (m_checked = InitWSAPI()))
    return (unsigned)m_version.protocol;
  return 0;
}

WSServiceVersion_t WSAPI::CheckService(WSServiceId_t id)
{
  OS::LockGuard lock(*m_mutex);
  if (m_checked || (m_checked = InitWSAPI()))
    return m_serviceVersion[id];
  return m_serviceVersion[WS_INVALID];
}

void WSAPI::InvalidateService()
{
  if (m_checked)
    m_checked = false;
}

std::string WSAPI::GetServerHostName()
{
  return m_serverHostName;
}

VersionPtr WSAPI::GetVersion()
{
  return VersionPtr(new Version(m_version));
}

std::string WSAPI::ResolveHostName(const std::string& hostname)
{
  OS::LockGuard lock(*m_mutex);
  std::map<std::string, std::string>::const_iterator it = m_namedCache.find(hostname);
  if (it != m_namedCache.end())
    return it->second;
  Myth::SettingPtr addr = this->GetSetting("BackendServerIP6", hostname);
  if (addr && !addr->value.empty() && addr->value != "::1")
  {
    std::string& ret = m_namedCache[hostname];
    ret.assign(addr->value);
    DBG(DBG_DEBUG, "%s: resolving hostname %s as %s\n", __FUNCTION__, hostname.c_str(), ret.c_str());
    return ret;
  }
  addr = this->GetSetting("BackendServerIP", hostname);
  if (addr && !addr->value.empty())
  {
    std::string& ret = m_namedCache[hostname];
    ret.assign(addr->value);
    DBG(DBG_DEBUG, "%s: resolving hostname %s as %s\n", __FUNCTION__, hostname.c_str(), ret.c_str());
    return ret;
  }
  DBG(DBG_ERROR, "%s: unknown host (%s)\n", __FUNCTION__, hostname.c_str());
  return std::string();
}

///////////////////////////////////////////////////////////////////////////////
////
////  Service operations
////

SettingPtr WSAPI::GetSetting2_0(const std::string& key, const std::string& hostname)
{
  SettingPtr ret;

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Myth/GetSetting");
  req.SetContentParam("HostName", hostname);
  req.SetContentParam("Key", key);
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return ret;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return ret;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  // Object: SettingList
  const JSON::Node& slist = root.GetObjectValue("SettingList");
  // Object: Settings
  const JSON::Node& sts = slist.GetObjectValue("Settings");
  if (sts.IsObject())
  {
    if (sts.Size())
    {
      const JSON::Node& val = sts.GetObjectValue(static_cast<size_t>(0));
      if (val.IsString())
      {
        ret.reset(new Setting());  // Using default constructor
        ret->key = sts.GetObjectKey(0);
        ret->value = val.GetStringValue();
      }
    }
  }
  return ret;
}

SettingPtr WSAPI::GetSetting5_0(const std::string& key, const std::string& hostname)
{
  SettingPtr ret;

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Myth/GetSetting");
  req.SetContentParam("HostName", hostname);
  req.SetContentParam("Key", key);
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return ret;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return ret;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  // Object: String
  const JSON::Node& val = root.GetObjectValue("String");
  if (val.IsString())
  {
    ret.reset(new Setting());  // Using default constructor
    ret->key = key;
    ret->value = val.GetStringValue();
  }
  return ret;
}

SettingPtr WSAPI::GetSetting(const std::string& key, bool myhost)
{
  std::string hostname;
  if (myhost)
    hostname = TcpSocket::GetMyHostName();
  return GetSetting(key, hostname);
}

SettingMapPtr WSAPI::GetSettings2_0(const std::string& hostname)
{
  SettingMapPtr ret(new SettingMap);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Myth/GetSetting");
  req.SetContentParam("HostName", hostname);
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return ret;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return ret;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  // Object: SettingList
  const JSON::Node& slist = root.GetObjectValue("SettingList");
  // Object: Settings
  const JSON::Node& sts = slist.GetObjectValue("Settings");
  if (sts.IsObject())
  {
    size_t s = sts.Size();
    for (size_t i = 0; i < s; ++i)
    {
      const JSON::Node& val = sts.GetObjectValue(i);
      if (val.IsString())
      {
        SettingPtr setting(new Setting());  // Using default constructor
        setting->key = sts.GetObjectKey(i);
        setting->value = val.GetStringValue();
        ret->insert(SettingMap::value_type(setting->key, setting));
      }
    }
  }
  return ret;
}

SettingMapPtr WSAPI::GetSettings5_0(const std::string& hostname)
{
  SettingMapPtr ret(new SettingMap);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Myth/GetSettingList");
  req.SetContentParam("HostName", hostname);
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return ret;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return ret;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  // Object: SettingList
  const JSON::Node& slist = root.GetObjectValue("SettingList");
  // Object: Settings
  const JSON::Node& sts = slist.GetObjectValue("Settings");
  if (sts.IsObject())
  {
    size_t s = sts.Size();
    for (size_t i = 0; i < s; ++i)
    {
      const JSON::Node& val = sts.GetObjectValue(i);
      if (val.IsString())
      {
        SettingPtr setting(new Setting());  // Using default constructor
        setting->key = sts.GetObjectKey(i);
        setting->value = val.GetStringValue();
        ret->insert(SettingMap::value_type(setting->key, setting));
      }
    }
  }
  return ret;
}

SettingMapPtr WSAPI::GetSettings(bool myhost)
{
  std::string hostname;
  if (myhost)
    hostname = TcpSocket::GetMyHostName();
  return GetSettings(hostname);
}

bool WSAPI::PutSetting2_0(const std::string& key, const std::string& value, bool myhost)
{
  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Myth/PutSetting", HRM_POST);
  std::string hostname;
  if (myhost)
    hostname = TcpSocket::GetMyHostName();
  req.SetContentParam("HostName", hostname);
  req.SetContentParam("Key", key);
  req.SetContentParam("Value", value);
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return false;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return false;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  const JSON::Node& field = root.GetObjectValue("bool");
  if (field.IsTrue() || (field.IsString() && strcmp(field.GetStringValue().c_str(), "true") == 0))
    return true;
  return false;
}

///////////////////////////////////////////////////////////////////////////////
////
//// Capture service
////
CaptureCardListPtr WSAPI::GetCaptureCardList1_4()
{
  CaptureCardListPtr ret(new CaptureCardList);
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindcard = MythDTO::getCaptureCardBindArray(proto);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Capture/GetCaptureCardList");
  req.SetContentParam("HostName", m_serverHostName.c_str());
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return ret;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return ret;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  // Object: CaptureCardList
  const JSON::Node& clist = root.GetObjectValue("CaptureCardList");
  // Object: CaptureCards[]
  const JSON::Node& cards = clist.GetObjectValue("CaptureCards");
  // Iterates over the sequence elements.
  size_t cs = cards.Size();
  for (size_t ci = 0; ci < cs; ++ci)
  {
    const JSON::Node& card = cards.GetArrayElement(ci);
    CaptureCardPtr captureCard(new CaptureCard());  // Using default constructor
    // Bind the new captureCard
    JSON::BindObject(card, captureCard.get(), bindcard);
    ret->push_back(captureCard);
  }
  return ret;
}

///////////////////////////////////////////////////////////////////////////////
////
//// Channel Service
////
VideoSourceListPtr WSAPI::GetVideoSourceList1_2()
{
  VideoSourceListPtr ret(new VideoSourceList);
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindvsrc = MythDTO::getVideoSourceBindArray(proto);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Channel/GetVideoSourceList");
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return ret;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return ret;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  // Object: VideoSourceList
  const JSON::Node& slist = root.GetObjectValue("VideoSourceList");
  // Object: VideoSources[]
  const JSON::Node& vsrcs = slist.GetObjectValue("VideoSources");
  // Iterates over the sequence elements.
  size_t vs = vsrcs.Size();
  for (size_t vi = 0; vi < vs; ++vi)
  {
    const JSON::Node& vsrc = vsrcs.GetArrayElement(vi);
    VideoSourcePtr videoSource(new VideoSource());  // Using default constructor
    // Bind the new videoSource
    JSON::BindObject(vsrc, videoSource.get(), bindvsrc);
    ret->push_back(videoSource);
  }
  return ret;
}

ChannelListPtr WSAPI::GetChannelList1_2(uint32_t sourceid, bool onlyVisible)
{
  ChannelListPtr ret(new ChannelList);
  BUILTIN_BUFFER buf;
  int32_t req_index = 0, req_count = FETCHSIZE, count = 0;
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindlist = MythDTO::getListBindArray(proto);
  const bindings_t *bindchan = MythDTO::getChannelBindArray(proto);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Channel/GetChannelInfoList");

  do
  {
    req.ClearContent();
    uint32_to_string(sourceid, &buf);
    req.SetContentParam("SourceID", buf.data);
    int32_to_string(req_index, &buf);
    req.SetContentParam("StartIndex", buf.data);
    int32_to_string(req_count, &buf);
    req.SetContentParam("Count", buf.data);

    DBG(DBG_DEBUG, "%s: request index(%d) count(%d)\n", __FUNCTION__, req_index, req_count);
    WSResponse resp(req);
    if (!resp.IsSuccessful())
    {
      DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
      break;
    }
    const JSON::Document json(resp);
    const JSON::Node& root = json.GetRoot();
    if (!json.IsValid() || !root.IsObject())
    {
      DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
      break;
    }
    DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

    // Object: ChannelInfoList
    const JSON::Node& clist = root.GetObjectValue("ChannelInfoList");
    ItemList list = ItemList(); // Using default constructor
    JSON::BindObject(clist, &list, bindlist);
    // List has ProtoVer. Check it or sound alarm
    if (list.protoVer != proto)
    {
      InvalidateService();
      break;
    }
    count = 0;
    // Object: ChannelInfos[]
    const JSON::Node& chans = clist.GetObjectValue("ChannelInfos");
    // Iterates over the sequence elements.
    size_t cs = chans.Size();
    for (size_t ci = 0; ci < cs; ++ci)
    {
      ++count;
      const JSON::Node& chan = chans.GetArrayElement(ci);
      ChannelPtr channel(new Channel());  // Using default constructor
      // Bind the new channel
      JSON::BindObject(chan, channel.get(), bindchan);
      if (channel->chanId && (!onlyVisible || channel->visible))
        ret->push_back(channel);
    }
    DBG(DBG_DEBUG, "%s: received count(%d)\n", __FUNCTION__, count);
    req_index += count; // Set next requested index
  }
  while (count == req_count);

  return ret;
}

ChannelListPtr WSAPI::GetChannelList1_5(uint32_t sourceid, bool onlyVisible)
{
  ChannelListPtr ret(new ChannelList);
  BUILTIN_BUFFER buf;
  int32_t /*req_index = 0, req_count = FETCHSIZE,*/ count = 0;
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindlist = MythDTO::getListBindArray(proto);
  const bindings_t *bindchan = MythDTO::getChannelBindArray(proto);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Channel/GetChannelInfoList");

  do
  {
    req.ClearContent();
    req.SetContentParam("Details", "true");
    req.SetContentParam("OnlyVisible", BOOLSTR(onlyVisible));
    uint32_to_string(sourceid, &buf);
    req.SetContentParam("SourceID", buf.data);
    // W.A. for bug tracked by ticket 12461
    //int32_to_string(req_index, &buf);
    //req.SetContentParam("StartIndex", buf.data);
    //int32_to_string(req_count, &buf);
    //req.SetContentParam("Count", buf.data);

    //DBG(DBG_DEBUG, "%s: request index(%d) count(%d)\n", __FUNCTION__, req_index, req_count);
    WSResponse resp(req);
    if (!resp.IsSuccessful())
    {
      DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
      break;
    }
    const JSON::Document json(resp);
    const JSON::Node& root = json.GetRoot();
    if (!json.IsValid() || !root.IsObject())
    {
      DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
      break;
    }
    DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

    // Object: ChannelInfoList
    const JSON::Node& clist = root.GetObjectValue("ChannelInfoList");
    ItemList list = ItemList(); // Using default constructor
    JSON::BindObject(clist, &list, bindlist);
    // List has ProtoVer. Check it or sound alarm
    if (list.protoVer != proto)
    {
      InvalidateService();
      break;
    }
    count = 0;
    // Object: ChannelInfos[]
    const JSON::Node& chans = clist.GetObjectValue("ChannelInfos");
    // Iterates over the sequence elements.
    size_t cs = chans.Size();
    for (size_t ci = 0; ci < cs; ++ci)
    {
      ++count;
      const JSON::Node& chan = chans.GetArrayElement(ci);
      ChannelPtr channel(new Channel());  // Using default constructor
      // Bind the new channel
      JSON::BindObject(chan, channel.get(), bindchan);
      if (channel->chanId)
        ret->push_back(channel);
    }
    DBG(DBG_DEBUG, "%s: received count(%d)\n", __FUNCTION__, count);
    //req_index += count; // Set next requested index
  }
  //while (count == req_count);
  while (false); // W.A. for bug tracked by ticket 12461

  return ret;
}

ChannelPtr WSAPI::GetChannel1_2(uint32_t chanid)
{
  ChannelPtr ret;
  BUILTIN_BUFFER buf;
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindchan = MythDTO::getChannelBindArray(proto);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Channel/GetChannelInfo");
  uint32_to_string(chanid, &buf);
  req.SetContentParam("ChanID", buf.data);

  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return ret;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return ret;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  // Object: ChannelInfo
  const JSON::Node& chan = root.GetObjectValue("ChannelInfo");
  ChannelPtr channel(new Channel());  // Using default constructor
  // Bind the new channel
  JSON::BindObject(chan, channel.get(), bindchan);
  if (channel->chanId == chanid)
    ret = channel;
  return ret;
}

///////////////////////////////////////////////////////////////////////////////
////
//// Guide service
////
std::map<uint32_t, ProgramMapPtr> WSAPI::GetProgramGuide1_0(time_t starttime, time_t endtime)
{
  std::map<uint32_t, ProgramMapPtr> ret;
  BUILTIN_BUFFER buf;
  int32_t count = 0;
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindlist = MythDTO::getListBindArray(proto);
  const bindings_t *bindchan = MythDTO::getChannelBindArray(proto);
  const bindings_t *bindprog = MythDTO::getProgramBindArray(proto);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Guide/GetProgramGuide");
  req.SetContentParam("StartChanId", "0");
  req.SetContentParam("NumChannels", "0");
  time_to_iso8601utc(starttime, &buf);
  req.SetContentParam("StartTime", buf.data);
  time_to_iso8601utc(endtime, &buf);
  req.SetContentParam("EndTime", buf.data);
  req.SetContentParam("Details", "true");

  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return ret;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return ret;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  // Object: ProgramGuide
  const JSON::Node& glist = root.GetObjectValue("ProgramGuide");
  ItemList list = ItemList(); // Using default constructor
  JSON::BindObject(glist, &list, bindlist);
  // List has ProtoVer. Check it or sound alarm
  if (list.protoVer != proto)
  {
    InvalidateService();
    return ret;
  }
  // Object: Channels[]
  const JSON::Node& chans = glist.GetObjectValue("Channels");
  // Iterates over the sequence elements.
  size_t cs = chans.Size();
  for (size_t ci = 0; ci < cs; ++ci)
  {
    const JSON::Node& chan = chans.GetArrayElement(ci);
    Channel channel;
    JSON::BindObject(chan, &channel, bindchan);
    ProgramMapPtr pmap(new ProgramMap);
    ret.insert(std::make_pair(channel.chanId, pmap));
    // Object: Programs[]
    const JSON::Node& progs = chan.GetObjectValue("Programs");
    // Iterates over the sequence elements.
    size_t ps = progs.Size();
    for (size_t pi = 0; pi < ps; ++pi)
    {
      ++count;
      const JSON::Node& prog = progs.GetArrayElement(pi);
      ProgramPtr program(new Program());  // Using default constructor
      // Bind the new program
      JSON::BindObject(prog, program.get(), bindprog);
      program->channel = channel;
      pmap->insert(std::make_pair(program->startTime, program));
    }
  }
  DBG(DBG_DEBUG, "%s: received count(%d)\n", __FUNCTION__, count);

  return ret;
}

ProgramMapPtr WSAPI::GetProgramGuide1_0(uint32_t chanid, time_t starttime, time_t endtime)
{
  ProgramMapPtr ret(new ProgramMap);
  BUILTIN_BUFFER buf;
  int32_t count = 0;
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindlist = MythDTO::getListBindArray(proto);
  const bindings_t *bindchan = MythDTO::getChannelBindArray(proto);
  const bindings_t *bindprog = MythDTO::getProgramBindArray(proto);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Guide/GetProgramGuide");
  uint32_to_string(chanid, &buf);
  req.SetContentParam("StartChanId", buf.data);
  req.SetContentParam("NumChannels", "1");
  time_to_iso8601utc(starttime, &buf);
  req.SetContentParam("StartTime", buf.data);
  time_to_iso8601utc(endtime, &buf);
  req.SetContentParam("EndTime", buf.data);
  req.SetContentParam("Details", "true");

  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return ret;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return ret;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  // Object: ProgramGuide
  const JSON::Node& glist = root.GetObjectValue("ProgramGuide");
  ItemList list = ItemList(); // Using default constructor
  JSON::BindObject(glist, &list, bindlist);
  // List has ProtoVer. Check it or sound alarm
  if (list.protoVer != proto)
  {
    InvalidateService();
    return ret;
  }
  // Object: Channels[]
  const JSON::Node& chans = glist.GetObjectValue("Channels");
  // Iterates over the sequence elements.
  size_t cs = chans.Size();
  for (size_t ci = 0; ci < cs; ++ci)
  {
    const JSON::Node& chan = chans.GetArrayElement(ci);
    Channel channel;
    JSON::BindObject(chan, &channel, bindchan);
    if (channel.chanId != chanid)
      continue;
    // Object: Programs[]
    const JSON::Node& progs = chan.GetObjectValue("Programs");
    // Iterates over the sequence elements.
    size_t ps = progs.Size();
    for (size_t pi = 0; pi < ps; ++pi)
    {
      ++count;
      const JSON::Node& prog = progs.GetArrayElement(pi);
      ProgramPtr program(new Program());  // Using default constructor
      // Bind the new program
      JSON::BindObject(prog, program.get(), bindprog);
      program->channel = channel;
      ret->insert(std::make_pair(program->startTime, program));
    }
    break;
  }
  DBG(DBG_DEBUG, "%s: received count(%d)\n", __FUNCTION__, count);

  return ret;
}

std::map<uint32_t, ProgramMapPtr> WSAPI::GetProgramGuide2_2(time_t starttime, time_t endtime)
{
  std::map<uint32_t, ProgramMapPtr> ret;
  BUILTIN_BUFFER buf;
  uint32_t req_index = 0, req_count = FETCHSIZE, count = 0;
  unsigned proto = (unsigned)m_version.protocol;

  // Adjust the fetch count according to the number of requested days
  double d = difftime(endtime, starttime);
  if (d > 0)
    req_count = FETCHSIZE / (int)(1.0 + d / (3 * 86400));

  // Get bindings for protocol version
  const bindings_t *bindlist = MythDTO::getListBindArray(proto);
  const bindings_t *bindprog = MythDTO::getProgramBindArray(proto);
  const bindings_t *bindchan = MythDTO::getChannelBindArray(proto);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Guide/GetProgramGuide");

  do
  {
    req.ClearContent();
    uint32_to_string(req_index, &buf);
    req.SetContentParam("StartIndex", buf.data);
    uint32_to_string(req_count, &buf);
    req.SetContentParam("Count", buf.data);
    time_to_iso8601utc(starttime, &buf);
    req.SetContentParam("StartTime", buf.data);
    time_to_iso8601utc(endtime, &buf);
    req.SetContentParam("EndTime", buf.data);
    req.SetContentParam("Details", "true");

    DBG(DBG_DEBUG, "%s: request index(%d) count(%d)\n", __FUNCTION__, req_index, req_count);
    WSResponse resp(req);
    if (!resp.IsSuccessful())
    {
      DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
      break;
    }
    const JSON::Document json(resp);
    const JSON::Node& root = json.GetRoot();
    if (!json.IsValid() || !root.IsObject())
    {
      DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
      break;
    }
    DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

    // Object: ProgramGuide
    const JSON::Node& glist = root.GetObjectValue("ProgramGuide");
    ItemList list = ItemList(); // Using default constructor
    JSON::BindObject(glist, &list, bindlist);
    // List has ProtoVer. Check it or sound alarm
    if (list.protoVer != proto)
    {
      InvalidateService();
      break;
    }
    count = 0;
    // Object: Channels[]
    const JSON::Node& chans = glist.GetObjectValue("Channels");
    // Iterates over the sequence elements.
    size_t cs = chans.Size();
    for (size_t ci = 0; ci < cs; ++ci)
    {
      ++count;
      const JSON::Node& chan = chans.GetArrayElement(ci);
      Channel channel;
      JSON::BindObject(chan, &channel, bindchan);
      ProgramMapPtr pmap(new ProgramMap);
      ret.insert(std::make_pair(channel.chanId, pmap));
      // Object: Programs[]
      const JSON::Node& progs = chan.GetObjectValue("Programs");
      // Iterates over the sequence elements.
      size_t ps = progs.Size();
      for (size_t pi = 0; pi < ps; ++pi)
      {
        const JSON::Node& prog = progs.GetArrayElement(pi);
        ProgramPtr program(new Program());  // Using default constructor
        // Bind the new program
        JSON::BindObject(prog, program.get(), bindprog);
        program->channel = channel;
        pmap->insert(std::make_pair(program->startTime, program));
      }
    }
    DBG(DBG_DEBUG, "%s: received count(%d)\n", __FUNCTION__, count);
    req_index += count; // Set next requested index
  }
  while (count == req_count);

  return ret;
}

ProgramMapPtr WSAPI::GetProgramList2_2(uint32_t chanid, time_t starttime, time_t endtime)
{
  ProgramMapPtr ret(new ProgramMap);
  BUILTIN_BUFFER buf;
  uint32_t req_index = 0, req_count = FETCHSIZE_L, count = 0;
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindlist = MythDTO::getListBindArray(proto);
  const bindings_t *bindprog = MythDTO::getProgramBindArray(proto);
  const bindings_t *bindchan = MythDTO::getChannelBindArray(proto);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Guide/GetProgramList");

  do
  {
    req.ClearContent();
    uint32_to_string(req_index, &buf);
    req.SetContentParam("StartIndex", buf.data);
    uint32_to_string(req_count, &buf);
    req.SetContentParam("Count", buf.data);
    uint32_to_string(chanid, &buf);
    req.SetContentParam("ChanId", buf.data);
    time_to_iso8601utc(starttime, &buf);
    req.SetContentParam("StartTime", buf.data);
    time_to_iso8601utc(endtime, &buf);
    req.SetContentParam("EndTime", buf.data);
    req.SetContentParam("Details", "true");

    DBG(DBG_DEBUG, "%s: request index(%d) count(%d)\n", __FUNCTION__, req_index, req_count);
    WSResponse resp(req);
    if (!resp.IsSuccessful())
    {
      DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
      break;
    }
    const JSON::Document json(resp);
    const JSON::Node& root = json.GetRoot();
    if (!json.IsValid() || !root.IsObject())
    {
      DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
      break;
    }
    DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

    // Object: ProgramList
    const JSON::Node& plist = root.GetObjectValue("ProgramList");
    ItemList list = ItemList(); // Using default constructor
    JSON::BindObject(plist, &list, bindlist);
    // List has ProtoVer. Check it or sound alarm
    if (list.protoVer != proto)
    {
      InvalidateService();
      break;
    }
    count = 0;
    // Object: Programs[]
    const JSON::Node& progs = plist.GetObjectValue("Programs");
    // Iterates over the sequence elements.
    size_t ps = progs.Size();
    for (size_t pi = 0; pi < ps; ++pi)
    {
      ++count;
      const JSON::Node& prog = progs.GetArrayElement(pi);
      ProgramPtr program(new Program());  // Using default constructor
      // Bind the new program
      JSON::BindObject(prog, program.get(), bindprog);
      // Bind channel of program
      const JSON::Node& chan = prog.GetObjectValue("Channel");
      JSON::BindObject(chan, &(program->channel), bindchan);
      ret->insert(std::make_pair(program->startTime, program));
    }
    DBG(DBG_DEBUG, "%s: received count(%d)\n", __FUNCTION__, count);
    req_index += count; // Set next requested index
  }
  while (count == req_count);

  return ret;
}

///////////////////////////////////////////////////////////////////////////////
////
//// Dvr service
////
ProgramListPtr WSAPI::GetRecordedList1_5(unsigned n, bool descending)
{
  ProgramListPtr ret(new ProgramList);
  BUILTIN_BUFFER buf;
  uint32_t req_index = 0, req_count = FETCHSIZE, count = 0, total = 0;
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindlist = MythDTO::getListBindArray(proto);
  const bindings_t *bindprog = MythDTO::getProgramBindArray(proto);
  const bindings_t *bindchan = MythDTO::getChannelBindArray(proto);
  const bindings_t *bindreco = MythDTO::getRecordingBindArray(proto);
  const bindings_t *bindartw = MythDTO::getArtworkBindArray(proto);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/GetRecordedList");

  do
  {
    // Adjust the packet size
    if (n && req_count > (n - total))
      req_count = (n - total);

    req.ClearContent();
    uint32_to_string(req_index, &buf);
    req.SetContentParam("StartIndex", buf.data);
    uint32_to_string(req_count, &buf);
    req.SetContentParam("Count", buf.data);
    req.SetContentParam("Descending", BOOLSTR(descending));

    DBG(DBG_DEBUG, "%s: request index(%d) count(%d)\n", __FUNCTION__, req_index, req_count);
    WSResponse resp(req);
    if (!resp.IsSuccessful())
    {
      DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
      break;
    }
    const JSON::Document json(resp);
    const JSON::Node& root = json.GetRoot();
    if (!json.IsValid() || !root.IsObject())
    {
      DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
      break;
    }
    DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

    // Object: ProgramList
    const JSON::Node& plist = root.GetObjectValue("ProgramList");
    ItemList list = ItemList(); // Using default constructor
    JSON::BindObject(plist, &list, bindlist);
    // List has ProtoVer. Check it or sound alarm
    if (list.protoVer != proto)
    {
      InvalidateService();
      break;
    }
    count = 0;
    // Object: Programs[]
    const JSON::Node& progs = plist.GetObjectValue("Programs");
    // Iterates over the sequence elements.
    size_t ps = progs.Size();
    for (size_t pi = 0; pi < ps; ++pi)
    {
      ++count;
      const JSON::Node& prog = progs.GetArrayElement(pi);
      ProgramPtr program(new Program());  // Using default constructor
      // Bind the new program
      JSON::BindObject(prog, program.get(), bindprog);
      // Bind channel of program
      const JSON::Node& chan = prog.GetObjectValue("Channel");
      JSON::BindObject(chan, &(program->channel), bindchan);
      // Bind recording of program
      const JSON::Node& reco = prog.GetObjectValue("Recording");
      JSON::BindObject(reco, &(program->recording), bindreco);
      // Bind artwork list of program
      if (!prog.GetObjectValue("Artwork").IsNull())
      {
        const JSON::Node& arts = prog.GetObjectValue("Artwork").GetObjectValue("ArtworkInfos");
        size_t as = arts.Size();
        for (size_t pa = 0; pa < as; ++pa)
        {
          const JSON::Node& artw = arts.GetArrayElement(pa);
          Artwork artwork = Artwork();  // Using default constructor
          JSON::BindObject(artw, &artwork, bindartw);
          program->artwork.push_back(artwork);
        }
      }
      ret->push_back(program);
      ++total;
    }
    DBG(DBG_DEBUG, "%s: received count(%d)\n", __FUNCTION__, count);
    req_index += count; // Set next requested index
  }
  while (count == req_count && (!n || n > total));

  return ret;
}

ProgramPtr WSAPI::GetRecorded1_5(uint32_t chanid, time_t recstartts)
{
  ProgramPtr ret;
  BUILTIN_BUFFER buf;
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindprog = MythDTO::getProgramBindArray(proto);
  const bindings_t *bindchan = MythDTO::getChannelBindArray(proto);
  const bindings_t *bindreco = MythDTO::getRecordingBindArray(proto);
  const bindings_t *bindartw = MythDTO::getArtworkBindArray(proto);

  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/GetRecorded");
  uint32_to_string(chanid, &buf);
  req.SetContentParam("ChanId", buf.data);
  time_to_iso8601utc(recstartts, &buf);
  req.SetContentParam("StartTime", buf.data);
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return ret;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return ret;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  const JSON::Node& prog = root.GetObjectValue("Program");
  ProgramPtr program(new Program());  // Using default constructor
  // Bind the new program
  JSON::BindObject(prog, program.get(), bindprog);
  // Bind channel of program
  const JSON::Node& chan = prog.GetObjectValue("Channel");
  JSON::BindObject(chan, &(program->channel), bindchan);
  // Bind recording of program
  const JSON::Node& reco = prog.GetObjectValue("Recording");
  JSON::BindObject(reco, &(program->recording), bindreco);
  // Bind artwork list of program
  if (!prog.GetObjectValue("Artwork").IsNull())
  {
    const JSON::Node& arts = prog.GetObjectValue("Artwork").GetObjectValue("ArtworkInfos");
    size_t as = arts.Size();
    for (size_t pa = 0; pa < as; ++pa)
    {
      const JSON::Node& artw = arts.GetArrayElement(pa);
      Artwork artwork = Artwork();  // Using default constructor
      JSON::BindObject(artw, &artwork, bindartw);
      program->artwork.push_back(artwork);
    }
  }
  // Return valid program
  if (program->recording.startTs != INVALID_TIME)
    ret = program;
  return ret;
}

ProgramPtr WSAPI::GetRecorded6_0(uint32_t recordedid)
{
  ProgramPtr ret;
  BUILTIN_BUFFER buf;
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindprog = MythDTO::getProgramBindArray(proto);
  const bindings_t *bindchan = MythDTO::getChannelBindArray(proto);
  const bindings_t *bindreco = MythDTO::getRecordingBindArray(proto);
  const bindings_t *bindartw = MythDTO::getArtworkBindArray(proto);

  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/GetRecorded");
  uint32_to_string(recordedid, &buf);
  req.SetContentParam("RecordedId", buf.data);
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return ret;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return ret;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  const JSON::Node& prog = root.GetObjectValue("Program");
  ProgramPtr program(new Program());  // Using default constructor
  // Bind the new program
  JSON::BindObject(prog, program.get(), bindprog);
  // Bind channel of program
  const JSON::Node& chan = prog.GetObjectValue("Channel");
  JSON::BindObject(chan, &(program->channel), bindchan);
  // Bind recording of program
  const JSON::Node& reco = prog.GetObjectValue("Recording");
  JSON::BindObject(reco, &(program->recording), bindreco);
  // Bind artwork list of program
  if (!prog.GetObjectValue("Artwork").IsNull())
  {
    const JSON::Node& arts = prog.GetObjectValue("Artwork").GetObjectValue("ArtworkInfos");
    size_t as = arts.Size();
    for (size_t pa = 0; pa < as; ++pa)
    {
      const JSON::Node& artw = arts.GetArrayElement(pa);
      Artwork artwork = Artwork();  // Using default constructor
      JSON::BindObject(artw, &artwork, bindartw);
      program->artwork.push_back(artwork);
    }
  }
  // Return valid program
  if (program->recording.startTs != INVALID_TIME)
    ret = program;
  return ret;
}

bool WSAPI::DeleteRecording2_1(uint32_t chanid, time_t recstartts, bool forceDelete, bool allowRerecord)
{
  BUILTIN_BUFFER buf;

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/DeleteRecording", HRM_POST);
  uint32_to_string(chanid, &buf);
  req.SetContentParam("ChanId", buf.data);
  time_to_iso8601utc(recstartts, &buf);
  req.SetContentParam("StartTime", buf.data);
  req.SetContentParam("ForceDelete", BOOLSTR(forceDelete));
  req.SetContentParam("AllowRerecord", BOOLSTR(allowRerecord));
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return false;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return false;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  const JSON::Node& field = root.GetObjectValue("bool");
  if (field.IsTrue() || (field.IsString() && strcmp(field.GetStringValue().c_str(), "true") == 0))
    return true;
  return false;
}

bool WSAPI::DeleteRecording6_0(uint32_t recordedid, bool forceDelete, bool allowRerecord)
{
  BUILTIN_BUFFER buf;

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/DeleteRecording", HRM_POST);
  uint32_to_string(recordedid, &buf);
  req.SetContentParam("RecordedId", buf.data);
  req.SetContentParam("ForceDelete", BOOLSTR(forceDelete));
  req.SetContentParam("AllowRerecord", BOOLSTR(allowRerecord));
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return false;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return false;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  const JSON::Node& field = root.GetObjectValue("bool");
  if (field.IsTrue() || (field.IsString() && strcmp(field.GetStringValue().c_str(), "true") == 0))
    return true;
  return false;
}

bool WSAPI::UnDeleteRecording2_1(uint32_t chanid, time_t recstartts)
{
  BUILTIN_BUFFER buf;

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/UnDeleteRecording", HRM_POST);
  uint32_to_string(chanid, &buf);
  req.SetContentParam("ChanId", buf.data);
  time_to_iso8601utc(recstartts, &buf);
  req.SetContentParam("StartTime", buf.data);
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return false;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return false;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  const JSON::Node& field = root.GetObjectValue("bool");
  if (field.IsTrue() || (field.IsString() && strcmp(field.GetStringValue().c_str(), "true") == 0))
    return true;
  return false;
}

bool WSAPI::UnDeleteRecording6_0(uint32_t recordedid)
{
  BUILTIN_BUFFER buf;

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/UnDeleteRecording", HRM_POST);
  uint32_to_string(recordedid, &buf);
  req.SetContentParam("RecordedId", buf.data);
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return false;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return false;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  const JSON::Node& field = root.GetObjectValue("bool");
  if (field.IsTrue() || (field.IsString() && strcmp(field.GetStringValue().c_str(), "true") == 0))
    return true;
  return false;
}

bool WSAPI::UpdateRecordedWatchedStatus4_5(uint32_t chanid, time_t recstartts, bool watched)
{
  BUILTIN_BUFFER buf;

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/UpdateRecordedWatchedStatus", HRM_POST);
  uint32_to_string(chanid, &buf);
  req.SetContentParam("ChanId", buf.data);
  time_to_iso8601utc(recstartts, &buf);
  req.SetContentParam("StartTime", buf.data);
  req.SetContentParam("Watched", BOOLSTR(watched));
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return false;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return false;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  const JSON::Node& field = root.GetObjectValue("bool");
  if (field.IsTrue() || (field.IsString() && strcmp(field.GetStringValue().c_str(), "true") == 0))
    return true;
  return false;
}

bool WSAPI::UpdateRecordedWatchedStatus6_0(uint32_t recordedid, bool watched)
{
  BUILTIN_BUFFER buf;

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/UpdateRecordedWatchedStatus", HRM_POST);
  uint32_to_string(recordedid, &buf);
  req.SetContentParam("RecordedId", buf.data);
  req.SetContentParam("Watched", BOOLSTR(watched));
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return false;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return false;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  const JSON::Node& field = root.GetObjectValue("bool");
  if (field.IsTrue() || (field.IsString() && strcmp(field.GetStringValue().c_str(), "true") == 0))
    return true;
  return false;
}

MarkListPtr WSAPI::GetRecordedCommBreak6_1(uint32_t recordedid, int unit)
{
  BUILTIN_BUFFER buf;
  MarkListPtr ret(new MarkList);
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindcut = MythDTO::getCuttingBindArray(proto);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/GetRecordedCommBreak");
  uint32_to_string(recordedid, &buf);
  req.SetContentParam("RecordedId", buf.data);
  if (unit == 1)
    req.SetContentParam("OffsetType", "Position");
  else if (unit == 2)
    req.SetContentParam("OffsetType", "Duration");
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return ret;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return ret;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  // Object: 	CutList
  const JSON::Node& slist = root.GetObjectValue("CutList");
  // Object: 	Cuttings[]
  const JSON::Node& vcuts = slist.GetObjectValue("Cuttings");
  // Iterates over the sequence elements.
  size_t vs = vcuts.Size();
  for (size_t vi = 0; vi < vs; ++vi)
  {
    const JSON::Node& vcut = vcuts.GetArrayElement(vi);
    MarkPtr mark(new Mark());  // Using default constructor
    // Bind the new mark
    JSON::BindObject(vcut, mark.get(), bindcut);
    ret->push_back(mark);
  }
  return ret;
}

MarkListPtr WSAPI::GetRecordedCutList6_1(uint32_t recordedid, int unit)
{
  BUILTIN_BUFFER buf;
  MarkListPtr ret(new MarkList);
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindcut = MythDTO::getCuttingBindArray(proto);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/GetRecordedCutList");
  uint32_to_string(recordedid, &buf);
  req.SetContentParam("RecordedId", buf.data);
  if (unit == 1)
    req.SetContentParam("OffsetType", "Position");
  else if (unit == 2)
    req.SetContentParam("OffsetType", "Duration");
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return ret;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return ret;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  // Object: 	CutList
  const JSON::Node& slist = root.GetObjectValue("CutList");
  // Object: 	Cuttings[]
  const JSON::Node& vcuts = slist.GetObjectValue("Cuttings");
  // Iterates over the sequence elements.
  size_t vs = vcuts.Size();
  for (size_t vi = 0; vi < vs; ++vi)
  {
    const JSON::Node& vcut = vcuts.GetArrayElement(vi);
    MarkPtr mark(new Mark());  // Using default constructor
    // Bind the new mark
    JSON::BindObject(vcut, mark.get(), bindcut);
    ret->push_back(mark);
  }
  return ret;
}

bool WSAPI::SetSavedBookmark6_2(uint32_t recordedid, int unit, int64_t value)
{
  BUILTIN_BUFFER buf;

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/SetSavedBookmark", HRM_POST);
  uint32_to_string(recordedid, &buf);
  req.SetContentParam("RecordedId", buf.data);
  if (unit == 2)
    req.SetContentParam("OffsetType", "Duration");
  else
    req.SetContentParam("OffsetType", "Position");
  int64_to_string(value, &buf);
  req.SetContentParam("Offset", buf.data);
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return false;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return false;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  const JSON::Node& field = root.GetObjectValue("bool");
  if (field.IsTrue() || (field.IsString() && strcmp(field.GetStringValue().c_str(), "true") == 0))
    return true;
  return false;
}

int64_t WSAPI::GetSavedBookmark6_2(uint32_t recordedid, int unit)
{
  BUILTIN_BUFFER buf;

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/GetSavedBookmark", HRM_POST);
  uint32_to_string(recordedid, &buf);
  req.SetContentParam("RecordedId", buf.data);
  if (unit == 2)
    req.SetContentParam("OffsetType", "Duration");
  else
    req.SetContentParam("OffsetType", "Position");
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return false;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return false;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  int64_t value = 0;
  const JSON::Node& field = root.GetObjectValue("long");
  if (field.IsInt())
    value = field.GetBigIntValue();
  else if (!field.IsString() || string_to_int64(field.GetStringValue().c_str(), &value))
    return -1;
  return value;
}

static void ProcessRecordIN(unsigned proto, RecordSchedule& record)
{
  // Converting API codes to internal types
  record.type_t = RuleTypeFromString(proto, record.type);
  record.searchType_t = SearchTypeFromString(proto, record.searchType);
  record.dupMethod_t = DupMethodFromString(proto, record.dupMethod);
  record.dupIn_t = DupInFromString(proto, record.dupIn);
}

RecordScheduleListPtr WSAPI::GetRecordScheduleList1_5()
{
  RecordScheduleListPtr ret(new RecordScheduleList);
  BUILTIN_BUFFER buf;
  int32_t req_index = 0, req_count = FETCHSIZE, count = 0;
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindlist = MythDTO::getListBindArray(proto);
  const bindings_t *bindrec = MythDTO::getRecordScheduleBindArray(proto);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/GetRecordScheduleList");

  do
  {
    req.ClearContent();
    int32_to_string(req_index, &buf);
    req.SetContentParam("StartIndex", buf.data);
    int32_to_string(req_count, &buf);
    req.SetContentParam("Count", buf.data);

    DBG(DBG_DEBUG, "%s: request index(%d) count(%d)\n", __FUNCTION__, req_index, req_count);
    WSResponse resp(req);
    if (!resp.IsSuccessful())
    {
      DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
      break;
    }
    const JSON::Document json(resp);
    const JSON::Node& root = json.GetRoot();
    if (!json.IsValid() || !root.IsObject())
    {
      DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
      break;
    }
    DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

    // Object: RecRuleList
    const JSON::Node& rlist = root.GetObjectValue("RecRuleList");
    ItemList list = ItemList(); // Using default constructor
    JSON::BindObject(rlist, &list, bindlist);
    // List has ProtoVer. Check it or sound alarm
    if (list.protoVer != proto)
    {
      InvalidateService();
      break;
    }
    count = 0;
    // Object: RecRules[]
    const JSON::Node& recs = rlist.GetObjectValue("RecRules");
    // Iterates over the sequence elements.
    size_t rs = recs.Size();
    for (size_t ri = 0; ri < rs; ++ri)
    {
      ++count;
      const JSON::Node& rec = recs.GetArrayElement(ri);
      RecordSchedulePtr record(new RecordSchedule()); // Using default constructor
      // Bind the new record
      JSON::BindObject(rec, record.get(), bindrec);
      ProcessRecordIN(proto, *record);
      ret->push_back(record);
    }
    DBG(DBG_DEBUG, "%s: received count(%d)\n", __FUNCTION__, count);
    req_index += count; // Set next requested index
  }
  while (count == req_count);

  return ret;
}

RecordSchedulePtr WSAPI::GetRecordSchedule1_5(uint32_t recordid)
{
  RecordSchedulePtr ret;
  BUILTIN_BUFFER buf;
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindrec = MythDTO::getRecordScheduleBindArray(proto);

  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/GetRecordSchedule");
  uint32_to_string(recordid, &buf);
  req.SetContentParam("RecordId", buf.data);
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return ret;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return ret;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  const JSON::Node& rec = root.GetObjectValue("RecRule");
  RecordSchedulePtr record(new RecordSchedule()); // Using default constructor
  // Bind the new record
  JSON::BindObject(rec, record.get(), bindrec);
  // Return valid record
  if (record->recordId > 0)
  {
    ProcessRecordIN(proto, *record);
    ret = record;
  }
  return ret;
}

static void ProcessRecordOUT(unsigned proto, RecordSchedule& record)
{
  char buf[10];
  struct tm stm;
  time_t st = record.startTime;
  localtime_r(&st, &stm);
  // Set find time & day
  snprintf(buf, sizeof(buf), "%.2d:%.2d:%.2d", stm.tm_hour, stm.tm_min, stm.tm_sec);
  record.findTime = buf;
  record.findDay = (stm.tm_wday + 1) % 7;
  // Converting internal types to API codes
  record.type = RuleTypeToString(proto, record.type_t);
  record.searchType = SearchTypeToString(proto, record.searchType_t);
  record.dupMethod = DupMethodToString(proto, record.dupMethod_t);
  record.dupIn = DupInToString(proto, record.dupIn_t);
}

bool WSAPI::AddRecordSchedule1_5(RecordSchedule& record)
{
  BUILTIN_BUFFER buf;
  uint32_t recordid;
  unsigned proto = (unsigned)m_version.protocol;

  ProcessRecordOUT(proto, record);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/AddRecordSchedule", HRM_POST);

  req.SetContentParam("Title", record.title);
  req.SetContentParam("Subtitle", record.subtitle);
  req.SetContentParam("Description", record.description);
  req.SetContentParam("Category", record.category);
  time_to_iso8601utc(record.startTime, &buf);
  req.SetContentParam("StartTime", buf.data);
  time_to_iso8601utc(record.endTime, &buf);
  req.SetContentParam("EndTime", buf.data);
  req.SetContentParam("SeriesId", record.seriesId);
  req.SetContentParam("ProgramId", record.programId);
  uint32_to_string(record.chanId, &buf);
  req.SetContentParam("ChanId", buf.data);
  uint32_to_string(record.parentId, &buf);
  req.SetContentParam("ParentId", buf.data);
  req.SetContentParam("Inactive", BOOLSTR(record.inactive));
  uint16_to_string(record.season, &buf);
  req.SetContentParam("Season", buf.data);
  uint16_to_string(record.episode, &buf);
  req.SetContentParam("Episode", buf.data);
  req.SetContentParam("Inetref", record.inetref);
  req.SetContentParam("Type", record.type);
  req.SetContentParam("SearchType", record.searchType);
  int8_to_string(record.recPriority, &buf);
  req.SetContentParam("RecPriority", buf.data);
  uint32_to_string(record.preferredInput, &buf);
  req.SetContentParam("PreferredInput", buf.data);
  uint8_to_string(record.startOffset, &buf);
  req.SetContentParam("StartOffset", buf.data);
  uint8_to_string(record.endOffset, &buf);
  req.SetContentParam("EndOffset", buf.data);
  req.SetContentParam("DupMethod", record.dupMethod);
  req.SetContentParam("DupIn", record.dupIn);
  uint32_to_string(record.filter, &buf);
  req.SetContentParam("Filter", buf.data);
  req.SetContentParam("RecProfile", record.recProfile);
  req.SetContentParam("RecGroup", record.recGroup);
  req.SetContentParam("StorageGroup", record.storageGroup);
  req.SetContentParam("PlayGroup", record.playGroup);
  req.SetContentParam("AutoExpire", BOOLSTR(record.autoExpire));
  uint32_to_string(record.maxEpisodes, &buf);
  req.SetContentParam("MaxEpisodes", buf.data);
  req.SetContentParam("MaxNewest", BOOLSTR(record.maxNewest));
  req.SetContentParam("AutoCommflag", BOOLSTR(record.autoCommflag));
  req.SetContentParam("AutoTranscode", BOOLSTR(record.autoTranscode));
  req.SetContentParam("AutoMetaLookup", BOOLSTR(record.autoMetaLookup));
  req.SetContentParam("AutoUserJob1", BOOLSTR(record.autoUserJob1));
  req.SetContentParam("AutoUserJob2", BOOLSTR(record.autoUserJob2));
  req.SetContentParam("AutoUserJob3", BOOLSTR(record.autoUserJob3));
  req.SetContentParam("AutoUserJob4", BOOLSTR(record.autoUserJob4));
  uint32_to_string(record.transcoder, &buf);
  req.SetContentParam("Transcoder", buf.data);

  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return false;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return false;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  const JSON::Node& field = root.GetObjectValue("int");
  if (field.IsInt())
    recordid = (uint32_t) field.GetBigIntValue();
  else if (!field.IsString() || string_to_uint32(field.GetStringValue().c_str(), &recordid))
    return false;
  record.recordId = recordid;
  return true;
}

bool WSAPI::AddRecordSchedule1_7(RecordSchedule& record)
{
  BUILTIN_BUFFER buf;
  uint32_t recordid;
  unsigned proto = (unsigned)m_version.protocol;

  ProcessRecordOUT(proto, record);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/AddRecordSchedule", HRM_POST);

  req.SetContentParam("Title", record.title);
  req.SetContentParam("Subtitle", record.subtitle);
  req.SetContentParam("Description", record.description);
  req.SetContentParam("Category", record.category);
  time_to_iso8601utc(record.startTime, &buf);
  req.SetContentParam("StartTime", buf.data);
  time_to_iso8601utc(record.endTime, &buf);
  req.SetContentParam("EndTime", buf.data);
  req.SetContentParam("SeriesId", record.seriesId);
  req.SetContentParam("ProgramId", record.programId);
  uint32_to_string(record.chanId, &buf);
  req.SetContentParam("ChanId", buf.data);
  req.SetContentParam("Station", record.callSign);
  int8_to_string(record.findDay, &buf);
  req.SetContentParam("FindDay", buf.data);
  req.SetContentParam("FindTime", record.findTime);
  uint32_to_string(record.parentId, &buf);
  req.SetContentParam("ParentId", buf.data);
  req.SetContentParam("Inactive", BOOLSTR(record.inactive));
  uint16_to_string(record.season, &buf);
  req.SetContentParam("Season", buf.data);
  uint16_to_string(record.episode, &buf);
  req.SetContentParam("Episode", buf.data);
  req.SetContentParam("Inetref", record.inetref);
  req.SetContentParam("Type", record.type);
  req.SetContentParam("SearchType", record.searchType);
  int8_to_string(record.recPriority, &buf);
  req.SetContentParam("RecPriority", buf.data);
  uint32_to_string(record.preferredInput, &buf);
  req.SetContentParam("PreferredInput", buf.data);
  uint8_to_string(record.startOffset, &buf);
  req.SetContentParam("StartOffset", buf.data);
  uint8_to_string(record.endOffset, &buf);
  req.SetContentParam("EndOffset", buf.data);
  req.SetContentParam("DupMethod", record.dupMethod);
  req.SetContentParam("DupIn", record.dupIn);
  uint32_to_string(record.filter, &buf);
  req.SetContentParam("Filter", buf.data);
  req.SetContentParam("RecProfile", record.recProfile);
  req.SetContentParam("RecGroup", record.recGroup);
  req.SetContentParam("StorageGroup", record.storageGroup);
  req.SetContentParam("PlayGroup", record.playGroup);
  req.SetContentParam("AutoExpire", BOOLSTR(record.autoExpire));
  uint32_to_string(record.maxEpisodes, &buf);
  req.SetContentParam("MaxEpisodes", buf.data);
  req.SetContentParam("MaxNewest", BOOLSTR(record.maxNewest));
  req.SetContentParam("AutoCommflag", BOOLSTR(record.autoCommflag));
  req.SetContentParam("AutoTranscode", BOOLSTR(record.autoTranscode));
  req.SetContentParam("AutoMetaLookup", BOOLSTR(record.autoMetaLookup));
  req.SetContentParam("AutoUserJob1", BOOLSTR(record.autoUserJob1));
  req.SetContentParam("AutoUserJob2", BOOLSTR(record.autoUserJob2));
  req.SetContentParam("AutoUserJob3", BOOLSTR(record.autoUserJob3));
  req.SetContentParam("AutoUserJob4", BOOLSTR(record.autoUserJob4));
  uint32_to_string(record.transcoder, &buf);
  req.SetContentParam("Transcoder", buf.data);

  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return false;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return false;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  const JSON::Node& field = root.GetObjectValue("uint");
  if (field.IsInt())
    recordid = (uint32_t) field.GetBigIntValue();
  else if (!field.IsString() || string_to_uint32(field.GetStringValue().c_str(), &recordid))
    return false;
  record.recordId = recordid;
  return true;
}

bool WSAPI::UpdateRecordSchedule1_7(RecordSchedule& record)
{
  BUILTIN_BUFFER buf;
  unsigned proto = (unsigned)m_version.protocol;

  ProcessRecordOUT(proto, record);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/UpdateRecordSchedule", HRM_POST);

  uint32_to_string(record.recordId, &buf);
  req.SetContentParam("RecordId", buf.data);
  req.SetContentParam("Title", record.title);
  req.SetContentParam("Subtitle", record.subtitle);
  req.SetContentParam("Description", record.description);
  req.SetContentParam("Category", record.category);
  time_to_iso8601utc(record.startTime, &buf);
  req.SetContentParam("StartTime", buf.data);
  time_to_iso8601utc(record.endTime, &buf);
  req.SetContentParam("EndTime", buf.data);
  req.SetContentParam("SeriesId", record.seriesId);
  req.SetContentParam("ProgramId", record.programId);
  uint32_to_string(record.chanId, &buf);
  req.SetContentParam("ChanId", buf.data);
  req.SetContentParam("Station", record.callSign);
  int8_to_string(record.findDay, &buf);
  req.SetContentParam("FindDay", buf.data);
  req.SetContentParam("FindTime", record.findTime);
  uint32_to_string(record.parentId, &buf);
  req.SetContentParam("ParentId", buf.data);
  req.SetContentParam("Inactive", BOOLSTR(record.inactive));
  uint16_to_string(record.season, &buf);
  req.SetContentParam("Season", buf.data);
  uint16_to_string(record.episode, &buf);
  req.SetContentParam("Episode", buf.data);
  req.SetContentParam("Inetref", record.inetref);
  req.SetContentParam("Type", record.type);
  req.SetContentParam("SearchType", record.searchType);
  int8_to_string(record.recPriority, &buf);
  req.SetContentParam("RecPriority", buf.data);
  uint32_to_string(record.preferredInput, &buf);
  req.SetContentParam("PreferredInput", buf.data);
  uint8_to_string(record.startOffset, &buf);
  req.SetContentParam("StartOffset", buf.data);
  uint8_to_string(record.endOffset, &buf);
  req.SetContentParam("EndOffset", buf.data);
  req.SetContentParam("DupMethod", record.dupMethod);
  req.SetContentParam("DupIn", record.dupIn);
  uint32_to_string(record.filter, &buf);
  req.SetContentParam("Filter", buf.data);
  req.SetContentParam("RecProfile", record.recProfile);
  req.SetContentParam("RecGroup", record.recGroup);
  req.SetContentParam("StorageGroup", record.storageGroup);
  req.SetContentParam("PlayGroup", record.playGroup);
  req.SetContentParam("AutoExpire", BOOLSTR(record.autoExpire));
  uint32_to_string(record.maxEpisodes, &buf);
  req.SetContentParam("MaxEpisodes", buf.data);
  req.SetContentParam("MaxNewest", BOOLSTR(record.maxNewest));
  req.SetContentParam("AutoCommflag", BOOLSTR(record.autoCommflag));
  req.SetContentParam("AutoTranscode", BOOLSTR(record.autoTranscode));
  req.SetContentParam("AutoMetaLookup", BOOLSTR(record.autoMetaLookup));
  req.SetContentParam("AutoUserJob1", BOOLSTR(record.autoUserJob1));
  req.SetContentParam("AutoUserJob2", BOOLSTR(record.autoUserJob2));
  req.SetContentParam("AutoUserJob3", BOOLSTR(record.autoUserJob3));
  req.SetContentParam("AutoUserJob4", BOOLSTR(record.autoUserJob4));
  uint32_to_string(record.transcoder, &buf);
  req.SetContentParam("Transcoder", buf.data);

  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return false;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return false;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  const JSON::Node& field = root.GetObjectValue("bool");
  if (field.IsTrue() || (field.IsString() && strcmp(field.GetStringValue().c_str(), "true") == 0))
    return true;
  return false;
}

bool WSAPI::DisableRecordSchedule1_5(uint32_t recordid)
{
  BUILTIN_BUFFER buf;

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/DisableRecordSchedule", HRM_POST);

  uint32_to_string(recordid, &buf);
  req.SetContentParam("RecordId", buf.data);

  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return false;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return false;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  const JSON::Node& field = root.GetObjectValue("bool");
  if (field.IsTrue() || (field.IsString() && strcmp(field.GetStringValue().c_str(), "true") == 0))
    return true;
  return false;
}

bool WSAPI::EnableRecordSchedule1_5(uint32_t recordid)
{
  BUILTIN_BUFFER buf;

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/EnableRecordSchedule", HRM_POST);

  uint32_to_string(recordid, &buf);
  req.SetContentParam("RecordId", buf.data);

  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return false;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return false;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  const JSON::Node& field = root.GetObjectValue("bool");
  if (field.IsTrue() || (field.IsString() && strcmp(field.GetStringValue().c_str(), "true") == 0))
    return true;
  return false;
}

bool WSAPI::RemoveRecordSchedule1_5(uint32_t recordid)
{
  BUILTIN_BUFFER buf;

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/RemoveRecordSchedule", HRM_POST);

  uint32_to_string(recordid, &buf);
  req.SetContentParam("RecordId", buf.data);

  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return false;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return false;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  const JSON::Node& field = root.GetObjectValue("bool");
  if (field.IsTrue() || (field.IsString() && strcmp(field.GetStringValue().c_str(), "true") == 0))
    return true;
  return false;
}

ProgramListPtr WSAPI::GetUpcomingList1_5()
{
  // Only for backward compatibility (0.27)
  ProgramListPtr ret = GetUpcomingList2_2();
  // Add being recorded (https://code.mythtv.org/trac/changeset/3084ebc/mythtv)
  ProgramListPtr recordings = GetRecordedList(20, true);
  for (Myth::ProgramList::iterator it = recordings->begin(); it != recordings->end(); ++it)
  {
    if ((*it)->recording.status == RS_RECORDING)
      ret->push_back(*it);
  }
  return ret;
}

ProgramListPtr WSAPI::GetUpcomingList2_2()
{
  ProgramListPtr ret(new ProgramList);
  BUILTIN_BUFFER buf;
  int32_t req_index = 0, req_count = FETCHSIZE, count = 0;
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindlist = MythDTO::getListBindArray(proto);
  const bindings_t *bindprog = MythDTO::getProgramBindArray(proto);
  const bindings_t *bindchan = MythDTO::getChannelBindArray(proto);
  const bindings_t *bindreco = MythDTO::getRecordingBindArray(proto);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/GetUpcomingList");

  do
  {
    req.ClearContent();
    int32_to_string(req_index, &buf);
    req.SetContentParam("StartIndex", buf.data);
    int32_to_string(req_count, &buf);
    req.SetContentParam("Count", buf.data);
    req.SetContentParam("ShowAll", "true");

    DBG(DBG_DEBUG, "%s: request index(%d) count(%d)\n", __FUNCTION__, req_index, req_count);
    WSResponse resp(req);
    if (!resp.IsSuccessful())
    {
      DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
      break;
    }
    const JSON::Document json(resp);
    const JSON::Node& root = json.GetRoot();
    if (!json.IsValid() || !root.IsObject())
    {
      DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
      break;
    }
    DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

    // Object: ProgramList
    const JSON::Node& plist = root.GetObjectValue("ProgramList");
    ItemList list = ItemList(); // Using default constructor
    JSON::BindObject(plist, &list, bindlist);
    // List has ProtoVer. Check it or sound alarm
    if (list.protoVer != proto)
    {
      InvalidateService();
      break;
    }
    count = 0;
    // Object: Programs[]
    const JSON::Node& progs = plist.GetObjectValue("Programs");
    // Iterates over the sequence elements.
    size_t ps = progs.Size();
    for (size_t pi = 0; pi < ps; ++pi)
    {
      ++count;
      const JSON::Node& prog = progs.GetArrayElement(pi);
      ProgramPtr program(new Program());  // Using default constructor
      // Bind the new program
      JSON::BindObject(prog, program.get(), bindprog);
      // Bind channel of program
      const JSON::Node& chan = prog.GetObjectValue("Channel");
      JSON::BindObject(chan, &(program->channel), bindchan);
      // Bind recording of program
      const JSON::Node& reco = prog.GetObjectValue("Recording");
      JSON::BindObject(reco, &(program->recording), bindreco);
      ret->push_back(program);
    }
    DBG(DBG_DEBUG, "%s: received count(%d)\n", __FUNCTION__, count);
    req_index += count; // Set next requested index
  }
  while (count == req_count);

  return ret;
}

ProgramListPtr WSAPI::GetConflictList1_5()
{
  ProgramListPtr ret(new ProgramList);
  BUILTIN_BUFFER buf;
  int32_t req_index = 0, req_count = FETCHSIZE, count = 0;
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindlist = MythDTO::getListBindArray(proto);
  const bindings_t *bindprog = MythDTO::getProgramBindArray(proto);
  const bindings_t *bindchan = MythDTO::getChannelBindArray(proto);
  const bindings_t *bindreco = MythDTO::getRecordingBindArray(proto);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/GetConflictList");

  do
  {
    req.ClearContent();
    int32_to_string(req_index, &buf);
    req.SetContentParam("StartIndex", buf.data);
    int32_to_string(req_count, &buf);
    req.SetContentParam("Count", buf.data);

    DBG(DBG_DEBUG, "%s: request index(%d) count(%d)\n", __FUNCTION__, req_index, req_count);
    WSResponse resp(req);
    if (!resp.IsSuccessful())
    {
      DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
      break;
    }
    const JSON::Document json(resp);
    const JSON::Node& root = json.GetRoot();
    if (!json.IsValid() || !root.IsObject())
    {
      DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
      break;
    }
    DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

    // Object: ProgramList
    const JSON::Node& plist = root.GetObjectValue("ProgramList");
    ItemList list = ItemList(); // Using default constructor
    JSON::BindObject(plist, &list, bindlist);
    // List has ProtoVer. Check it or sound alarm
    if (list.protoVer != proto)
    {
      InvalidateService();
      break;
    }
    count = 0;
    // Object: Programs[]
    const JSON::Node& progs = plist.GetObjectValue("Programs");
    // Iterates over the sequence elements.
    size_t ps = progs.Size();
    for (size_t pi = 0; pi < ps; ++pi)
    {
      ++count;
      const JSON::Node& prog = progs.GetArrayElement(pi);
      ProgramPtr program(new Program());  // Using default constructor
      // Bind the new program
      JSON::BindObject(prog, program.get(), bindprog);
      // Bind channel of program
      const JSON::Node& chan = prog.GetObjectValue("Channel");
      JSON::BindObject(chan, &(program->channel), bindchan);
      // Bind recording of program
      const JSON::Node& reco = prog.GetObjectValue("Recording");
      JSON::BindObject(reco, &(program->recording), bindreco);
      ret->push_back(program);
    }
    DBG(DBG_DEBUG, "%s: received count(%d)\n", __FUNCTION__, count);
    req_index += count; // Set next requested index
  }
  while (count == req_count);

  return ret;
}

ProgramListPtr WSAPI::GetExpiringList1_5()
{
  ProgramListPtr ret(new ProgramList);
  BUILTIN_BUFFER buf;
  int32_t req_index = 0, req_count = FETCHSIZE, count = 0;
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindlist = MythDTO::getListBindArray(proto);
  const bindings_t *bindprog = MythDTO::getProgramBindArray(proto);
  const bindings_t *bindchan = MythDTO::getChannelBindArray(proto);
  const bindings_t *bindreco = MythDTO::getRecordingBindArray(proto);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/GetExpiringList");

  do
  {
    req.ClearContent();
    int32_to_string(req_index, &buf);
    req.SetContentParam("StartIndex", buf.data);
    int32_to_string(req_count, &buf);
    req.SetContentParam("Count", buf.data);

    DBG(DBG_DEBUG, "%s: request index(%d) count(%d)\n", __FUNCTION__, req_index, req_count);
    WSResponse resp(req);
    if (!resp.IsSuccessful())
    {
      DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
      break;
    }
    const JSON::Document json(resp);
    const JSON::Node& root = json.GetRoot();
    if (!json.IsValid() || !root.IsObject())
    {
      DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
      break;
    }
    DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

    // Object: ProgramList
    const JSON::Node& plist = root.GetObjectValue("ProgramList");
    ItemList list = ItemList(); // Using default constructor
    JSON::BindObject(plist, &list, bindlist);
    // List has ProtoVer. Check it or sound alarm
    if (list.protoVer != proto)
    {
      InvalidateService();
      break;
    }
    count = 0;
    // Object: Programs[]
    const JSON::Node& progs = plist.GetObjectValue("Programs");
    // Iterates over the sequence elements.
    size_t ps = progs.Size();
    for (size_t pi = 0; pi < ps; ++pi)
    {
      ++count;
      const JSON::Node& prog = progs.GetArrayElement(pi);
      ProgramPtr program(new Program());  // Using default constructor
      // Bind the new program
      JSON::BindObject(prog, program.get(), bindprog);
      // Bind channel of program
      const JSON::Node& chan = prog.GetObjectValue("Channel");
      JSON::BindObject(chan, &(program->channel), bindchan);
      // Bind recording of program
      const JSON::Node& reco = prog.GetObjectValue("Recording");
      JSON::BindObject(reco, &(program->recording), bindreco);
      ret->push_back(program);
    }
    DBG(DBG_DEBUG, "%s: received count(%d)\n", __FUNCTION__, count);
    req_index += count; // Set next requested index
  }
  while (count == req_count);

  return ret;
}

StringListPtr WSAPI::GetRecGroupList1_5()
{
  StringListPtr ret(new StringList);

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Dvr/GetRecGroupList");
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return ret;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return ret;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  // Object: Strings
  const JSON::Node& list = root.GetObjectValue("StringList");
  if (list.IsArray())
  {
    size_t s = list.Size();
    for (size_t i = 0; i < s; ++i)
    {
      const JSON::Node& val = list.GetArrayElement(i);
      if (val.IsString())
      {
        ret->push_back(val.GetStringValue());
      }
    }
  }
  return ret;
}

///////////////////////////////////////////////////////////////////////////////
////
//// Content service
////
WSStreamPtr WSAPI::GetFile1_32(const std::string& filename, const std::string& sgname)
{
  WSStreamPtr ret;

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestService("/Content/GetFile");
  req.SetContentParam("StorageGroup", sgname);
  req.SetContentParam("FileName", filename);
  WSResponse *resp = new WSResponse(req, 1, false, true);
  if (!resp->IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    delete resp;
    return ret;
  }
  ret.reset(new WSStream(resp));
  return ret;
}

WSStreamPtr WSAPI::GetChannelIcon1_32(uint32_t chanid, unsigned width, unsigned height)
{
  WSStreamPtr ret;
  BUILTIN_BUFFER buf;

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestService("/Guide/GetChannelIcon");
  uint32_to_string(chanid, &buf);
  req.SetContentParam("ChanId", buf.data);
  if (width)
  {
    uint32_to_string(width, &buf);
    req.SetContentParam("Width", buf.data);
  }
  if (height)
  {
    uint32_to_string(height, &buf);
    req.SetContentParam("Height", buf.data);
  }
  WSResponse *resp = new WSResponse(req, 1, false, true);
  if (!resp->IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    delete resp;
    return ret;
  }
  ret.reset(new WSStream(resp));
  return ret;
}

std::string WSAPI::GetChannelIconUrl1_32(uint32_t chanid, unsigned width, unsigned height)
{
  BUILTIN_BUFFER buf;
  std::string uri;
  uri.reserve(95);
  uri.append("http://").append(m_server);
  if (m_port != 80)
  {
    uint32_to_string(m_port, &buf);
    uri.append(":").append(buf.data);
  }
  uri.append("/Guide/GetChannelIcon");
  uint32_to_string(chanid, &buf);
  uri.append("?ChanId=").append(buf.data);
  if (width)
  {
    uint32_to_string(width, &buf);
    uri.append("&Width=").append(buf.data);
  }
  if (height)
  {
    uint32_to_string(height, &buf);
    uri.append("&Height=").append(buf.data);
  }
  return uri;
}

WSStreamPtr WSAPI::GetPreviewImage1_32(uint32_t chanid, time_t recstartts, unsigned width, unsigned height)
{
  WSStreamPtr ret;
  BUILTIN_BUFFER buf;

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestService("/Content/GetPreviewImage");
  uint32_to_string(chanid, &buf);
  req.SetContentParam("ChanId", buf.data);
  time_to_iso8601utc(recstartts, &buf);
  req.SetContentParam("StartTime", buf.data);
  if (width)
  {
    uint32_to_string(width, &buf);
    req.SetContentParam("Width", buf.data);
  }
  if (height)
  {
    uint32_to_string(height, &buf);
    req.SetContentParam("Height", buf.data);
  }
  WSResponse *resp = new WSResponse(req, 1, false, true);
  if (!resp->IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    delete resp;
    return ret;
  }
  ret.reset(new WSStream(resp));
  return ret;
}

std::string WSAPI::GetPreviewImageUrl1_32(uint32_t chanid, time_t recstartts, unsigned width, unsigned height)
{
  BUILTIN_BUFFER buf;
  std::string uri;
  uri.reserve(95);
  uri.append("http://").append(m_server);
  if (m_port != 80)
  {
    uint32_to_string(m_port, &buf);
    uri.append(":").append(buf.data);
  }
  uri.append("/Content/GetPreviewImage");
  uint32_to_string(chanid, &buf);
  uri.append("?ChanId=").append(buf.data);
  time_to_iso8601utc(recstartts, &buf);
  uri.append("&StartTime=").append(encode_param(buf.data));
  if (width)
  {
    uint32_to_string(width, &buf);
    uri.append("&Width=").append(buf.data);
  }
  if (height)
  {
    uint32_to_string(height, &buf);
    uri.append("&Height=").append(buf.data);
  }
  return uri;
}

WSStreamPtr WSAPI::GetRecordingArtwork1_32(const std::string& type, const std::string& inetref, uint16_t season, unsigned width, unsigned height)
{
  WSStreamPtr ret;
  BUILTIN_BUFFER buf;

  // Initialize request header
  WSRequest req = WSRequest(m_server, m_port);
  req.RequestService("/Content/GetRecordingArtwork");
  req.SetContentParam("Type", type.c_str());
  req.SetContentParam("Inetref", inetref.c_str());
  uint16_to_string(season, &buf);
  req.SetContentParam("Season", buf.data);
  if (width)
  {
    uint32_to_string(width, &buf);
    req.SetContentParam("Width", buf.data);
  }
  if (height)
  {
    uint32_to_string(height, &buf);
    req.SetContentParam("Height", buf.data);
  }
  WSResponse *resp = new WSResponse(req, 1, false, true);
  if (!resp->IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    delete resp;
    return ret;
  }
  ret.reset(new WSStream(resp));
  return ret;
}

std::string WSAPI::GetRecordingArtworkUrl1_32(const std::string& type, const std::string& inetref, uint16_t season, unsigned width, unsigned height)
{
  BUILTIN_BUFFER buf;
  std::string uri;
  uri.reserve(127);
  uri.append("http://").append(m_server);
  if (m_port != 80)
  {
    uint32_to_string(m_port, &buf);
    uri.append(":").append(buf.data);
  }
  uri.append("/Content/GetRecordingArtwork");
  uri.append("?Type=").append(encode_param(type));
  uri.append("&Inetref=").append(encode_param(inetref));
  uint16_to_string(season, &buf);
  uri.append("&Season=").append(buf.data);
  if (width)
  {
    uint32_to_string(width, &buf);
    uri.append("&Width=").append(buf.data);
  }
  if (height)
  {
    uint32_to_string(height, &buf);
    uri.append("&Height=").append(buf.data);
  }
  return uri;
}

ArtworkListPtr WSAPI::GetRecordingArtworkList1_32(uint32_t chanid, time_t recstartts)
{
  ArtworkListPtr ret(new ArtworkList);
  BUILTIN_BUFFER buf;
  unsigned proto = (unsigned)m_version.protocol;

  // Get bindings for protocol version
  const bindings_t *bindartw = MythDTO::getArtworkBindArray(proto);

  WSRequest req = WSRequest(m_server, m_port);
  req.RequestAccept(CT_JSON);
  req.RequestService("/Content/GetRecordingArtworkList");
  uint32_to_string(chanid, &buf);
  req.SetContentParam("ChanId", buf.data);
  time_to_iso8601utc(recstartts, &buf);
  req.SetContentParam("StartTime", buf.data);
  WSResponse resp(req);
  if (!resp.IsSuccessful())
  {
    DBG(DBG_ERROR, "%s: invalid response\n", __FUNCTION__);
    return ret;
  }
  const JSON::Document json(resp);
  const JSON::Node& root = json.GetRoot();
  if (!json.IsValid() || !root.IsObject())
  {
    DBG(DBG_ERROR, "%s: unexpected content\n", __FUNCTION__);
    return ret;
  }
  DBG(DBG_DEBUG, "%s: content parsed\n", __FUNCTION__);

  const JSON::Node& list = root.GetObjectValue("ArtworkInfoList");
  // Bind artwork list
  const JSON::Node& arts = list.GetObjectValue("ArtworkInfos");
  size_t as = arts.Size();
  for (size_t pa = 0; pa < as; ++pa)
  {
    const JSON::Node& artw = arts.GetArrayElement(pa);
    ArtworkPtr artwork(new Artwork());  // Using default constructor
    JSON::BindObject(artw, artwork.get(), bindartw);
    ret->push_back(artwork);
  }
  return ret;
}
