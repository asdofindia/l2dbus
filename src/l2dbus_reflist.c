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
 * @file           l2dbus_reflist.c
 * @author         Glenn Schmottlach
 * @brief          Implementation of an object reference list.
 *===========================================================================
 */

#include "lauxlib.h"
#include "l2dbus_reflist.h"
#include "l2dbus_alloc.h"


void
l2dbus_refListInit
    (
    l2dbus_RefList* refList
    )
{
    if ( NULL != refList )
    {
        LIST_INIT(&refList->list);
    }
}


void
l2dbus_refListFree
    (
    l2dbus_RefList*     refList,
    lua_State*          L,
    l2dbus_FreeItemFunc func,
    void*               userdata
    )
{
    l2dbus_RefItem* item;
    l2dbus_RefItem* nextItem;
    void* itemUd;

    if ( (NULL != L) &&  (NULL != refList) )
    {
        for ( item = LIST_FIRST(&refList->list);
            item != LIST_END(&refList->list);
            item = nextItem )
        {
            nextItem = LIST_NEXT(item, link);
            if ( NULL != func )
            {
                lua_rawgeti(L, LUA_REGISTRYINDEX, item->refIdx);
                itemUd = lua_touserdata(L, -1);
                lua_pop(L, 1);
                func(itemUd, userdata);
            }
            luaL_unref(L, LUA_REGISTRYINDEX, item->refIdx);
            l2dbus_free(item);
        }
    }
}


int
l2dbus_refListRef
    (
    l2dbus_RefList* refList,
    lua_State*      L,
    int             idx
    )
{
    int ref = LUA_NOREF;
    l2dbus_RefItem* item;

    if ( NULL != refList )
    {
        item = l2dbus_malloc(sizeof(*item));
        if ( NULL != item )
        {
            lua_pushvalue(L, idx);
            item->refIdx = luaL_ref(L, LUA_REGISTRYINDEX);
            ref = item->refIdx;
            LIST_INSERT_HEAD(&refList->list, item, link);
        }
    }
    return ref;
}


l2dbus_Bool
l2dbus_refListUnref
    (
    l2dbus_RefList* refList,
    lua_State*      L,
    int             ref
    )
{
    l2dbus_Bool status = L2DBUS_FALSE;
    l2dbus_RefItem* item;
    l2dbus_RefItem* nextItem;

    if ( (NULL != L) &&  (NULL != refList) )
    {
        for ( item = LIST_FIRST(&refList->list);
            item != LIST_END(&refList->list);
            item = nextItem )
        {
            nextItem = LIST_NEXT(item, link);
            if ( ref == item->refIdx )
            {
                luaL_unref(L, LUA_REGISTRYINDEX, item->refIdx);
                LIST_REMOVE(item, link);
                l2dbus_free(item);
                status = L2DBUS_FALSE;
                break;
            }
        }
    }

    return status;
}


void
l2dbus_refListIterInit
    (
    l2dbus_RefList*     refList,
    l2dbus_RefListIter* iter
    )
{
    if ( (NULL != refList) && (NULL != iter) )
    {
        iter->cur = LIST_FIRST(&refList->list);
        iter->next = LIST_NEXT(iter->cur, link);
    }
}


int
l2dbus_refListIterCurrent
    (
    l2dbus_RefList*     refList,
    l2dbus_RefListIter* iter
    )
{
    int ref = LUA_NOREF;
    if ( (NULL != refList) && (NULL != iter) )
    {
        if ( iter->cur != LIST_END(&refList->list) )
        {
            ref = iter->cur->refIdx;
        }
    }

    return ref;
}


l2dbus_Bool
l2dbus_refListIterNext
    (
    l2dbus_RefList*     refList,
    l2dbus_RefListIter* iter
    )
{
    l2dbus_Bool hasNext = L2DBUS_FALSE;

    if ( (NULL != refList) && (NULL != iter) )
    {
        iter->cur = iter->next;
        if ( iter->cur != LIST_END(&refList->list) )
        {
            hasNext = L2DBUS_TRUE;
            iter->next = LIST_NEXT(iter->next, link);
        }
    }

    return hasNext;
}


void
l2dbus_refListIterErase
    (
    l2dbus_RefList*     refList,
    lua_State*          L,
    l2dbus_RefListIter* iter
    )
{
    if ( (NULL != refList) && (NULL != iter) && (NULL != L) )
    {
        if ( iter->cur != LIST_END(&refList->list) )
        {
            LIST_REMOVE(iter->cur, link);
            luaL_unref(L, LUA_REGISTRYINDEX, iter->cur->refIdx);
            l2dbus_free(iter->cur);
            l2dbus_refListIterNext(refList, iter);
        }
    }
}


void*
l2dbus_refListIterRefItem
    (
    l2dbus_RefList*     refList,
    lua_State*          L,
    l2dbus_RefListIter* iter
    )
{
    void* item = NULL;

    if ( (NULL != refList) && (NULL != iter) && (NULL != L) )
    {
        if ( iter->cur != LIST_END(&refList->list) )
        {
            lua_rawgeti(L, LUA_REGISTRYINDEX, iter->cur->refIdx);
            if ( lua_isuserdata(L, -1) )
            {
                item = lua_touserdata(L, -1);
            }
        }
    }

    /* This leaves the Lua userdata at the top of the stack or
     * nil if not found
     */
    return item;
}


l2dbus_Bool
l2dbus_refListFindItem
    (
    l2dbus_RefList*     refList,
    lua_State*          L,
    const void*         item,
    l2dbus_RefListIter* iter
    )
{
    l2dbus_Bool found = L2DBUS_FALSE;
    l2dbus_RefItem* cur;

    if ( (NULL != refList) && (NULL != iter) && (NULL != L) )
    {
        for ( cur = LIST_FIRST(&refList->list);
            (cur != LIST_END(&refList->list)) && !found;
            cur = LIST_NEXT(cur, link) )
        {
            lua_rawgeti(L, LUA_REGISTRYINDEX, cur->refIdx);
            if ( lua_isuserdata(L, -1) &&
                (item == lua_touserdata(L, -1)) )
            {
                iter->cur = cur;
                iter->next = LIST_NEXT(cur, link);
                found = L2DBUS_TRUE;
            }
            lua_pop(L, 1);
        }
    }

    return found;
}


