/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   class definition for the SSA/ASS subtitle parser

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#pragma once

#include "common/common_pch.h"

#include "merge/generic_reader.h"
#include "input/subtitles.h"

class ssa_reader_c: public generic_reader_c {
private:
  ssa_parser_cptr m_subs;
  boost::optional<std::string> m_encoding;
  int64_t m_bytes_to_process{}, m_bytes_processed{};

public:
  ssa_reader_c(const track_info_c &ti, const mm_io_cptr &in);
  virtual ~ssa_reader_c();

  virtual mtx::file_type_e get_format_type() const {
    return mtx::file_type_e::ssa;
  }

  virtual void read_headers();
  virtual void identify();
  virtual void create_packetizer(int64_t tid);
  virtual int64_t get_progress() override;
  virtual int64_t get_maximum_progress() override;
  virtual bool is_simple_subtitle_container() {
    return true;
  }

  static int probe_file(mm_text_io_c &in, uint64_t size);

protected:
  virtual file_status_e read(generic_packetizer_c *ptzr, bool force = false) override;
};
