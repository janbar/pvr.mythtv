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

#ifndef MYTHLOCKED_H
#define	MYTHLOCKED_H

namespace Myth
{

  namespace OS {
    class Latch;
  }

  class Lockable
  {
  public:
    Lockable();
    ~Lockable();

    /**
     * Blocks until exclusive lock is held.
     */
    void Lock();
    /**
     * Release exclusive lock.
     */
    void Unlock();

    /**
     * Blocks until shared lock is held.
     */
    void LockShared();
    /**
     * Release shared lock.
     */
    void UnlockShared();

#if __cplusplus >= 201103L
    Lockable(const Lockable&) = delete;
    Lockable& operator=(const Lockable&) = delete;
#endif

    class ExclusiveGuard
    {
      Lockable& m_lock;
#if __cplusplus < 201103L
      ExclusiveGuard(const ExclusiveGuard&);
      ExclusiveGuard& operator=(const ExclusiveGuard&);
#endif
    public:
      ExclusiveGuard(Lockable& lock) : m_lock(lock) { m_lock.Lock(); }
      ~ExclusiveGuard() { m_lock.Unlock(); }
#if __cplusplus >= 201103L
      ExclusiveGuard(const ExclusiveGuard&) = delete;
      ExclusiveGuard& operator=(const ExclusiveGuard&) = delete;
#endif
    };

    class SharedGuard
    {
      Lockable& m_lock;
#if __cplusplus < 201103L
      SharedGuard(const SharedGuard&);
      SharedGuard& operator=(const SharedGuard&);
#endif
    public:
      SharedGuard(Lockable& lock) : m_lock(lock) { m_lock.LockShared(); }
      ~SharedGuard() { m_lock.UnlockShared(); }
#if __cplusplus >= 201103L
      SharedGuard(const SharedGuard&) = delete;
      SharedGuard& operator=(const SharedGuard&) = delete;
#endif
    };

  private:
    OS::Latch* m_lock;

#if __cplusplus < 201103L
    Lockable(const Lockable&);
    Lockable& operator=(const Lockable&);
#endif
  };

  template<typename T>
  class Locked
  {
  public:
    Locked()
    : m_val(), m_latch() {}

    Locked(const T& val)
    : m_val(val), m_latch() {}

    ~Locked() {}

    T Load()
    {
      Lockable::SharedGuard g(m_latch);
      return m_val; // return copy
    }

    const T& Store(const T& newval)
    {
      Lockable::ExclusiveGuard g(m_latch);
      m_val = newval;
      return newval; // return input
    }

    class pointer
    {
      friend class Locked;
    public:
      T& operator* () const { return *m_val; }
      T *operator->() const { return m_val; }

      pointer() : m_val(nullptr), m_x(nullptr) { }
      ~pointer() { if (m_x) m_x->Unlock(); }

      pointer(const pointer& other)
      : m_val(other.m_val), m_x(other.m_x) { m_x->Lock(); }

      pointer& operator=(const pointer& other)
      {
        if (m_x) m_x->Unlock();
        m_val = other.m_val;
        m_x = other.m_x;
        m_x->Lock();
        return *this;
      }

#if __cplusplus >= 201103L
      pointer(pointer&& other) noexcept
      : m_val(other.m_val), m_x(other.m_x)
      {
        other.m_val = nullptr;
        other.m_x = nullptr;
      }

      pointer& operator=(pointer&& other) noexcept
      {
        if (m_x) m_x->Unlock();
        m_val = other.m_val;
        m_x = other.m_x;
        other.m_val = nullptr;
        other.m_x = nullptr;
        return *this;
      }
#endif

    private:
      pointer(T* val, Lockable* latch)
      : m_val(val), m_x(latch) { m_x->Lock(); }
      T* m_val;
      Lockable* m_x;
    };

    pointer GetExclusive()
    {
      return pointer(&m_val, &m_latch);
    }

    class const_pointer
    {
      friend class Locked;
    public:
      const T& operator* () const { return *m_val; }
      const T *operator->() const { return m_val; }

      const_pointer() : m_val(nullptr), m_s(nullptr) { }
      ~const_pointer() { if (m_s) m_s->UnlockShared(); }

      const_pointer(const const_pointer& other)
      : m_val(other.m_val), m_s(other.m_s) { m_s->LockShared(); }

      const_pointer& operator=(const const_pointer& other)
      {
        if (m_s) m_s->UnlockShared();
        m_val = other.m_val;
        m_s = other.m_s;
        m_s->LockShared();
        return *this;
      }

#if __cplusplus >= 201103L
      const_pointer(const_pointer&& other) noexcept
      : m_val(other.m_val), m_s(other.m_s)
      {
        other.m_val = nullptr;
        other.m_s = nullptr;
      }

      const_pointer& operator=(const_pointer&& other) noexcept
      {
        if (m_s) m_s->UnlockShared();
        m_val = other.m_val;
        m_s = other.m_s;
        other.m_val = nullptr;
        other.m_s = nullptr;
        return *this;
      }
#endif

    private:
      const_pointer(const T* val, Lockable* latch)
      : m_val(val), m_s(latch) { m_s->LockShared(); }
      const T* m_val;
      Lockable* m_s;
    };

    const_pointer GetShared()
    {
      return const_pointer(&m_val, &m_latch);
    }

#if __cplusplus >= 201103L
    // Prevent copy
    Locked(const Locked<T>& other) = delete;
    Locked<T>& operator=(const Locked<T>& other) = delete;
#endif

  protected:
    T m_val;
    Lockable m_latch;

#if __cplusplus < 201103L
    // Prevent copy
    Locked(const Locked<T>& other);
    Locked<T>& operator=(const Locked<T>& other);
#endif
  };

}

#endif	/* MYTHLOCKED_H */
