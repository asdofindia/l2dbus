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
 * @file           l2dbus_match.c
 * @author         Glenn Schmottlach
 * @brief          Implementation of D-Bus "match" object
 *===========================================================================
 */
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include "l2dbus_compat.h"
#include "l2dbus_match.h"
#include "l2dbus_connection.h"
#include "l2dbus_core.h"
#include "l2dbus_object.h"
#include "l2dbus_util.h"
#include "l2dbus_trace.h"
#include "l2dbus_debug.h"
#include "l2dbus_types.h"
#include "l2dbus_message.h"
#include "l2dbus_alloc.h"
#include "lualib.h"

/**
 L2DBUS Match

 This section defines the data structures associated with the
 representation of a match rule.

 @namespace l2dbus.Match
 */


/**
 The table that describes keys/fields for matching a message. Excluding
 a field indicates a wildcard match while including a field narrows
 the scope of a match (e.g. makes it more inclusive).

 @table MatchRule
 @field msgType (number) The type of the D-Bus message to match. Possible options
  include:
  <ul>
  <li>@{l2dbus.Dbus.MESSAGE_TYPE_METHOD_CALL|MESSAGE_TYPE_METHOD_CALL}</li>
  <li>@{l2dbus.Dbus.MESSAGE_TYPE_METHOD_RETURN|MESSAGE_TYPE_METHOD_RETURN}</li>
  <li>@{l2dbus.Dbus.MESSAGE_TYPE_ERROR|MESSAGE_TYPE_ERROR}</li>
  <li>@{l2dbus.Dbus.MESSAGE_TYPE_SIGNAL|MESSAGE_TYPE_SIGNAL}</li>
  </ul>
 @field member (string) Matches messages with a particular signal or member name
 @field objInterface (string) Matches messages sent over or to a particular
 object interface
 @field sender (string) Matches messages sent by a particular sender.
 @field path (string) Matches messages which are sent from or to the given
 object. This can be treated as an object path or namespace depending on
 how treatPathAsNamespace is configured.
 @field treatPathAsNamespace (bool) An optional value that determines how
 to interpret the path field. If **true** then the path is treated
 as a Java-like namespace. If **false** (the default) then treat *path* as
 an object path.
 @field arg0Namespace (string) Matches messages whose first argument is a string
 and is a bus name or interface name within the specified
 namespace. This is primarily intended for watching name owner changes
 for a group of related bus names, rather than for signal name or all
 name changes.
 @field eavesdrop (bool) Since D-Bus 1.5.6, match rules do not match
 messages which have a DESTINATION field unless the match rule
 specifically requests this by specifying eavesdrop as **true** in the match
 rule. Setting eavesdrop as **false** restores the default behavior.
 Messages are delivered to their DESTINATION regardless of match rules,
 so this match does not affect normal delivery of unicast messages. If
 the message bus has a security policy which forbids eavesdropping, this
 match may still be used without error, but will not have any practical
 effect. In older versions of D-Bus, this match was not allowed in match
 rules, and all match rules behaved as if eavesdrop equals **true** had
 been used.
 @field filterArgs (array) A Lua array of arg*N* @{FilterArgs|filter arguments}.
 */

/**
 The table that describes matches on the *N*'th argument of the body
 of a message. Only arguments of D-Bus type **string** or **object path**
 can be matched depending on how the argument is interpreted. At most
 64 filter arguments can be specified.

 @table FilterArgs
 @field type (string) Possible values are *string* or *path* depending
 on how the match should be interpreted. If not specified then *string*
 is the default.
 @field index (number) The argument index [0, 63].
 @field value (string) The D-Bus *string* or *object path*.
 */

/**
 * @brief Process rule matches and dispatch to Lua handler function.
 *
 * This function is called whenever a match rule is matched and needs
 * to be dispatched to a Lua handler function.
 *
 * @param [in] conn The CDBUS connection on which the message matched.
 * @param [in] hnd An opaque CDBUS match handle.
 * @param [in] msg The D-Bus message that matched.
 * @param [in] userData User provided data that is returned in the callback.
 */
