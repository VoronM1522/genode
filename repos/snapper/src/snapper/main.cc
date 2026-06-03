#include <base/component.h>
#include <base/log.h>

#include "snapper.h"

void
Component::construct (Genode::Env &env)
{
  static Snapper::Main snapper(env);
}
