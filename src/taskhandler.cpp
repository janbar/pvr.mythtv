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
    void *process();

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
  start_thread(false);
}

TaskHandlerPrivate::~TaskHandlerPrivate()
{
  Clear();
  Suspend();
  // last chance
  wait_thread(1000);
}

void TaskHandlerPrivate::ScheduleTask(Task *task, unsigned delayMs)
{
  Myth::OS::LockGuard lock(m_mutex);
  m_queue.push(std::make_pair(task, new Myth::OS::Timeout(delayMs)));
  m_queueContent.notify_one();
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
  if (is_stopped())
    return;
  stop_thread(false);
  m_queueContent.notify_one();
}

bool TaskHandlerPrivate::Resume()
{
  if (!is_stopped())
    return true;
  // wait until stopped
  if (is_running() && !wait_thread(5000))
    return false;
  // wait until running
  return start_thread(true);
}


void *TaskHandlerPrivate::process()
{
  while (!is_stopped())
  {
    Myth::OS::Timeout later;
    unsigned left = 0;

    m_mutex.lock();

    // refill all delayed in queue
    for (std::vector<Scheduled>::const_iterator it = m_delayed.begin(); it != m_delayed.end(); ++it)
      m_queue.push(*it);
    m_delayed.clear();

    while (!m_queue.empty() && !is_stopped())
    {
      Scheduled& item = m_queue.front();
      m_queue.pop();
      // delay the job else process it
      if ((left = item.second->time_left()) > 0)
      {
        m_delayed.push_back(item);
        m_mutex.unlock();
        if (!later.is_set() || later.time_left() > left)
          later.set(left);
      }
      else
      {
        m_mutex.unlock();
        item.first->Execute();
        delete item.second;
        delete item.first;
      }
      m_mutex.lock();
    }

    m_mutex.unlock();

    if (is_stopped())
      break;

    if (!later.is_set())
      m_queueContent.wait();
    else if ((left = later.time_left()) > 0)
      m_queueContent.wait_for(left);
  }
  return NULL;
}
