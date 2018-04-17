/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   AV1 video output module

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include "common/common_pch.h"

#include "common/av1.h"
#include "common/codec.h"
#include "merge/connection_checks.h"
#include "output/p_av1.h"

using namespace libmatroska;

av1_video_packetizer_c::av1_video_packetizer_c(generic_reader_c *p_reader,
                                               track_info_c &p_ti)
  : generic_packetizer_c{p_reader, p_ti}
{
  m_timestamp_factory_application_mode = TFA_SHORT_QUEUEING;

  set_track_type(track_video);
  set_codec_id(MKV_V_AV1);
}

int
av1_video_packetizer_c::process(packet_cptr packet) {
  packet->bref         = m_parser.is_keyframe(*packet->data) ? -1 : m_previous_timestamp;
  m_previous_timestamp = packet->timestamp;

  add_packet(packet);

  return FILE_STATUS_MOREDATA;
}

connection_result_e
av1_video_packetizer_c::can_connect_to(generic_packetizer_c *src,
                                       std::string &error_message) {
  auto psrc = dynamic_cast<av1_video_packetizer_c *>(src);
  if (!psrc)
    return CAN_CONNECT_NO_FORMAT;

  connect_check_v_width( m_hvideo_pixel_width,  psrc->m_hvideo_pixel_width);
  connect_check_v_height(m_hvideo_pixel_height, psrc->m_hvideo_pixel_height);

  return CAN_CONNECT_YES;
}

bool
av1_video_packetizer_c::is_compatible_with(output_compatibility_e compatibility) {
  return (OC_MATROSKA == compatibility) || (OC_WEBM == compatibility);
}