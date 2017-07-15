/**
 * @file mega/posix/megawaiter.h
 * @brief POSIX event/timeout handling
 *
 * (c) 2013-2014 by Mega Limited, Wellsford, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#ifndef MEGACHAT_WAIT_CLASS
#define MEGACHAT_WAIT_CLASS LibwebsocketsWaiter

#include <libwebsockets.h>
#include "mega/waiter.h"

namespace mega {
struct LibwebsocketsWaiter : public Waiter
{
    struct lws_context *wscontext;

    LibwebsocketsWaiter();
    ~LibwebsocketsWaiter();

    void init(dstime);
    int wait();

    void notify();

};
} // namespace

#endif