#pragma once
/*
 *  Copyright (C) 2018-2021 Jean-Luc Barrière
 *  Copyright (C) 2018 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include <kodi/AddonBase.h>
#include "private/os/threads/mutex.h"
#include "private/os/threads/condition.h"

#include <queue>

template<typename ENTRY>
class ATTR_DLL_LOCAL FIFO
{
public:
  explicit FIFO(unsigned capacity)
  : m_capacity(capacity)
  , m_filled(false)
  {
  }

  virtual ~FIFO() { clear(); }

  bool push(ENTRY entry)
  {
    Myth::OS::LockGuard guard(m_lock);
    if (m_c.size() < m_capacity)
    {
      m_c.push(entry);
      m_filled = true;
      m_condition.Signal();
      return true;
    }
    return false;
  }

  bool pop(ENTRY& entry, unsigned timeout = 0)
  {
    Myth::OS::LockGuard guard(m_lock);
    if (!m_filled)
    {
      if (timeout == 0)
        return false;
      if (!m_condition.Wait(m_lock, m_filled, timeout))
        return false;
    }
    entry = m_c.front();
    m_c.pop();
    m_filled = !m_c.empty();
    return true;
  }

  void clear()
  {
    Myth::OS::LockGuard guard(m_lock);
    while (!empty())
      m_c.pop();
    m_condition.Broadcast();
  }

  size_t size(void)
  {
    Myth::OS::LockGuard guard(m_lock);
    return m_c.size();
  }

  bool empty(void)
  {
    Myth::OS::LockGuard guard(m_lock);
    return m_c.empty();
  }

private:
  unsigned                    m_capacity;
  bool                        m_filled;
  std::queue<ENTRY>           m_c;
  Myth::OS::Mutex            m_lock;
  Myth::OS::Condition<bool>  m_condition;
};
