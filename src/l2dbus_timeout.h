/*===========================================================================
 * 
 * Project         l2dbus
 *
 * Released under the MIT License (MIT)
 * Copyright (c) 2013 XS-Embedded LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 * NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *===========================================================================
 *===========================================================================
 * @file           l2dbus_timeout.h        
 * @author         Glenn Schmottlach
 * @brief          Definition of a D-Bus timeout object.
 *===========================================================================
 */

#ifndef L2DBUS_TIMEOUT_H_
#define L2DBUS_TIMEOUT_H_

#include "lua.h"
#include "l2dbus_callback.h"

/* Forward declarations */
struct cdbus_Timeout;

typedef struct l2dbus_Timeout
{
    struct cdbus_Timeout*   timeout;
    int                     dispUdRef;
    l2dbus_CallbackCtx      cbCtx;
} l2dbus_Timeout;

int l2dbus_newTimeout(lua_State* L);
void l2dbus_openTimeout(lua_State* L);



#endif /* Guard for L2DBUS_TIMEOUT_H_ */
