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

#include "mythprototransfer.h"
#include "../private/debug.h"
#include "../private/socket.h"
#include "../private/os/threads/latch.h"
#include "../private/builtin.h"

#include <limits>
#include <cstdio>

using namespace Myth;

///////////////////////////////////////////////////////////////////////////////
////
//// Protocol connection to transfer file
////

ProtoTransfer::ProtoTransfer(const std::string& server, unsigned port, const std::string& pathname, const std::string& sgname)
: ProtoBase(server, port)
, m_fileSize(0)
, m_filePosition(0)
, m_fileRequest(0)
, m_fileId(0)
, m_pathName(pathname)
, m_storageGroupName(sgname)
{
}

bool ProtoTransfer::Open()
{
  bool ok = false;

  if (IsOpen())
    return true;
  if (!OpenConnection(PROTO_TRANSFER_RCVBUF))
    return false;

  if (m_protoVersion >= 75)
    ok = Announce75();

  if (!ok)
  {
    m_hang = true; // set hang to close without notice
    Close();
    return false;
  }
  return true;
}

void ProtoTransfer::Close()
{
  OS::WriteLock lock(*m_latch);
  ProtoBase::Close();
  // Clean hanging and disable retry
  m_tainted = m_hang = false;
  // Reset transfer
  m_filePosition = m_fileRequest = 0;
  m_fileId = 0;
}

void ProtoTransfer::Lock()
{
  m_latch->lock();
}

void ProtoTransfer::Unlock()
{
  m_latch->unlock();
}

void ProtoTransfer::Flush()
{
  OS::WriteLock lock(*m_latch);
  int64_t unread = m_fileRequest - m_filePosition;
  if (unread > 0)
  {
    char buf[PROTO_BUFFER_SIZE];
    size_t n = (size_t)unread;
    while (n > 0)
    {
      size_t s = (n > PROTO_BUFFER_SIZE ? PROTO_BUFFER_SIZE : n);
      if(m_socket->ReceiveData(buf, s) != s)
        break;
      n -= s;
    }
    DBG(DBG_DEBUG, "%s: unreaded bytes (%u)\n", __FUNCTION__, (unsigned)n);
    // Reset position regardless bytes read
    m_filePosition = m_fileRequest;
  }
}

bool ProtoTransfer::Announce75()
{
  OS::WriteLock lock(*m_latch);
  m_filePosition = m_fileSize = m_fileRequest = 0;
  std::string cmd("ANN FileTransfer ");
  cmd.append(m_socket->GetMyHostName());
  cmd.append(" 0 0 1000" PROTO_STR_SEPARATOR);
  cmd.append(m_pathName).append(PROTO_STR_SEPARATOR);
  cmd.append(m_storageGroupName);
  if (!SendCommand(cmd.c_str()))
    return false;

  std::string field;
  if (!ReadField(field) || !IsMessageOK(field))
    goto out;
  if (!ReadField(field) || 0 != string_to_uint32(field.c_str(), &m_fileId))
    goto out;
  if (!ReadField(field) || 0 != string_to_int64(field.c_str(), &m_fileSize))
    goto out;
  return true;

out:
  FlushMessage();
  return false;
}

uint32_t ProtoTransfer::GetFileId() const
{
  return m_fileId;
}

std::string ProtoTransfer::GetPathName() const
{
  return m_pathName;
}

std::string ProtoTransfer::GetStorageGroupName() const
{
  return m_storageGroupName;
}

int64_t ProtoTransfer::GetSize() const
{
  OS::ReadLock lock(*m_latch);
  return m_fileSize;
}

int64_t ProtoTransfer::GetPosition() const
{
  OS::ReadLock lock(*m_latch);
  return m_filePosition;
}

int64_t ProtoTransfer::GetRequested() const
{
  OS::ReadLock lock(*m_latch);
  return m_fileRequest;
}

int64_t ProtoTransfer::GetRemaining() const
{
  OS::ReadLock lock(*m_latch);
  return (m_fileSize - m_filePosition);
}

void ProtoTransfer::SetSize(int64_t size)
{
  OS::WriteLock lock(*m_latch);
  m_fileSize = size;
}

void ProtoTransfer::SetPosition(int64_t position)
{
  OS::WriteLock lock(*m_latch);
  m_filePosition = position;
}

void ProtoTransfer::SetRequested(int64_t requested)
{
  OS::WriteLock lock(*m_latch);
  m_fileRequest = requested;
}
