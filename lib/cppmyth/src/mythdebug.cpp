/*
 *      Copyright (C) 2026 Jean-Luc Barriere
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

#include "mythdebug.h"
#include "private/debug.h"

void Myth::DBGLevel(int l)
{
  Myth::_dbgLevel = l;
}

void Myth::DBGAll(void)
{
  Myth::_dbgLevel = DBG_ALL;
}

void Myth::DBGNone(void)
{
  Myth::_dbgLevel = DBG_NONE;
}

void Myth::SetDBGMsgCallback(void (*msgcb)(int level, char*))
{
  Myth::_setDBGMsgCallback(msgcb);
}
