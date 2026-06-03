#include <base/component.h>
#include <util/construct_at.h>
#include <util/list.h>

#include "snapper_session/connection.h"
#include "utils.h"

/* Test Stats */
static unsigned total_tests = 0;
static unsigned successful_tests = 0;
static unsigned ignored_tests = 0;

/* Run Test */
[[maybe_unused]] static void
test (const char *name, bool condition)
{
  total_tests++;

  if (condition)
    {
      successful_tests++;
      Genode::log (GREEN "[PASS] ", name);
    }
  else
    {
      Genode::log (RED "[FAIL] ", name);
    }
}

#define TEST(condition) return test (__PRETTY_FUNCTION__, condition)

/* Ignore Test */
[[maybe_unused]] static void
ignore (const char *name)
{
  ignored_tests++;
  Genode::log (YELLOW "[SKIP] ", name);
}

#define IGNORE                                                                \
  ignore (__PRETTY_FUNCTION__);                                               \
  return

/* Summarize Test Results */
void
summary (void)
{
  Genode::log ("\n", successful_tests, "/", total_tests, " tests passed.");
  Genode::log (ignored_tests, " tests were ignored.");
}

/* Tests */
void
test_snapshot_creation (Snapper::Connection &snapper)
{
  Result res = snapper.init_snapshot ();
  
  if (res != Snapper::Ok && res != Snapper::NoPriorGen)
    {
      TEST (false);
    }

  for (int i = 1; i <= TESTS; i++)
    {
      if (snapper.take_snapshot (&i, sizeof (decltype (i)), i) != Snapper::Ok)
        {
          TEST (false);
        }
    }

  if (snapper.commit_snapshot () != Snapper::Ok)
    {
      TEST (false);
    }

  TEST (true);
}

void
test_successful_recovery (Snapper::Connection &snapper)
{
  int value = 0;
  Genode::size_t size = sizeof (decltype (value));

  if (snapper.open_generation () != Snapper::Ok)
    TEST (false);

  for (int i = 1; i <= TESTS; i++)
    {
      Snapper::Result res = snapper.restore ((char *)&value, size, i);
      if (res != Ok) {
        TEST(false);
      }

      if (value != i)
        TEST (false);
    }

  if (snapper.close_generation () != Snapper::Ok)
    TEST (false);

  TEST (true);
}

void
test_snapshot_purge (Snapper::Connection &snapper)
{
  bool ok = snapper.purge () == Snapper::Ok;

  TEST (ok);
}

void
test_snapshot_purge_zombies (Snapper::Connection &snapper)
{
  bool ok = snapper.purge_zombies () == Snapper::Ok;

  TEST (ok);
}

void
Component::construct (Genode::Env &env)
{
  Snapper::Connection snapper (env);

  Genode::log ("-*- SNAPPER TEST SUITE -*-\n");

  test_snapshot_creation (snapper);
  test_snapshot_creation (snapper); // test linking with identical snapshot
  test_successful_recovery (snapper);
  test_snapshot_purge (snapper);
  test_snapshot_purge_zombies (snapper);
  test_snapshot_purge (snapper);

  summary ();
  Genode::log ("\n-*- SNAPPER TESTS DONE -*-");

  env.parent ().exit ((successful_tests == total_tests) ? 0 : 1);
}
