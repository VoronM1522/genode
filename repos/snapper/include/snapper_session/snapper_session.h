/**
 * @brief Snapper session and root interfaces.
 * @author Rumen Mitov
 * @date 2025-08-23
 */

#ifndef __SNAPPER_SESSION_H
#define __SNAPPER_SESSION_H

#ifdef __cplusplus
#include <base/attached_ram_dataspace.h>
#include <base/rpc.h>
#include <session/session.h>

#include "snapper.h"

namespace Snapper
{
  struct Session;
  struct Session_component;
  class Root_component;
};

using namespace Snapper;

struct Snapper::Session : Genode::Session
{
  static const char *
  service_name ()
  {
    return "Snapper";
  }

  /*
   * A terminal session consumes a dataspace capability for the server's
   * session-object allocation, its session capability, and a dataspace
   * capability for the communication buffer.
   */
  enum
  {
    CAP_QUOTA = 3
  };

  /**
   * @brief Internal method for returning the dataspace used for the
   *        communication buffer.
   */
  virtual Genode::Dataspace_capability _dataspace (void) = 0;

  /**
   * @brief Internal wrapper that uses the communication buffer.
   */
  virtual Result _take_snapshot (Genode::size_t, Archive::ArchiveKey) = 0;

  /**
   * @brief Internal wrapper that uses the communication buffer.
   */
  virtual Result _restore (Genode::size_t, Archive::ArchiveKey) = 0;

  virtual Result init_snapshot (void) = 0;

  virtual Result commit_snapshot (void) = 0;

  virtual Result open_generation (
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
          & = "")
      = 0;

  virtual Result close_generation (void) = 0;

  virtual Result
  purge (const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
             & = "")
      = 0;

  virtual void purge_expired (void) = 0;

  virtual Result purge_zombies (void) = 0;

  GENODE_RPC (Rpc_dataspace, Genode::Dataspace_capability, _dataspace);

  GENODE_RPC (Rpc_init_snapshot, Result, init_snapshot);

  GENODE_RPC (Rpc_take_snapshot, Result, _take_snapshot, Genode::size_t,
              Archive::ArchiveKey);

  GENODE_RPC (Rpc_commit_snapshot, Result, commit_snapshot);

  GENODE_RPC (
      Rpc_open_generation, Result, open_generation,
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> &);

  GENODE_RPC (Rpc_restore, Result, _restore, Genode::size_t,
              Archive::ArchiveKey);

  GENODE_RPC (Rpc_close_generation, Result, close_generation);

  GENODE_RPC (
      Rpc_purge, Result, purge,
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> &);

  GENODE_RPC (Rpc_purge_expired, void, purge_expired);

  GENODE_RPC (Rpc_purge_zombies, Result, purge_zombies);

  GENODE_RPC_INTERFACE (Rpc_dataspace, Rpc_init_snapshot, Rpc_take_snapshot,
                        Rpc_commit_snapshot, Rpc_open_generation, Rpc_restore,
                        Rpc_close_generation, Rpc_purge, Rpc_purge_expired,
                        Rpc_purge_zombies);
};

struct Snapper::Session_component : Genode::Rpc_object<Session>
{
  /**
   * @brief Session_component is a wrapper for the Snapper::Main object.
   */
  Snapper::Main &snapper;

  /**
   * @brief Dataspace for communication with client.
   */
  Genode::Attached_ram_dataspace ds;

  Session_component () = delete;
  Session_component (Genode::Env &env, Snapper::Main &snapper,
                     const Genode::Number_of_bytes bufsize)
      : snapper (snapper), ds (env.ram (), env.rm (), bufsize)
  {
  }

  Genode::Dataspace_capability
  _dataspace () override
  {
    return ds.cap ();
  }

  Result
  _take_snapshot (Genode::size_t size, Archive::ArchiveKey identifier) override
  {
    return snapper.take_snapshot (ds.local_addr<void> (), size, identifier);
  }

  Result
  _restore (Genode::size_t size, Archive::ArchiveKey identifier) override
  {
    return snapper.restore (ds.local_addr<void> (), size, identifier);
  }

  Result
  init_snapshot (void) override
  {
    return snapper.init_snapshot ();
  }

  Result
  commit_snapshot (void) override
  {
    return snapper.commit_snapshot ();
  }

  Result
  open_generation (
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
          &generation) override
  {
    return snapper.open_generation (generation);
  }

  Result
  close_generation (void) override
  {
    return snapper.close_generation ();
  }

  Result
  purge (const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
             &generation) override
  {
    return snapper.purge (generation);
  }

  void
  purge_expired (void) override
  {
    snapper.purge_expired ();
  }

  Result
  purge_zombies (void) override
  {
    return snapper.purge_zombies ();
  }
};

class Snapper::Root_component
    : public Genode::Root_component<Session_component>
{
protected:
  // Create_result  // API 2025-04-09: _create_session returns SESSION_TYPE*, not Create_result
  Session_component *  // Added
  _create_session (const char *) override
  {
     _num_sessions++; // Added
    _had_sessions = true; // Added
    // return *(new (md_alloc ()) Session_component (env, snapper, bufsize));  // Added: deref wrong for pointer return
    return new (md_alloc ()) Session_component (env, snapper, bufsize);  // Added
  }

  // Added
  void
  _destroy_session (Session_component *session) override
  {
    destroy (*session);
    if (--_num_sessions == 0 && _had_sessions) {
      Genode::log("Snapper has no active sessions.\nExit!");
      env.parent().exit (0); 
    }
  }   

public:
  Root_component (Genode::Env &env, Genode::Allocator &md_alloc,
                  Snapper::Main &snapper,
                  const Genode::Number_of_bytes bufsize)
      : Genode::Root_component<Session_component> (env.ep (), md_alloc),
        env (env), snapper (snapper), bufsize (bufsize)
  {
    if (snapper.config.verbose)
      Genode::log ("root snapper component created");
  }

private:
  Genode::Env &env;
  Snapper::Main &snapper;
  Genode::Number_of_bytes bufsize;
  unsigned _num_sessions { 0 }; // Added
  bool _had_sessions { false }; // Added
};

#endif // __cplusplus

#endif // __SNAPPER_SESSION_H
