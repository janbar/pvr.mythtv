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

#include "mythprotoplayback.h"
#include "../private/debug.h"
#include "../private/socket.h"
#include "../private/os/threads/latch.h"
#include "../private/builtin.h"

#include <limits>
#include <cstdio>

#ifdef __WINDOWS__
#include <Ws2tcpip.h>
typedef unsigned long nfds_t;
#define poll(fds, nfds, timeout) WSAPoll(fds, nfds, timeout)
#else
#include <sys/socket.h> // for recv
#include <poll.h>       // for poll
#endif /* __WINDOWS__ */

using namespace Myth;

///////////////////////////////////////////////////////////////////////////////
////
//// Protocol connection to control playback
////

ProtoPlayback::ProtoPlayback(const std::string& server, unsigned port)
: ProtoBase(server, port)
{
}

bool ProtoPlayback::Open()
{
  bool ok = false;

  if (!OpenConnection(PROTO_PLAYBACK_RCVBUF))
    return false;

  if (m_protoVersion >= 75)
    ok = Announce75();

  if (ok)
    return true;
  Close();
  return false;
}

void ProtoPlayback::Close()
{
  ProtoBase::Close();
  // Clean hanging and disable retry
  m_tainted = m_hang = false;
}

bool ProtoPlayback::IsOpen()
{
  // Try reconnect
  if (m_hang)
    return ProtoPlayback::Open();
  return ProtoBase::IsOpen();
}

bool ProtoPlayback::Announce75()
{
  OS::WriteLock lock(*m_latch);

  std::string cmd("ANN Playback ");
  cmd.append(m_socket->GetMyHostName()).append(" 0");
  if (!SendCommand(cmd.c_str()))
    return false;

  std::string field;
  if (!ReadField(field) || !IsMessageOK(field))
    goto out;
  return true;

out:
  FlushMessage();
  return false;
}

void ProtoPlayback::TransferDone75(ProtoTransfer& transfer)
{
  BUILTIN_BUFFER buf;

  OS::WriteLock lock(*m_latch);
  if (!transfer.IsOpen())
    return;
  std::string cmd("QUERY_FILETRANSFER ");
  uint32_to_string(transfer.GetFileId(), &buf);
  cmd.append(buf.data).append(PROTO_STR_SEPARATOR).append("DONE");
  if (SendCommand(cmd.c_str()))
  {
    std::string field;
    if (!ReadField(field) || !IsMessageOK(field))
      FlushMessage();
  }
}

bool ProtoPlayback::TransferIsOpen75(ProtoTransfer& transfer)
{
  BUILTIN_BUFFER buf;
  std::string field;
  int8_t status = 0;

  OS::WriteLock lock(*m_latch);
  if (!IsOpen())
    return false;
  std::string cmd("QUERY_FILETRANSFER ");
  uint32_to_string(transfer.GetFileId(), &buf);
  cmd.append(buf.data);
  cmd.append(PROTO_STR_SEPARATOR);
  cmd.append("IS_OPEN");

  if (!SendCommand(cmd.c_str()))
    return false;
  if (!ReadField(field) || 0 != string_to_int8(field.c_str(), &status))
  {
      FlushMessage();
      return false;
  }
  if (status == 0)
    return false;
  return true;
}

