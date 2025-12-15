/*
 *      Copyright (C) 2015 Jean-Luc Barriere
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

#include "mythlocked.h"
#include "private/os/threads/latch.h"

using namespace Myth;

Lockable::Lockable()
: m_lock(new OS::Latch()) { }

Lockable::~Lockable()
{
  delete m_lock;
}

void Lockable::Lock()
{
  m_lock->lock();
}

void Lockable::Unlock()
{
  m_lock->unlock();
}

void Lockable::LockShared()
{
  m_lock->lock_shared();
}

void Lockable::UnlockShared()
{
  m_lock->unlock_shared();
}
