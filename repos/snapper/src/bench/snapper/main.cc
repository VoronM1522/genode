#include <base/component.h>
#include <util/construct_at.h>
#include <util/list.h>

#include "snapper_session/connection.h"

#ifndef PAYLOAD_NUM
#define PAYLOAD_NUM 100
#endif // PAYLOAD_NUM

#ifndef PAYLOAD_SIZE
#define PAYLOAD_SIZE 1
#endif // PAYLOAD_SIZE

constexpr static unsigned
mb_to_bytes (unsigned mb)
{
  return mb * 1000 * 1000;
}

constexpr static unsigned PAYLOAD_SIZE_BYTES = mb_to_bytes (PAYLOAD_SIZE);

void
Component::construct (Genode::Env &env)
{
  Snapper::Connection snapper (env);
  Genode::Heap heap{ env.ram (), env.rm () };

  (void)snapper.init_snapshot ();

  for (int i = 1; i <= PAYLOAD_NUM; i++)
    {
      char *payload = (char *)heap.alloc (PAYLOAD_SIZE_BYTES);

      (void)snapper.take_snapshot (payload, PAYLOAD_SIZE_BYTES, i);

      heap.free (payload, PAYLOAD_SIZE_BYTES);
    }

  (void)snapper.commit_snapshot ();
  env.parent ().exit (0);
}