static void
l2dbus_matchHandler
    (
    cdbus_Connection*   conn,
    cdbus_Handle        hnd,
    DBusMessage*        msg,
    void*               userData
    )
{
    lua_State* L = l2dbus_callbackGetThread();
    const char* errMsg = "";
    l2dbus_Match* match = (l2dbus_Match*)userData;

    assert( NULL != L );

    if ( NULL != match)
    {
        /* Push function and user value on the stack and execute the callback */
        lua_rawgeti(L, LUA_REGISTRYINDEX, match->cbCtx.funcRef);
        lua_pushlightuserdata(L, match);

        /* Leaves a Message userdata object on the stack */
        l2dbus_messageWrap(L, msg, L2DBUS_TRUE);

        lua_rawgeti(L, LUA_REGISTRYINDEX, match->cbCtx.userRef);

        if ( 0 != lua_pcall(L, 3 /* nArgs */, 0, 0) )
        {
            if ( lua_isstring(L, -1) )
            {
                errMsg = lua_tostring(L, -1);
            }
            L2DBUS_TRACE((L2DBUS_TRC_ERROR, "Match callback error: %s", errMsg));
        }
    }

    /* Clean up the thread stack */
    lua_settop(L, 0);
}


/**
 * @brief De-allocates and free's a match rule structure.
 *
 * This function is called to free and destroy a match rule
 * structure.
 *
 * @param [in] rule The match rule to free.
 */
static void
l2dbus_matchFreeRule
    (
    cdbus_MatchRule*    rule
    )
{
    int idx;
    if ( NULL != rule )
    {
        l2dbus_free(rule->member);
        l2dbus_free(rule->objInterface);
        l2dbus_free(rule->sender);
        l2dbus_free(rule->path);
        l2dbus_free(rule->arg0Namespace);

        if ( NULL != rule->filterArgs )
        {
            for ( idx = 0; idx <= DBUS_MAXIMUM_MATCH_RULE_ARG_NUMBER; ++idx )
            {
                if ( CDBUS_FILTER_ARG_INVALID ==
                    rule->filterArgs[idx].argType)
                {
                    break;
                }
                l2dbus_free(rule->filterArgs[idx].value);
            }

            l2dbus_free(rule->filterArgs);
        }
    }
}


/**
 * @brief Constructs a new match rule.
 *
 * This function is called to construct a new match rule.
 *
 * @param [in]      L           Lua state
 * @param [in]      ruleIdx     The reference to the table on the Lua stack
 * containing the representation of the match rule.
 * @param [in]      funcIdx     The reference to a Lua function representing
 * the handler.
 * @param [in]      userIdx     The reference to the user token data.
 * @param [in]      connIdx     The reference to the Lua connection userdata.
 * @param [in,out]  errMsg      A pointer to an optional string pointer to receive a
 *                              constant error message. This returned pointer
 *                              should not be freed. If not needed then pass
 *                              in NULL
 *
 * @return
 */
