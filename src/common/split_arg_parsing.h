/** \brief command line parsing

   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   \author Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#pragma once

#include "common/common_pch.h"

#include "common/split_point.h"

namespace mtx { namespace args {

class format_x: public exception {
protected:
  std::string m_message;
public:
  explicit format_x(std::string const &message) : m_message{message} { }
  virtual ~format_x() throw() { }

  virtual char const *what() const throw() {
    return m_message.c_str();
  }
};

std::vector<split_point_c> parse_split_parts(const std::string &arg, bool frames_fields);

}}
