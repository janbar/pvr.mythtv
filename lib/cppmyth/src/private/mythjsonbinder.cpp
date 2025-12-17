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

#include "mythjsonbinder.h"
#include "builtin.h"
#include "debug.h"

#include <cstdlib>  // for atof
#include <cstring>  // for strcmp
#include <cstdio>
#include <errno.h>

using namespace Myth;

void JSON::BindObject(const Node& node, void *obj, const bindings_t *bl)
{
  int i, err;

  if (bl == nullptr || node.IsNull())
    return;

  for (i = 0; i < bl->attr_count; ++i)
  {
    err = 0;
    const Node& field = node.GetObjectValue(bl->attr_bind[i].field);
    if (field.IsNull())
      continue;
    if (field.IsString())
    {
      std::string value(field.GetStringValue());
      switch (bl->attr_bind[i].type)
      {
        case IS_STRING:
          bl->attr_bind[i].set(obj, value.c_str());
          break;
        case IS_INT8:
        {
          int8_t num = 0;
          err = string_to_int8(value.c_str(), &num);
          bl->attr_bind[i].set(obj, &num);
          break;
        }
        case IS_INT16:
        {
          int16_t num = 0;
          err = string_to_int16(value.c_str(), &num);
          bl->attr_bind[i].set(obj, &num);
          break;
        }
        case IS_INT32:
        {
          int32_t num = 0;
          err = string_to_int32(value.c_str(), &num);
          bl->attr_bind[i].set(obj, &num);
          break;
        }
        case IS_INT64:
        {
          int64_t num = 0;
          err = string_to_int64(value.c_str(), &num);
          bl->attr_bind[i].set(obj, &num);
          break;
        }
        case IS_UINT8:
        {
          uint8_t num = 0;
          err = string_to_uint8(value.c_str(), &num);
          bl->attr_bind[i].set(obj, &num);
          break;
        }
        case IS_UINT16:
        {
          uint16_t num = 0;
          err = string_to_uint16(value.c_str(), &num);
          bl->attr_bind[i].set(obj, &num);
          break;
        }
        case IS_UINT32:
        {
          uint32_t num = 0;
          err = string_to_uint32(value.c_str(), &num);
          bl->attr_bind[i].set(obj, &num);
          break;
        }
        case IS_DOUBLE:
        {
          double num = atof(value.c_str());
          bl->attr_bind[i].set(obj, &num);
          break;
        }
        case IS_BOOLEAN:
        {
          bool b = (strcmp(value.c_str(), "true") == 0 ? true : false);
          bl->attr_bind[i].set(obj, &b);
          break;
        }
        case IS_TIME:
        {
          time_t time = 0;
          err = string_to_time(value.c_str(), &time);
          bl->attr_bind[i].set(obj, &time);
          break;
        }
        default:
          break;
      }
    }
    else
    {
      switch (bl->attr_bind[i].type)
      {
        case IS_STRING:
        {
          if (field.IsString())
          {
            std::string value(field.GetStringValue());
            bl->attr_bind[i].set(obj, value.c_str());
          }
          else
            Myth::DBG(DBG_ERROR, "%s: invalid value for field \"%s\" type %d\n", __FUNCTION__, bl->attr_bind[i].field, bl->attr_bind[i].type);
          break;
        }
        case IS_INT8:
        {
          if (field.IsInt())
          {
            int8_t num = field.GetIntValue();
            bl->attr_bind[i].set(obj, &num);
          }
          else
            Myth::DBG(DBG_ERROR, "%s: invalid value for field \"%s\" type %d\n", __FUNCTION__, bl->attr_bind[i].field, bl->attr_bind[i].type);
          break;
        }
        case IS_INT16:
        {
          if (field.IsInt())
          {
            int16_t num = field.GetIntValue();
            bl->attr_bind[i].set(obj, &num);
          }
          else
            Myth::DBG(DBG_ERROR, "%s: invalid value for field \"%s\" type %d\n", __FUNCTION__, bl->attr_bind[i].field, bl->attr_bind[i].type);
          break;
        }
        case IS_INT32:
        {
          if (field.IsInt())
          {
            int32_t num = field.GetIntValue();
            bl->attr_bind[i].set(obj, &num);
          }
          else
            Myth::DBG(DBG_ERROR, "%s: invalid value for field \"%s\" type %d\n", __FUNCTION__, bl->attr_bind[i].field, bl->attr_bind[i].type);
          break;
        }
        case IS_INT64:
        {
          if (field.IsInt() || field.IsDouble())
          {
            int64_t num = field.GetBigIntValue();
            bl->attr_bind[i].set(obj, &num);
          }
          else
            Myth::DBG(DBG_ERROR, "%s: invalid value for field \"%s\" type %d\n", __FUNCTION__, bl->attr_bind[i].field, bl->attr_bind[i].type);
          break;
        }
        case IS_UINT8:
        {
          if (field.IsInt())
          {
            uint8_t num = field.GetIntValue();
            bl->attr_bind[i].set(obj, &num);
          }
          else
            Myth::DBG(DBG_ERROR, "%s: invalid value for field \"%s\" type %d\n", __FUNCTION__, bl->attr_bind[i].field, bl->attr_bind[i].type);
          break;
        }
        case IS_UINT16:
        {
          if (field.IsInt())
          {
            uint16_t num = field.GetIntValue();
            bl->attr_bind[i].set(obj, &num);
          }
          else
            Myth::DBG(DBG_ERROR, "%s: invalid value for field \"%s\" type %d\n", __FUNCTION__, bl->attr_bind[i].field, bl->attr_bind[i].type);
          break;
        }
        case IS_UINT32:
        {
          if (field.IsInt() || field.IsDouble())
          {
            uint32_t num = (uint32_t) field.GetBigIntValue();
            bl->attr_bind[i].set(obj, &num);
          }
          else
            Myth::DBG(DBG_ERROR, "%s: invalid value for field \"%s\" type %d\n", __FUNCTION__, bl->attr_bind[i].field, bl->attr_bind[i].type);
          break;
        }
        case IS_DOUBLE:
        {
          if (field.IsDouble() || field.IsInt())
          {
            double num = field.GetDoubleValue();
            bl->attr_bind[i].set(obj, &num);
          }
          else
            Myth::DBG(DBG_ERROR, "%s: invalid value for field \"%s\" type %d\n", __FUNCTION__, bl->attr_bind[i].field, bl->attr_bind[i].type);
          break;
        }
        case IS_BOOLEAN:
        {
          if (field.IsTrue() || field.IsFalse())
          {
            bool b = field.IsTrue();
            bl->attr_bind[i].set(obj, &b);
          }
          else
            Myth::DBG(DBG_ERROR, "%s: invalid value for field \"%s\" type %d\n", __FUNCTION__, bl->attr_bind[i].field, bl->attr_bind[i].type);
          break;
        }
        case IS_TIME:
        {
          if (field.IsString())
          {
            std::string value(field.GetStringValue());
            time_t time = 0;
            err = string_to_time(value.c_str(), &time);
            bl->attr_bind[i].set(obj, &time);
          }
          else
            Myth::DBG(DBG_ERROR, "%s: invalid value for field \"%s\" type %d\n", __FUNCTION__, bl->attr_bind[i].field, bl->attr_bind[i].type);
          break;
        }
        default:
          break;
      }
    }
    if (err)
      Myth::DBG(DBG_ERROR, "%s: failed (%d) field \"%s\" type %d\n", __FUNCTION__, err, bl->attr_bind[i].field, bl->attr_bind[i].type);
  }
}
