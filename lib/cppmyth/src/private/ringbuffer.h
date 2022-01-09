/*
 *      Copyright (C) 2022 Jean-Luc Barriere
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

#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <cppmyth_config.h>

#include <cstring>
#include <cassert>
#include <vector>
#include <list>

namespace NSROOT
{

class RingBufferPacket
{
public:
  RingBufferPacket(int _capacity);
  ~RingBufferPacket();
  unsigned id;
  int size;
  char * const data;
  const int capacity;
private:
  // prevent copy
  RingBufferPacket(const RingBufferPacket& other);
  RingBufferPacket& operator=(const RingBufferPacket& other);
};

class RingBuffer
{
public:
  RingBuffer(int capacity);
  virtual ~RingBuffer();

  int capacity() const;

  int bytesAvailable() const;

  unsigned bytesUnread() const;

  /**
   * When the buffer is full, writing new data will overwrite the next
   * available chunk for reading.
   * @return true if the buffer is full
   */
  bool full() const;

  void clear();

  int write(const char * data, int len);
  RingBufferPacket * newPacket(int len);
  void writePacket(RingBufferPacket * packet);

  /**
   * Returned pointer MUST BE freed by caller
   * @see freePacket(FramePacket *)
   * @return new FramePacket or nullptr
   */
  RingBufferPacket * read();

  void freePacket(RingBufferPacket * p);

private:
  // Prevent copy
  RingBuffer(const RingBuffer& other);
  RingBuffer& operator=(const RingBuffer& other);

private:
  struct Lockable;
  mutable Lockable * m_lock;
  const int m_capacity;           /// buffer size
  volatile unsigned m_count;      /// total count of processed chunk
  volatile unsigned m_unread;     /// total size of unread data in the buffer

  struct Chunk
  {
    Chunk() : packet(nullptr), next(nullptr) { }
    ~Chunk() { if (packet) delete packet; }
    RingBufferPacket * packet;
    Chunk * next;
  };

  std::vector<Chunk*> m_buffer;   /// buffer of chunk
  volatile Chunk * m_read;        /// chunk to read
  volatile Chunk * m_write;       /// chunk to write

  void init();

  std::list<RingBufferPacket*> m_pool;
  RingBufferPacket * needPacket(int size);
};

}

#endif /* RINGBUFFER_H */

