/*
  mkvmerge -- utility for splicing together matroska files
      from component media subtypes

  r_avi.h

  Written by Moritz Bunkus <moritz@bunkus.org>

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

/*!
    \file r_aac.h
    \version $Id$
    \brief class definitions for the AVI demultiplexer module
    \author Moritz Bunkus <moritz@bunkus.org>
*/

#ifndef __R_AAC_H
#define __R_AAC_H

#include "os.h"

#include <stdio.h>

#include "aac_common.h"
#include "common.h"
#include "error.h"
#include "mm_io.h"
#include "pr_generic.h"

class aac_reader_c: public generic_reader_c {
private:
  unsigned char *chunk;
  mm_io_c *mm_io;
  int64_t bytes_processed, size;
  bool emphasis_present;
  aac_header_t aacheader;

public:
  aac_reader_c(track_info_c *nti) throw (error_c);
  virtual ~aac_reader_c();

  virtual int read(generic_packetizer_c *ptzr);
  virtual int display_priority();
  virtual void display_progress(bool final = false);
  virtual void identify();
  virtual void create_packetizer(int64_t id);

  static int probe_file(mm_io_c *mm_io, int64_t size);

protected:
  virtual void guess_adts_version();
};

#endif // __R_AAC_H