l2dbus_Match*
l2dbus_newMatch
    (
    lua_State*      L,
    int             ruleIdx,
    int             funcIdx,
    int             userIdx,
    int             connIdx,
    const char**    errMsg
    )
{
    l2dbus_Match* match = NULL;
    cdbus_MatchRule rule;
    int nFilterArgs = 0;
    int idx;
    int argArrayRef;
    int itemRef;
    int top;
    const char* reason = "";
    l2dbus_Bool failed = FALSE;
    cdbus_FilterArgType argType;
    l2dbus_Connection* connUd;

    L2DBUS_TRACE((L2DBUS_TRC_TRACE, "Create: match"));
    ruleIdx = lua_absindex(L, ruleIdx);
    funcIdx = lua_absindex(L, funcIdx);
    connIdx = lua_absindex(L, connIdx);

    /* Zero it out in preparation to filling it in */
    memset(&rule, 0, sizeof(rule));

    lua_getfield(L, ruleIdx, "msgType");
    if ( !lua_isnumber(L, -1) )
    {
        rule.msgType = CDBUS_MATCH_MSG_ANY;
    }
    else
    {
        switch ( lua_tointeger(L, -1) )
        {
            case DBUS_MESSAGE_TYPE_METHOD_CALL:
                rule.msgType = CDBUS_MATCH_MSG_METHOD_CALL;
                break;

            case DBUS_MESSAGE_TYPE_METHOD_RETURN:
                rule.msgType = CDBUS_MATCH_MSG_METHOD_RETURN;
                break;

            case DBUS_MESSAGE_TYPE_ERROR:
                rule.msgType = CDBUS_MATCH_MSG_ERROR;
                break;

            case DBUS_MESSAGE_TYPE_SIGNAL:
                rule.msgType = CDBUS_MATCH_MSG_SIGNAL;
                break;

            default:
                rule.msgType = CDBUS_MATCH_MSG_ANY;
                break;
        }
    }
    /* Pop off the msgType */
    lua_pop(L, 1);

    lua_getfield(L, ruleIdx, "member");
    if ( lua_isstring(L, -1) )
    {
        rule.member = l2dbus_strDup(lua_tostring(L, -1));
    }
    else
    {
        rule.member = NULL;
    }
    lua_pop(L, 1);

    lua_getfield(L, ruleIdx, "interface");
    if ( lua_isstring(L, -1) )
    {
        rule.objInterface = l2dbus_strDup(lua_tostring(L, -1));
    }
    else
    {
        rule.objInterface = NULL;
    }
    lua_pop(L, 1);

    lua_getfield(L, ruleIdx, "sender");
    if ( lua_isstring(L, -1) )
    {
        rule.sender = l2dbus_strDup(lua_tostring(L, -1));
    }
    else
    {
        rule.sender = NULL;
    }
    lua_pop(L, 1);

    lua_getfield(L, ruleIdx, "path");
    if ( lua_isstring(L, -1) )
    {
        rule.path = l2dbus_strDup(lua_tostring(L, -1));
    }
    else
    {
        rule.path = NULL;
    }
    lua_pop(L, 1);

    lua_getfield(L, ruleIdx, "treatPathAsNamespace");
    if ( lua_isboolean(L, -1) )
    {
        rule.treatPathAsNamespace = (lua_toboolean(L, -1) == 0) ? CDBUS_FALSE : CDBUS_TRUE;
    }
    else
    {
        rule.treatPathAsNamespace = CDBUS_FALSE;
    }
    lua_pop(L, 1);

    lua_getfield(L, ruleIdx, "arg0Namespace");
    if ( lua_isstring(L, -1) )
    {
        rule.arg0Namespace = l2dbus_strDup(lua_tostring(L, -1));
    }
    else
    {
        rule.arg0Namespace = NULL;
    }
    lua_pop(L, 1);

    lua_getfield(L, ruleIdx, "eavesdrop");
    if ( lua_isboolean(L, -1) )
    {
        rule.eavesdrop = (lua_toboolean(L, -1) == 0) ? CDBUS_FALSE : CDBUS_TRUE;
    }
    else
    {
        rule.eavesdrop = CDBUS_FALSE;
    }
    lua_pop(L, 1);

    rule.filterArgs = NULL;
    lua_getfield(L, ruleIdx, "filterArgs");
    if ( lua_istable(L, -1) )
    {
        argArrayRef = lua_absindex(L, -1);
        nFilterArgs = lua_rawlen(L, -1);
        if ( nFilterArgs > 0 )
        {
            if ( nFilterArgs > DBUS_MAXIMUM_MATCH_RULE_ARG_NUMBER+1 )
            {
                nFilterArgs = DBUS_MAXIMUM_MATCH_RULE_ARG_NUMBER+1;
            }
            rule.filterArgs = (cdbus_FilterArgItem*)l2dbus_calloc(nFilterArgs+1,
                                sizeof(cdbus_FilterArgItem));
            if ( NULL == rule.filterArgs )
            {
                failed = TRUE;
                reason = "failed to allocate memory for argN filter elements";
            }
            else
            {
                /* Remember the top of the stack in case we need
                 * to break out of the loop. This will be used
                 * to "restore" the stack to it's previous level.
                 */
                top = lua_gettop(L);

                /* Loop over all the argN filters */
                for ( idx = 0; idx < nFilterArgs; ++idx )
                {
                    lua_rawgeti(L, argArrayRef, idx+1);
                    if ( !lua_istable(L, -1) )
                    {
                        failed = L2DBUS_TRUE;
                        reason = "argN table expected";
                        break;
                    }

                    itemRef = lua_absindex(L, -1);

                    argType = CDBUS_FILTER_ARG_INVALID;
                    lua_getfield(L, itemRef, "type");
                    if ( lua_isstring(L, -1) )
                    {
                        if ( 0 == strcmp(lua_tostring(L, -1), "string") )
                        {
                            argType = CDBUS_FILTER_ARG;
                        }
                        else if ( 0 == strcmp(lua_tostring(L, -1), "path") )
                        {
                            argType = CDBUS_FILTER_ARG_PATH;
                        }
                    }
                    /* Else if this field was *not* specified then */
                    else if ( lua_isnil(L, -1) )
                    {
                        /* The default is to treat the argument as a
                         * regular (string) argument for the matching.
                         */
                        argType = CDBUS_FILTER_ARG;
                    }
                    lua_pop(L, 1);

                    if ( (CDBUS_FILTER_ARG != argType) &&
                        (CDBUS_FILTER_ARG_PATH != argType) )
                    {
                        failed = L2DBUS_TRUE;
                        reason = "unknown argument type specified (!= 'path' or 'string')";
                        break;
                    }
                    rule.filterArgs[idx].argType = argType;

                    lua_getfield(L, itemRef, "index");
                    if ( !lua_isnumber(L, -1) )
                    {
                        failed = L2DBUS_TRUE;
                        reason = "arg filter index not specified";
                        break;
                    }
                    rule.filterArgs[idx].argN = lua_tointeger(L, -1);
                    lua_pop(L, 1);
                    /* If the argN index is out of range then ... */
                    if ( (0 > rule.filterArgs[idx].argN) ||
                        (DBUS_MAXIMUM_MATCH_RULE_ARG_NUMBER <
                         rule.filterArgs[idx].argN) )
                    {
                        failed = L2DBUS_TRUE;
                        reason = "arg filter index out of range";
                        break;
                    }

                    lua_getfield(L, itemRef, "value");
                    if ( !lua_isstring(L, -1) )
                    {
                        failed = L2DBUS_TRUE;
                        reason = "arg filter missing a value";
                        break;
                    }

                    rule.filterArgs[idx].value = l2dbus_strDup(lua_tostring(L, -1));

                    /* Pop the value and the item table */
                    lua_pop(L, 2);
                }

                /* Restore the top of the stack */
                lua_settop(L, top);

                /* Set a marker to indicate the end of the array */
                rule.filterArgs[idx].argType = CDBUS_FILTER_ARG_INVALID;
            }
        }
    }

    /* Pop what should be the filterArgs table */
    lua_pop(L, 1);

    /* If the rule has been parsed successfully then ... */
    if ( !failed )
    {
        match = (l2dbus_Match*)l2dbus_calloc(1, sizeof(*match));
        if ( NULL == match )
        {
            failed = L2DBUS_TRUE;
            reason = "failed to allocate memory for match object";
        }
        else
        {
            connUd = (l2dbus_Connection*)lua_touserdata(L, connIdx);
            match->matchHnd = cdbus_connectionRegMatchHandler(
                                                    connUd->conn,
                                                    l2dbus_matchHandler,
                                                    match,
                                                    &rule,
                                                    NULL);

            if ( CDBUS_INVALID_HANDLE == match->matchHnd )
            {
                failed = L2DBUS_TRUE;
                reason = "failed to register match handler";
            }
            else
            {
                lua_pushvalue(L, connIdx);
                match->connRef = luaL_ref(L, LUA_REGISTRYINDEX);
                l2dbus_callbackInit(&match->cbCtx);
                l2dbus_callbackRef(L, funcIdx, userIdx, &match->cbCtx);
            }
        }
    }

    if ( *errMsg != NULL )
    {
        *errMsg = reason;
    }

    if ( failed )
    {
        free(match);
        match = NULL;
    }

    /* Always free the rule since we no longer need it */
    l2dbus_matchFreeRule(&rule);

    return match;
}


/**
 * @brief Called to dispose/free a match rule.
 *
 * This function is called to destroy a match rule.
 *
 * @param [in]  L       Lua state
 * @param [in]  match   The match rule to destroy
 */
void
l2dbus_disposeMatch
    (
    lua_State*      L,
    l2dbus_Match*   match
    )
{
    cdbus_HResult rc;
    l2dbus_Connection* connUd;

    if ( NULL != match )
    {
        lua_rawgeti(L, LUA_REGISTRYINDEX, match->connRef);
        connUd = (l2dbus_Connection*)lua_touserdata(L, -1);
        rc = cdbus_connectionUnregMatchHandler(connUd->conn, match->matchHnd);
        if ( CDBUS_FAILED(rc) )
        {
            L2DBUS_TRACE((L2DBUS_TRC_WARN, "Failed to unregister match (0x%x)", rc));
        }
        l2dbus_callbackUnref(L, &match->cbCtx);
        /* Pop of the connection userdata */
        lua_pop(L, 1);
        luaL_unref(L, LUA_REGISTRYINDEX, match->connRef);
        l2dbus_free(match);
    }
}


