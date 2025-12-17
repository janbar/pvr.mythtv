/*
 *  Copyright (C) 2018-2021 Jean-Luc Barrière
 *  Copyright (C) 2018 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "taskhandler.h"
#include "private/os/threads/mutex.h"
#include "private/os/threads/timeout.h"
#include "private/os/threads/event.h"
#include "private/os/threads/thread.h"

class TaskHandlerPrivate : private Myth::OS::Thread
{
public:
  TaskHandlerPrivate();
  virtual ~TaskHandlerPrivate();

  void ScheduleTask(Task *task, unsigned delayMs = 0);
  void Clear();
  void Suspend();
  bool Resume();

protected:
    void *Process();

private:
  typedef std::pair<Task*, Myth::OS::Timeout*> Scheduled;
  std::queue<Scheduled> m_queue;
  std::vector<Scheduled> m_delayed;
  Myth::OS::Mutex m_mutex;
  Myth::OS::Event m_queueContent;
};

TaskHandler::TaskHandler()
: m_p(new TaskHandlerPrivate)
{
}

TaskHandler::~TaskHandler()
{
  delete m_p;
}

void TaskHandler::ScheduleTask(Task* task, unsigned delayMs)
{
  m_p->ScheduleTask(task, delayMs);
}

void TaskHandler::Clear()
{
  m_p->Clear();
}

void TaskHandler::Suspend()
{
  m_p->Suspend();
}

bool TaskHandler::Resume()
{
  return m_p->Resume();
}


TaskHandlerPrivate::TaskHandlerPrivate()
: Myth::OS::Thread()
{
  StartThread(false);
}

TaskHandlerPrivate::~TaskHandlerPrivate()
{
  Clear();
  Suspend();
  // last chance
  WaitThread(1000);
}

void TaskHandlerPrivate::ScheduleTask(Task *task, unsigned delayMs)
{
  Myth::OS::LockGuard lock(m_mutex);
  m_queue.push(std::make_pair(task, new Myth::OS::Timeout(delayMs)));
  m_queueContent.Signal();
}

void TaskHandlerPrivate::Clear()
{
  Myth::OS::LockGuard lock(m_mutex);
  for (std::vector<Scheduled>::const_iterator it = m_delayed.begin(); it != m_delayed.end(); ++it)
  {
    delete it->second;
    delete it->first;
  }
  m_delayed.clear();
  while (!m_queue.empty())
  {
    Scheduled& item = m_queue.front();
    delete item.second;
    delete item.first;
    m_queue.pop();
  }
}

void TaskHandlerPrivate::Suspend()
{
  if (IsStopped())
    return;
  StopThread(false);
  m_queueContent.Signal();
}

bool TaskHandlerPrivate::Resume()
{
  if (!IsStopped())
    return true;
  // wait until stopped
  if (IsRunning() && !WaitThread(5000))
    return false;
  // wait until running
  return StartThread(true);
}


void *TaskHandlerPrivate::Process()
{
  while (!IsStopped())
  {
    Myth::OS::Timeout later;
    unsigned left = 0;

    m_mutex.Lock();

    // refill all delayed in queue
    for (std::vector<Scheduled>::const_iterator it = m_delayed.begin(); it != m_delayed.end(); ++it)
      m_queue.push(*it);
    m_delayed.clear();

    while (!m_queue.empty() && !IsStopped())
    {
      Scheduled& item = m_queue.front();
      m_queue.pop();
      // delay the job else process it
      if ((left = item.second->TimeLeft()) > 0)
      {
        m_delayed.push_back(item);
        m_mutex.Unlock();
        if (!later.IsSet() || later.TimeLeft() > left)
          later.Set(left);
      }
      else
      {
        m_mutex.Unlock();
        item.first->Execute();
        delete item.second;
        delete item.first;
      }
      m_mutex.Lock();
    }

    m_mutex.Unlock();

    if (IsStopped())
      break;

    if (!later.IsSet())
      m_queueContent.Wait();
    else if ((left = later.TimeLeft()) > 0)
      m_queueContent.Wait(left);
  }
  return NULL;
}
