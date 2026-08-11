/*
 *      Copyright (C) 2014-2024 Jean-Luc Barriere
 *
 *  This library is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 3, or (at your option)
 *  any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this library; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301 USA
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "debug.h"

#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#if defined(_MSC_VER) && _MSC_VER < 1900
#define snprintf _snprintf
#endif


int NSROOT::_dbgLevel = DBG_NONE;

typedef struct
{
  const char* name;
  void (*msg_callback)(int level, char* msg);
} debug_ctx_t;

static debug_ctx_t debug_ctx = {LIBTAG, nullptr};

void NSROOT::_setDBGMsgCallback(void (*msgcb)(int level, char*))
{
  debug_ctx.msg_callback = msgcb;
}

/**
 * Generate a debug message at a given debug level
 * \param ctx the subsystem debug context to use
 * \param level the debug level of the debug message
 * \param fmt a printf style format string for the message
 * \param ... arguments to the format
 */
static inline void __dbg(debug_ctx_t* ctx, int level, const char* fmt, va_list ap)
{
  if (ctx != nullptr)
  {
    char msg[4096];
    int len = snprintf(msg, sizeof (msg), "(%s)", ctx->name);
    vsnprintf(msg + len, sizeof (msg) - len, fmt, ap);
    if (ctx->msg_callback)
    {
      ctx->msg_callback(level, msg);
    }
    else
    {
      fwrite(msg, strlen(msg), 1, stderr);
    }
  }
}

void NSROOT::_writeDBGMsg(int level, const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  __dbg(&debug_ctx, level, fmt, ap);
  va_end(ap);
}