int ProtoPlayback::TransferRequestBlock(ProtoTransfer& transfer, void *buffer, unsigned n)
{
  bool ok = true;
  bool request = false;
  bool data = false;
  char *p = (char*)buffer;
  unsigned s = 0;
  struct pollfd fds[2];

  int64_t filePosition = transfer.GetPosition();
  int64_t fileRequest = transfer.GetRequested();

  if (n == 0)
    return n;

  // read tranfer socket
  fds[0].fd = transfer.GetSocket();
  fds[0].events = POLLIN;
  // read control socket
  fds[1].fd = GetSocket();
  fds[1].events = POLLIN;
  if (INVALID_SOCKET_VALUE == (net_socket_t)fds[0].fd)
    return -1;
  if (INVALID_SOCKET_VALUE == (net_socket_t)fds[1].fd)
    return -1;

  // Max size is RCVBUF size
  if (n > PROTO_TRANSFER_RCVBUF)
    n = PROTO_TRANSFER_RCVBUF;
  if ((filePosition + n) > fileRequest)
  {
    // Begin critical section
    m_latch->lock();
    request = true;
    ok = TransferRequestBlock75(transfer, n);
  }

  // While request is not completed the latch remains locked, therefore
  // the loop must not break while the flags ok and request are true
  while (ok && (request || data || s == 0))
  {
    int nfds;
    int tv;

    // if a request is sent, then listen the control socket too
    if (request)
      nfds = 2;
    else
      nfds = 1;

    // Read directly to get all queued packets without waiting,
    // else wait for read incoming packet
    if (data)
      tv = 0;
    else
      tv = 10000;

    int r = poll(fds, nfds, tv);
    if (r < 0)
    {
      DBG(DBG_ERROR, "%s: poll error\n", __FUNCTION__);
      ok = false;
    }
    else if (r == 0)
    {
      if (data)
      {
        // Clear the flag data, so now it will break, or continue with
        // timeout while the request is not completed
        data = false;
      }
      else
      {
        // Timeout expired
        DBG(DBG_ERROR, "%s: poll timeout\n", __FUNCTION__);
        ok = false;
      }
    }
    else
    {
      // Clear flag data for this new attempt
      data = false;

      // Check for data
      if ((fds[0].revents & POLLIN))
      {
        int rr = (int) recv(fds[0].fd, p, (size_t)(n - s), 0);
        if (rr < 0)
        {
          DBG(DBG_ERROR, "%s: recv data error (%d)\n", __FUNCTION__, rr);
          ok = false;
        }
        else if (rr > 0)
        {
          s += rr;
          p += rr;
          filePosition += rr;
          transfer.SetPosition(filePosition);
          // If the buffer is full, clear the events to stop the polling
          // Otherwise, try again immediately
          if (s == n)
            fds[0].events = 0;
          else
            data = true;
        }
      }
      else if ((fds[0].revents & (POLLHUP | POLLERR |POLLNVAL)))
      {
        DBG(DBG_ERROR, "%s: transfer socket error (%d)\n", __FUNCTION__, fds[0].revents);
        ok = false;
      }

      // Check for response of request
      if (request)
      {
        if ((fds[1].revents & POLLIN))
        {
          int32_t rlen = TransferRequestBlockFeedback75();
          request = false; // request is completed
          m_latch->unlock();
          if (rlen < 0)
            ok = false;
          else
          {
            DBG(DBG_DEBUG, "%s: receive block size (%u)\n", __FUNCTION__, (unsigned)rlen);
            if (rlen == 0 && !data)
              break; // no more data
            fileRequest += rlen;
            transfer.SetRequested(fileRequest);
          }
        }
        else if ((fds[1].revents & (POLLHUP | POLLERR |POLLNVAL)))
        {
          DBG(DBG_ERROR, "%s: control socket error (%d)\n", __FUNCTION__, fds[1].revents);
          ok = false;
        }
      }
    }
  }

  if (ok)
  {
    DBG(DBG_DEBUG, "%s: data read (%u)\n", __FUNCTION__, s);
    return (int)s;
  }

  if (request)
  {
    if (RcvMessageLength())
      FlushMessage();
    m_latch->unlock();
  }
  // Recover the file position or die
  if (TransferSeek(transfer, filePosition, WHENCE_SET) < 0)
    HangException();
  return -1;
}

bool ProtoPlayback::TransferRequestBlock75(ProtoTransfer& transfer, unsigned n)
{
  // Note: Caller has to hold mutex until feedback or cancel point
  BUILTIN_BUFFER buf;

  if (!transfer.IsOpen())
    return false;
  std::string cmd("QUERY_FILETRANSFER ");
  uint32_to_string(transfer.GetFileId(), &buf);
  cmd.append(buf.data);
  cmd.append(PROTO_STR_SEPARATOR);
  cmd.append("REQUEST_BLOCK");
  cmd.append(PROTO_STR_SEPARATOR);
  uint32_to_string(n, &buf);
  cmd.append(buf.data);

  // No wait for feedback
  if (!SendCommand(cmd.c_str(), false))
    return false;
  return true;
}

int32_t ProtoPlayback::TransferRequestBlockFeedback75()
{
  int32_t rlen = 0;
  std::string field;
  if (!RcvMessageLength() || !ReadField(field) || 0 != string_to_int32(field.c_str(), &rlen) || rlen < 0)
  {
    DBG(DBG_ERROR, "%s: invalid response for request block (%s)\n", __FUNCTION__, field.c_str());
    FlushMessage();
    return -1;
  }
  return rlen;
}

int64_t ProtoPlayback::TransferSeek75(ProtoTransfer& transfer, int64_t offset, WHENCE_t whence)
{
  BUILTIN_BUFFER buf;
  int64_t position = 0;
  std::string field;

  int64_t filePosition = transfer.GetPosition();
  int64_t fileSize = transfer.GetSize();

  // Check offset
  switch (whence)
  {
    case WHENCE_CUR:
      if (offset == 0)
        return filePosition;
      position = filePosition + offset;
      if (position < 0 || position > fileSize)
        return -1;
      break;
    case WHENCE_SET:
      if (offset == filePosition)
        return filePosition;
      if (offset < 0 || offset > fileSize)
        return -1;
      break;
    case WHENCE_END:
      position = fileSize - offset;
      if (position < 0 || position > fileSize)
        return -1;
      break;
    default:
      return -1;
  }

  OS::WriteLock lock(*m_latch);
  if (!transfer.IsOpen())
    return -1;
  std::string cmd("QUERY_FILETRANSFER ");
  uint32_to_string(transfer.GetFileId(), &buf);
  cmd.append(buf.data);
  cmd.append(PROTO_STR_SEPARATOR);
  cmd.append("SEEK");
  cmd.append(PROTO_STR_SEPARATOR);
  int64_to_string(offset, &buf);
  cmd.append(buf.data);
  cmd.append(PROTO_STR_SEPARATOR);
  int8_to_string(whence, &buf);
  cmd.append(buf.data);
  cmd.append(PROTO_STR_SEPARATOR);
  int64_to_string(filePosition, &buf);
  cmd.append(buf.data);

  if (!SendCommand(cmd.c_str()))
    return -1;
  if (!ReadField(field) || 0 != string_to_int64(field.c_str(), &position))
  {
      FlushMessage();
      return -1;
  }
  // Reset transfer
  transfer.Flush();
  transfer.SetRequested(position);
  transfer.SetPosition(position);
  return position;
}
