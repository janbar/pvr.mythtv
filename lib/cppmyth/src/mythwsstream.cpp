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

#include "mythwsstream.h"
#include "private/wsresponse.h"
#include "private/cppdef.h"

using namespace Myth;

WSStream::WSStream()
: m_response(NULL)
, m_position(0)
{
}

WSStream::WSStream(WSResponse *response)
: m_response(response)
, m_position(0)
{
}

WSStream::~WSStream()
{
  SAFE_DELETE(m_response);
}

int WSStream::Read(void* buffer, unsigned n)
{
  if (m_response == NULL)
    return 0;
  size_t s = m_response->ReadContent((char *)buffer, n);
  m_position += s;
  return (int)s;
}

int64_t WSStream::GetSize() const
{
  return (m_response != NULL ? (int64_t)(-1) : 0);
}

int64_t WSStream::GetPosition() const
{
  return (m_response != NULL ? m_position : 0);
}

int64_t WSStream::Seek(int64_t offset, WHENCE_t whence)
{
  (void)offset;
  (void)whence;
  return GetPosition();
}
