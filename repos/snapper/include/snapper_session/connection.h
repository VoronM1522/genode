/**
 * @brief Connection to Snapper service.
 * @author Rumen Mitov
 * @date 2025-08-23
 */

#ifndef __SNAPPER_SESSION_CONNECTION_H
#define __SNAPPER_SESSION_CONNECTION_H

#include "client.h"
#include <base/connection.h>

namespace Snapper
{
  struct Connection;
}

struct Snapper::Connection : Genode::Connection<Session>,
                             Snapper::Session_client
{
  Connection (Genode::Env &env)
      : Genode::Connection<Snapper::Session> (env, Label (),
                                              Ram_quota{ 8 * 1024 }, Args ()),
        Snapper::Session_client (env.rm (), cap ())
  {
  }
};

#endif // __SNAPPER_SESSION_CONNECTION_H
