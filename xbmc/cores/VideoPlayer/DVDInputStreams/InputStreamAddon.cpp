/*
 *      Copyright (C) 2015 Team Kodi
 *      http://kodi.tv
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
 *  along with Kodi; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "InputStreamAddon.h"
#include "TimingConstants.h"
#include "addons/binary-addons/AddonDll.h"
#include "addons/binary-addons/BinaryAddonBase.h"
#include "cores/VideoPlayer/DVDClock.h"
#include "cores/VideoPlayer/DVDDemuxers/DVDDemux.h"
#include "cores/VideoPlayer/DVDDemuxers/DVDDemuxUtils.h"
#include "filesystem/SpecialProtocol.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"

using namespace ADDON;

CInputStreamAddon::CInputStreamAddon(BinaryAddonBasePtr& addonBase, IVideoPlayer* player, const CFileItem& fileitem)
  : IAddonInstanceHandler(ADDON_INSTANCE_INPUTSTREAM, addonBase),
    CDVDInputStream(DVDSTREAM_TYPE_ADDON, fileitem),
    m_player(player)
{
  std::string listitemprops = addonBase->Type(ADDON_INPUTSTREAM)->GetValue("@listitemprops").asString();
  std::string name(addonBase->ID());

  m_fileItemProps = StringUtils::Tokenize(listitemprops, "|");
  for (auto &key : m_fileItemProps)
  {
    StringUtils::Trim(key);
    key = name + "." + key;
  }

  m_struct = {{ 0 }};
}

CInputStreamAddon::~CInputStreamAddon()
{
  Close();
}

bool CInputStreamAddon::Supports(BinaryAddonBasePtr& addonBase, const CFileItem &fileitem)
{
  // check if a specific inputstream addon is requested
  CVariant addon = fileitem.GetProperty("inputstreamaddon");
  if (!addon.isNull())
    return (addon.asString() == addonBase->ID());

  // check protocols
  std::string protocol = fileitem.GetURL().GetProtocol();
  if (!protocol.empty())
  {
    std::string protocols = addonBase->Type(ADDON_INPUTSTREAM)->GetValue("@protocols").asString();
    if (!protocols.empty())
    {
      std::vector<std::string> protocolsList = StringUtils::Tokenize(protocols, "|");
      for (auto& value : protocolsList)
      {
        StringUtils::Trim(value);
        if (value == protocol)
          return true;
      }
    }
  }

  std::string filetype = fileitem.GetURL().GetFileType();
  if (!filetype.empty())
  {
    std::string extensions = addonBase->Type(ADDON_INPUTSTREAM)->GetValue("@extension").asString();
    if (!extensions.empty())
    {
      std::vector<std::string> extensionsList = StringUtils::Tokenize(extensions, "|");
      for (auto& value : extensionsList)
      {
        StringUtils::Trim(value);
        if (value == filetype)
          return true;
      }
    }
  }

  return false;
}

bool CInputStreamAddon::Open()
{
  m_struct.toKodi.kodiInstance = this;
  m_struct.toKodi.free_demux_packet = cb_free_demux_packet;
  m_struct.toKodi.allocate_demux_packet = cb_allocate_demux_packet;
  if (!CreateInstance(&m_struct) || !m_struct.toAddon.open)
    return false;

  INPUTSTREAM props;
  std::map<std::string, std::string> propsMap;
  for (auto &key : m_fileItemProps)
  {
    if (m_item.GetProperty(key).isNull())
      continue;
    propsMap[key] = m_item.GetProperty(key).asString();
  }

  props.m_nCountInfoValues = 0;
  for (auto &pair : propsMap)
  {
    props.m_ListItemProperties[props.m_nCountInfoValues].m_strKey = pair.first.c_str();
    props.m_ListItemProperties[props.m_nCountInfoValues].m_strValue = pair.second.c_str();
    props.m_nCountInfoValues++;
  }

  props.m_strURL = m_item.GetPath().c_str();

  std::string libFolder = URIUtils::GetDirectory(Addon()->Path());
  std::string profileFolder = CSpecialProtocol::TranslatePath(Addon()->Profile());
  props.m_libFolder = libFolder.c_str();
  props.m_profileFolder = profileFolder.c_str();

  unsigned int videoWidth = 1280;
  unsigned int videoHeight = 720;
  if (m_player)
    m_player->GetVideoResolution(videoWidth, videoHeight);
  SetVideoResolution(videoWidth, videoHeight);

  bool ret = m_struct.toAddon.open(&m_struct, &props);
  if (ret)
  {
    memset(&m_caps, 0, sizeof(m_caps));
    m_struct.toAddon.get_capabilities(&m_struct, &m_caps);
  }

  UpdateStreams();
  return ret;
}

void CInputStreamAddon::Close()
{
  if (m_struct.toAddon.close)
    m_struct.toAddon.close(&m_struct);
  DestroyInstance();
  m_struct = {{ 0 }};
}

bool CInputStreamAddon::IsEOF()
{
  return false;
}

int CInputStreamAddon::Read(uint8_t* buf, int buf_size)
{
  if (!m_struct.toAddon.read_stream)
    return -1;

  return m_struct.toAddon.read_stream(&m_struct, buf, buf_size);
}

int64_t CInputStreamAddon::Seek(int64_t offset, int whence)
{
  if (!m_struct.toAddon.seek_stream)
    return -1;

  return m_struct.toAddon.seek_stream(&m_struct, offset, whence);
}

int64_t CInputStreamAddon::PositionStream()
{
  if (!m_struct.toAddon.position_stream)
    return -1;

  return m_struct.toAddon.position_stream(&m_struct);
}
int64_t CInputStreamAddon::GetLength()
{
  if (!m_struct.toAddon.length_stream)
    return -1;

  return m_struct.toAddon.length_stream(&m_struct);
}

bool CInputStreamAddon::Pause(double time)
{
  if (!m_struct.toAddon.pause_stream)
    return false;

  m_struct.toAddon.pause_stream(&m_struct, time);
  return true;
}

bool CInputStreamAddon::CanSeek()
{
  return (m_caps.m_mask & INPUTSTREAM_CAPABILITIES::SUPPORTS_SEEK) != 0;
}

bool CInputStreamAddon::CanPause()
{
  return (m_caps.m_mask & INPUTSTREAM_CAPABILITIES::SUPPORTS_PAUSE) != 0;
}

// IDisplayTime
CDVDInputStream::IDisplayTime* CInputStreamAddon::GetIDisplayTime()
{
  if ((m_caps.m_mask & INPUTSTREAM_CAPABILITIES::SUPPORTS_IDISPLAYTIME) == 0)
    return nullptr;

  return this;
}

int CInputStreamAddon::GetTotalTime()
{
  if (!m_struct.toAddon.get_total_time)
    return 0;

  return m_struct.toAddon.get_total_time(&m_struct);
}

int CInputStreamAddon::GetTime()
{
  if (!m_struct.toAddon.get_time)
    return 0;

  return m_struct.toAddon.get_time(&m_struct);
}

// IPosTime
CDVDInputStream::IPosTime* CInputStreamAddon::GetIPosTime()
{
  if ((m_caps.m_mask & INPUTSTREAM_CAPABILITIES::SUPPORTS_IPOSTIME) == 0)
    return nullptr;

  return this;
}

bool CInputStreamAddon::PosTime(int ms)
{
  if (!m_struct.toAddon.pos_time)
    return false;

  return m_struct.toAddon.pos_time(&m_struct, ms);
}

// IDemux
CDVDInputStream::IDemux* CInputStreamAddon::GetIDemux()
{
  if ((m_caps.m_mask & INPUTSTREAM_CAPABILITIES::SUPPORTS_IDEMUX) == 0)
    return nullptr;

  return this;
}

bool CInputStreamAddon::OpenDemux()
{
  if ((m_caps.m_mask & INPUTSTREAM_CAPABILITIES::SUPPORTS_IDEMUX) != 0)
    return true;
  else
    return false;
}

DemuxPacket* CInputStreamAddon::ReadDemux()
{
  if (!m_struct.toAddon.demux_read)
    return nullptr;

  DemuxPacket* pPacket = m_struct.toAddon.demux_read(&m_struct);

  if (!pPacket)
  {
    return nullptr;
  }
  else if (pPacket->iStreamId == DMX_SPECIALID_STREAMINFO)
  {
    UpdateStreams();
  }
  else if (pPacket->iStreamId == DMX_SPECIALID_STREAMCHANGE)
  {
    UpdateStreams();
  }

  return pPacket;
}

std::vector<CDemuxStream*> CInputStreamAddon::GetStreams() const
{
  std::vector<CDemuxStream*> streams;

  for (auto stream : m_streams)
    streams.push_back(stream.second);

  return streams;
}

CDemuxStream* CInputStreamAddon::GetStream(int streamId) const
{
  auto stream = m_streams.find(streamId);
  if (stream != m_streams.end())
    return stream->second;

  return nullptr;
}

void CInputStreamAddon::EnableStream(int streamId, bool enable)
{
  if (!m_struct.toAddon.enable_stream)
    return;

  auto stream = m_streams.find(streamId);
  if (stream == m_streams.end())
    return;

  m_struct.toAddon.enable_stream(&m_struct, stream->second->uniqueId, enable);
}

int CInputStreamAddon::GetNrOfStreams() const
{
  return m_streams.size();
}

void CInputStreamAddon::SetSpeed(int speed)
{
  if (!m_struct.toAddon.demux_set_speed)
    return;

  m_struct.toAddon.demux_set_speed(&m_struct, speed);
}

bool CInputStreamAddon::SeekTime(double time, bool backward, double* startpts)
{
  if (!m_struct.toAddon.demux_seek_time)
    return false;

  if ((m_caps.m_mask & INPUTSTREAM_CAPABILITIES::SUPPORTS_IPOSTIME) != 0)
  {
    if (!PosTime(static_cast<int>(time)))
      return false;

    FlushDemux();

    if(startpts)
      *startpts = DVD_NOPTS_VALUE;
    return true;
  }

  return m_struct.toAddon.demux_seek_time(&m_struct, time, backward, startpts);
}

void CInputStreamAddon::AbortDemux()
{
  if (m_struct.toAddon.demux_abort)
    m_struct.toAddon.demux_abort(&m_struct);
}

void CInputStreamAddon::FlushDemux()
{
  if (m_struct.toAddon.demux_flush)
    m_struct.toAddon.demux_flush(&m_struct);
}

void CInputStreamAddon::SetVideoResolution(int width, int height)
{
  if (m_struct.toAddon.set_video_resolution)
    m_struct.toAddon.set_video_resolution(&m_struct, width, height);
}

bool CInputStreamAddon::IsRealTimeStream()
{
  if (m_struct.toAddon.is_real_time_stream)
    return m_struct.toAddon.is_real_time_stream(&m_struct);
  return false;
}

void CInputStreamAddon::UpdateStreams()
{
  DisposeStreams();

  INPUTSTREAM_IDS streamIDs = m_struct.toAddon.get_stream_ids(&m_struct);
  if (streamIDs.m_streamCount > INPUTSTREAM_IDS::MAX_STREAM_COUNT)
  {
    DisposeStreams();
    return;
  }

  for (unsigned int i = 0; i < streamIDs.m_streamCount; ++i)
  {
    INPUTSTREAM_INFO stream = m_struct.toAddon.get_stream(&m_struct, streamIDs.m_streamIds[i]);
    if (stream.m_streamType == INPUTSTREAM_INFO::TYPE_NONE)
      continue;

    std::string codecName(stream.m_codecName);
    StringUtils::ToLower(codecName);
    AVCodec *codec = avcodec_find_decoder_by_name(codecName.c_str());
    if (!codec)
      continue;

    CDemuxStream *demuxStream;

    if (stream.m_streamType == INPUTSTREAM_INFO::TYPE_AUDIO)
    {
      CDemuxStreamAudio *audioStream = new CDemuxStreamAudio();

      audioStream->iChannels = stream.m_Channels;
      audioStream->iSampleRate = stream.m_SampleRate;
      audioStream->iBlockAlign = stream.m_BlockAlign;
      audioStream->iBitRate = stream.m_BitRate;
      audioStream->iBitsPerSample = stream.m_BitsPerSample;
      demuxStream = audioStream;
    }
    else if (stream.m_streamType == INPUTSTREAM_INFO::TYPE_VIDEO)
    {
      CDemuxStreamVideo *videoStream = new CDemuxStreamVideo();

      videoStream->iFpsScale = stream.m_FpsScale;
      videoStream->iFpsRate = stream.m_FpsRate;
      videoStream->iWidth = stream.m_Width;
      videoStream->iHeight = stream.m_Height;
      videoStream->fAspect = stream.m_Aspect;
      videoStream->stereo_mode = "mono";
      videoStream->iBitRate = stream.m_BitRate;
      demuxStream = videoStream;
    }
    else if (stream.m_streamType == INPUTSTREAM_INFO::TYPE_SUBTITLE)
    {
      CDemuxStreamSubtitle *subtitleStream = new CDemuxStreamSubtitle();
      demuxStream = subtitleStream;
    }
    else
      continue;

    demuxStream->codec = codec->id;
    demuxStream->codecName = stream.m_codecInternalName;
    demuxStream->uniqueId = streamIDs.m_streamIds[i];
    demuxStream->language[0] = stream.m_language[0];
    demuxStream->language[1] = stream.m_language[1];
    demuxStream->language[2] = stream.m_language[2];
    demuxStream->language[3] = stream.m_language[3];

    if (stream.m_ExtraData && stream.m_ExtraSize)
    {
      demuxStream->ExtraData = new uint8_t[stream.m_ExtraSize];
      demuxStream->ExtraSize = stream.m_ExtraSize;
      for (unsigned int j = 0; j < stream.m_ExtraSize; ++j)
        demuxStream->ExtraData[j] = stream.m_ExtraData[j];
    }

    m_streams[demuxStream->uniqueId] = demuxStream;
  }
}

void CInputStreamAddon::DisposeStreams()
{
  for (auto &stream : m_streams)
    delete stream.second;
  m_streams.clear();
}

/*!
 * Callbacks from add-on to kodi
 */
//@{
DemuxPacket* CInputStreamAddon::cb_allocate_demux_packet(void* kodiInstance, int data_size)
{
  return CDVDDemuxUtils::AllocateDemuxPacket(data_size);
}

void CInputStreamAddon::cb_free_demux_packet(void* kodiInstance, DemuxPacket* packet)
{
  CDVDDemuxUtils::FreeDemuxPacket(packet);
}
//@}
