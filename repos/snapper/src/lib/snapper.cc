#include <base/allocator.h>
#include <util/construct_at.h>
#include <vfs/directory_service.h>
#include <vfs/vfs_handle.h>

#include "snapper.h"
#include "snapper_session/snapper_session.h"
#include "utils.h"
#include "xxhash32.h"

namespace Snapper
{
  /*
   * CONSTRUCTORS
   */

  Main::Main (Genode::Env &env) : env (env)
  {
    config.verbose
        = rom.xml ().attribute_value<decltype (Snapper::Config::verbose)> (
            "verbose", Snapper::Config::_verbose);

    config.redundancy
        = rom.xml ().attribute_value<decltype (Snapper::Config::redundancy)> (
            "redundancy", Snapper::Config::_redundancy);

    config.integrity
        = rom.xml ().attribute_value<decltype (Snapper::Config::integrity)> (
            "integrity", Snapper::Config::_integrity);

    config.threshold
        = rom.xml ().attribute_value<decltype (Snapper::Config::threshold)> (
            "threshold", Snapper::Config::_threshold);

    config.max_snapshots
        = rom.xml ()
              .attribute_value<decltype (Snapper::Config::max_snapshots)> (
                  "max_snapshots", Snapper::Config::_max_snapshots);

    config.min_snapshots
        = rom.xml ()
              .attribute_value<decltype (Snapper::Config::min_snapshots)> (
                  "min_snapshots", Snapper::Config::_min_snapshots);

    config.expiration
        = rom.xml ().attribute_value<decltype (Snapper::Config::expiration)> (
            "expiration", Snapper::Config::_expiration);

    config.bufsize = rom.xml ().attribute_value (
        "bufsize", Genode::Number_of_bytes (Snapper::Config::_bufsize));

    archiver->verbose = config.verbose;

    static Snapper::Root_component root (env, heap, *this, config.bufsize);
    env.parent ().announce (env.ep ().manage (root));
  }

  Main::~Main ()
  {
    generation.destruct ();
    snapshot.destruct ();
    archiver.destruct ();

    if (config.verbose)
      Genode::log ("snapper object destroyed.");
  }

  /*
   * CREATING SNAPSHOT
   */
  Snapper::Result
  Main::init_snapshot ()
  {
    if (state != Dormant)
      return InvalidState;

    state = Creation;
    snapshot_file_count = 0;

    Result res = __remove_unfinished_gen ();
    if (res != Ok)
      return res;

    res = __init_gen ();
    if (res != Ok)
      return res;

    return res;
  }

  Snapper::Result
  Main::take_snapshot (void const *const payload, Genode::uint64_t size,
                       Archive::ArchiveKey identifier)
  {
    if (state != Creation)
      return InvalidState;

    snapshots_requested++;

    bool new_backlink_needed = false;
    Snapper::HASH hash = xxhash32 (payload, size);

    // check if identifier exists in the mapping and if the hash
    // matches the calculated hash of the payload.
    archiver->archive.with_element (
        identifier,
        [this, &new_backlink_needed, &hash] (Archive::ArchiveEntry &entry) {
          // INFO Go through backlinks until a valid one is found.
          Archive::Backlink *latest_valid_backlink = nullptr;
          entry.queue.for_each ([&] (Archive::Backlink &backlink) {
            if (backlink.is_backlink_valid (hash))
              {
                latest_valid_backlink = backlink._self;
              }
            else
              {
                if (config.verbose)
                  Genode::log ("removing outdated backlink: ", backlink.value);

                entry.queue.remove (backlink);
                Genode::destroy (archiver->heap, backlink._self);
                archiver->total_backlinks--;
              }
          });

          if (!latest_valid_backlink)
            {
              new_backlink_needed = true;
              return;
            }

          latest_valid_backlink->get_reference_count ().with_result (
              [this, &new_backlink_needed,
               &latest_valid_backlink] (Snapper::RC rc) {
                if (rc >= config.redundancy)
                  {
                    if (config.verbose)
                      Genode::log ("backlink reference count exceeded: ",
                                   latest_valid_backlink->value,
                                   ". Creating redundant copy.");

                    new_backlink_needed = true;
                  }
                else
                  {
                    if (latest_valid_backlink->set_reference_count (rc + 1)
                            .failed ())
                      {
                        Genode::error ("failed to update reference count of "
                                       "backlink! Creating a new backlink.");

                        new_backlink_needed = true;
                      }
                  }
              },
              [&new_backlink_needed] (Snapper::Archive::Backlink::Error) {
                // INFO Technically, this code should not be reachable
                // since we already checked that the backlink is valid.
                new_backlink_needed = true;
              });
        },
        [&] () { new_backlink_needed = true; });

    if (!new_backlink_needed)
      return Ok;

    snapshot_files_created++;

    // create a new snapshot file and write to it the payload metadata
    // and the payload data

    snapshot_file_count++;

    if (snapshot_file_count >= config.threshold)
      {
        snapshot->create_sub_directory ("ext");
        if (!snapshot->directory_exists ("ext"))
          {
            Genode::error ("could not create extender sub-directory!");
            throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
          }

        /* INFO
           `snapshot.construct()` will run the destructor first, hence
           we need to copy the old snapshot directory.
         */
        Genode::Directory old_snapshot_dir (*snapshot, "/");

        snapshot.construct (old_snapshot_dir, "ext");
        snapshot_dir_path = Genode::Directory::join (snapshot_dir_path, "ext");
        snapshot_file_count = 0;
      }

    Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
        filepath_base ((Genode::Hex (snapshot_file_count)));

    try
      {
        VERSION ver = Version;

        Genode::New_file file (*snapshot, filepath_base);

        Genode::size_t buf_size = sizeof (Snapper::VERSION)
                                  + sizeof (Snapper::HASH)
                                  + sizeof (Snapper::RC) + size;

        char *buf = new (heap) char[buf_size];

        Genode::memcpy (buf, (char *)&ver, sizeof (Snapper::VERSION));
        Genode::memcpy (buf + sizeof (Snapper::VERSION), (char *)&hash,
                        sizeof (Snapper::HASH));

        Snapper::RC reference_count = 1;
        Genode::memcpy (buf + sizeof (Snapper::VERSION)
                            + sizeof (Snapper::HASH),
                        (char *)&reference_count, sizeof (Snapper::RC));

        Genode::memcpy (buf + sizeof (Snapper::VERSION)
                            + sizeof (Snapper::HASH) + sizeof (Snapper::RC),
                        payload, size);

        Genode::New_file::Append_result res = file.append (buf, buf_size);

        heap.free (buf, buf_size);

        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error ("could not write to backlink file: ",
                           filepath_base);
            throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
          }
      }
    catch (Genode::New_file::Create_failed)
      {
        Genode::error ("could not create file: ", filepath_base);
        throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
      }

    // save the snapshot file's path (i.e. a backlink) into the archive
    // (relative to snapper_root)

    archiver->insert (identifier, Genode::Directory::join (snapshot_dir_path,
                                                           filepath_base));
    return Ok;
  }

  Snapper::Result
  Main::commit_snapshot (void)
  {
    if (state != Creation)
      return InvalidState;

    if (!generation.constructed ())
      {
        Genode::error ("generation object was not constructed!");
        return InvalidState;
      }

    if (!archiver.constructed ())
      {
        if (config.verbose)
          {
            Genode::log ("archiver is empty! Aborting the snapshot.");
          }

        __abort_snapshot ();
        return InvalidState;
      }

    archiver->commit (*generation);

    Genode::Microseconds snap_fin{ timer.curr_time ().trunc_to_plain_us () };

    if (config.verbose)
      Genode::log ("generation committed successfully! ", snapshots_requested,
                   " snapshots requested, ", snapshot_files_created,
                   " snapshot files created, ",
                   snap_fin.value - snap_start.value, "us");

    __reset_gen ();

    state = Dormant;

    purge_expired ();

    return Ok;
  }

  /*
   * RESTORING SNAPSHOT
   */

  Snapper::Result
  Main::open_generation (
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
          &generation)
  {
    if (state != Dormant)
      return InvalidState;

    state = Restoration;

    Result res = __remove_unfinished_gen ();
    if (res != Ok)
      return res;

    res = __load_gen (generation);
    if (res == LoadGenFailed)
      {
        state = Dormant;
      }

    return res;
  }

  Snapper::Result
  Main::restore (void *dst, Genode::size_t size,
                 Archive::ArchiveKey identifier)
  {
    if (state != Restoration)
      return InvalidState;

    Snapper::Result res = Ok;
    Genode::memset (dst, 0, size);

    archiver->archive.with_element (
        identifier,
        [&res, &dst, &size] (Archive::ArchiveEntry &entry) {
          entry.queue.for_each (
              [&res, &dst, &size] (Archive::Backlink &backlink) {
                Genode::Byte_range_ptr dst_buf ((char *)dst, size);
                switch (backlink.get_data (dst_buf))
                  {
                  case Archive::Backlink::Error::None:
                    res = Ok;
                    break;
                  case Archive::Backlink::Error::InvalidVersion:
                    res = InvalidVersion;
                    break;
                  case Archive::Backlink::Error::InvalidIntegrity:
                    res = IntegrityFailed;
                    break;
                  case Archive::Backlink::Error::MissingFieldErr:
                    res = IntegrityFailed;
                    break;
                  default:
                    res = RestoreFailed;
                    break;
                  }
              });
        },
        [&res] () { res = NoMatches; });

    return res;
  }

  Snapper::Result
  Main::close_generation (void)
  {
    if (state != Restoration)
      return InvalidState;

    state = Dormant;

    if (generation.constructed ())
      generation.destruct ();

    // INFO Do not destruct the archiver as it will be used as the
    // basis for future snapshots.

    purge_expired ();

    return Ok;
  }

  Snapper::Result
  Main::purge (
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
          &generation)
  {
    if (state != Dormant)
      {
        return InvalidState;
      }

    /* INFO
       Check that we have the minimum allowed generation count.
    */
    if (__num_gen () - 1 < config.min_snapshots)
      {
        Genode::error ("purging generation will reduce snapshot count to less "
                       "than allowed!");
        return PurgeDenied;
      }

    Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> _gen
        = generation;

    bool validity_verified = false;

    if (_gen == "")
      {
        snapper_root.for_each_entry (
            [this, &validity_verified,
             &_gen] (const Genode::Directory::Entry &entry) {
              if (__valid_archive (
                      Genode::Directory::join (entry.name (), "archive")))
                {
                  if (_gen == "")
                    {
                      _gen = entry.name ();
                    }
                  else if (_gen > entry.name ())
                    {
                      _gen = entry.name ();
                    }

                  validity_verified = true;
                }
            });
      }

    if (_gen == "")
      {
        if (config.verbose)
          Genode::log ("no generation exists for purging");

        return NoPriorGen;
      }

    if (!validity_verified)
      {
        if (!__valid_archive (Genode::Directory::join (_gen, "archive")))
          {
            if (config.verbose)
              Genode::log ("no generation exists for purging");

            return NoPriorGen;
          }
      }

    state = Purge;
    Snapper::Result res = Ok;

    try
      {
        Genode::Directory archive_dir (snapper_root, _gen);
        Genode::Readonly_file archive_file (archive_dir, "archive");

        Archive archiver_to_purge (heap, snapper_root, config.verbose);
        archiver_to_purge.extract_from_archive_file (archive_file);

        while (true)
          {
            bool has_element = archiver_to_purge.archive.with_any_element (
                [this,
                 &archiver_to_purge] (const Archive::ArchiveEntry &entry) {
                  // decrement each backlink's reference count
                  entry.queue.for_each ([this] (Archive::Backlink &backlink) {
                    bool remove = false;

                    backlink.get_reference_count ().with_result (
                        [&backlink, &remove] (Snapper::RC reference_count) {
                          reference_count--;

                          // if the reference count is 0 or less, remove the
                          // backlink
                          if (reference_count > 0)
                            {
                              if (backlink
                                      .set_reference_count (reference_count)
                                      .failed ())
                                {
                                  remove = true;
                                }
                            }
                          else
                            remove = true;
                        },
                        [&remove, this] (Archive::Backlink::Error) {
                          if (config.integrity)
                            remove = true;
                        });

                    if (remove)
                      __delete_upwards (backlink.value.string ());

                    /* INFO
                     * No need to dequeue the Backlink as the entire
                     * ArchiveEntry will be removed.
                     */
                  });
                  archiver_to_purge.remove (entry.name);
                });

            if (!has_element)
              {
                break;
              }
          }
        __delete_upwards (Genode::Directory::join (_gen, "archive").string ());
        __reset_gen ();
      }
    catch (Genode::Readonly_file::Open_failed)
      {
        Genode::warning ("generation was already purged. If you want to "
                         "cleanup any zombie files run purge_zombies()!");
        goto CLEAN_RET;
      }

    if (config.verbose)
      Genode::log ("purged: \"", _gen, "\"");

  CLEAN_RET:
    state = Dormant;
    return res;
  }

  void
  Main::purge_expired (void)
  {
    Genode::uint64_t num_gen = __num_gen ();

    while (config.max_snapshots && num_gen > config.max_snapshots
           && num_gen > config.min_snapshots)
      {
        if (config.verbose)
          Genode::log ("snaphots exceed max number, purging oldest");

        if (purge () != Ok)
          {
            Genode::error ("could not purge oldest generation! It must be "
                           "removed manually!");

            /* INFO
               We must crash here, otherwise non-expired
               generations will be purged!
            */
            throw CrashStates::PURGE_FAILED;
          }

        num_gen = __num_gen ();
      }

    if (!config.expiration)
      return;

    Rtc::Timestamp now = rtc.current_time ();
    Genode::uint64_t expiry = timestamp_to_seconds (now) - config.expiration;

    snapper_root.for_each_entry (
        [this, expiry] (const Genode::Directory::Entry &entry) {
          if (entry.name () == "lost+found")
            return;

          try
            {
              Rtc::Timestamp ts
                  = str_to_timestamp ((char *)entry.name ().string ());

              if (timestamp_to_seconds (ts) < expiry)
                {
                  if (purge (entry.name ()) != Ok)
                    throw -1;

                  if (config.integrity)
                    Genode::log ("purged expired generation: \"",
                                 entry.name (), "\"");
                }
            }
          catch (...)
            {
              Genode::error ("failed to purge expired generation: \"",
                             entry.name (), "\"");
              throw CrashStates::PURGE_FAILED;
            }
        });
  }

  Result
  Main::purge_zombies (void)
  {
    if (state != Dormant)
      {
        return InvalidState;
      }

    Result res = Ok;
    state = Purge;

    // for each dead snapshot run __purge_zombies helper.
    snapper_root.for_each_entry ([&] (const Genode::Directory::Entry &e) {
      if (!__valid_archive (Genode::Directory::join (e.name (), "archive")))
        {
          __purge_zombies (e.name ());
        }
    });

    if (config.verbose)
      Genode::log ("no zombies remain!");

    state = Dormant;
    return res;
  }

  /*
   * HELPER FUNCTIONS
   */

  bool
  Main::__valid_archive (const Genode::Path<Vfs::MAX_PATH_LEN> &archive_path)
  {
    if (!snapper_root.file_exists (archive_path))
      {
        /* INFO
           No log message here, as this branch is commonly used when
           iterating over the <snapper-root>'s entries. Logging here
           would just clutter the logs.
         */

        return false;
      }

    try
      {
        Genode::Readonly_file _archive (snapper_root, archive_path);

        // get data size
        Vfs::file_size data_size
            = snapper_root.file_size (archive_path)
              - sizeof (decltype (Archive::total_backlinks))
              - sizeof (Snapper::HASH) - sizeof (Snapper::VERSION);

        if (data_size == 0)
          return false;

        // check version
        Genode::Readonly_file::At pos{ 0 };

        char _version_buf[sizeof (Snapper::VERSION)];
        Genode::Byte_range_ptr version_buf (_version_buf,
                                            sizeof (Snapper::VERSION));

        if (_archive.read (pos, version_buf) == 0)
          {
            // INFO No error message since this could be called from
            // __load_gen() which is not an error, only means that the
            // archive file is not yet valid.
            return false;
          }

        Snapper::VERSION version
            = *(reinterpret_cast<Snapper::VERSION *> (version_buf.start));

        if (version != Version)
          {
            Genode::error ("invalid archive, version mismatch: ", version,
                           ", should be: ", (VERSION)Version);
            return false;
          }

        // check HASH
        pos.value = sizeof (Snapper::VERSION);

        char _hash_buf[sizeof (Snapper::HASH)];
        Genode::Byte_range_ptr hash_buf (_hash_buf, sizeof (Snapper::HASH));

        if (_archive.read (pos, hash_buf) == 0)
          {
            Genode::error ("invalid archive, missing HASH: ", archive_path);
            return false;
          }

        Snapper::HASH hash
            = *(reinterpret_cast<Snapper::HASH *> (hash_buf.start));

        // get number of entries in the data
        pos.value = sizeof (Snapper::VERSION) + sizeof (Snapper::HASH);

        char _num_backlinks_buf[sizeof (decltype (Archive::total_backlinks))];
        Genode::Byte_range_ptr num_backlinks_buf (_num_backlinks_buf,
                                                  sizeof (_num_backlinks_buf));

        if (_archive.read (pos, num_backlinks_buf) == 0)
          {
            Genode::error (
                "invalid archive, missing the number of data entries: ",
                archive_path);
            return false;
          }

        // get data
        pos.value = sizeof (Snapper::VERSION) + sizeof (Snapper::HASH)
                    + sizeof (decltype (Archive::total_backlinks));

        char *_data_buf = (char *)heap.alloc (data_size);
        Genode::Byte_range_ptr data (_data_buf, data_size);

        if (_archive.read (pos, data) == 0)
          {
            Genode::error ("invalid archive, missing data: ", archive_path);
            heap.free (data.start, data.num_bytes);
            return false;
          }

        // calculate and check hash
        bool integrity = hash != xxhash32 (data.start, data.num_bytes);
        heap.free (data.start, data.num_bytes);

        if (integrity)
          {
            // INFO No error message since this could be called from
            // __load_gen() which is not an error, only means that the
            // archive file is not yet valid.
            return false;
          }
      }
    catch (Genode::Readonly_file::Open_failed)
      {
        Genode::error ("could not open archive file");
        return false;
      }
    catch (Genode::Out_of_ram)
      {
        Genode::error ("could not allocate heap: out of ram!");
        return false;
      }
    catch (Genode::Out_of_caps)
      {
        Genode::error ("could not allocate heap: out of capabilities!");
        return false;
      }
    catch (Genode::Denied)
      {
        Genode::error ("could not allocate heap: allocation denied!");
        return false;
      }

    return true;
  }

  Snapper::Result
  Main::__remove_unfinished_gen (void)
  {
    Snapper::Result res = Ok;

    snapper_root.for_each_entry ([this,
                                  &res] (const Genode::Directory::Entry &e) {
      if (e.name () == "lost+found")
        return;

      Genode::Path<Vfs::MAX_PATH_LEN> archive
          = Genode::Directory::join (e.name (), "archive");

      if (!__valid_archive (archive))
        {
          snapper_root.unlink (e.name ());
          if (snapper_root.directory_exists (e.name ()))
            {
              Genode::error ("could not remove old generation: ", e.name ());
              res = InitFailed;
            }
        }
    });

    if (config.verbose && res == Ok)
      Genode::log ("no unfinished generation remain.");

    return res;
  }

  Snapper::Result
  Main::__init_gen (void)
  {
    snap_start = timer.curr_time ().trunc_to_plain_us ();

    Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> timestamp
        = timestamp_to_str (rtc.current_time ());

    // INFO While loop to prevent two identical timestamps (older
    // generation will be overriden by newer one!).
    while (snapper_root.directory_exists (timestamp))
      {
        Genode::warning (
            "generation name is already taken, waiting for a new one...");
        timer.msleep (1000);
        timestamp = timestamp_to_str (rtc.current_time ());
      };

    snapper_root.create_sub_directory (timestamp);
    if (!snapper_root.directory_exists (timestamp))
      {
        Genode::error ("could not create generation directory: ", timestamp);
        return InitFailed;
      }

    generation.construct (snapper_root, timestamp);

    generation->create_sub_directory ("snapshot");
    if (!generation->directory_exists ("snapshot"))
      {
        Genode::error ("could not create snapshot directory: ", timestamp,
                       "/snapshot");

        snapper_root.unlink (timestamp);
        if (snapper_root.directory_exists (timestamp))
          Genode::error ("could not remove directory: ", timestamp,
                         "! It must be manually removed!");

        return InitFailed;
      }

    snapshot.construct (*generation, Genode::Path<11> ("snapshot"));
    snapshot_dir_path = Genode::Directory::join (timestamp, "snapshot");

    return Ok;
  }

  void
  Main::__reset_gen (void)
  {
    snapshot_dir_path = "/";
    snapshot_file_count = 0;
    snapshots_requested = 0;
    snapshot_files_created = 0;
    snap_start.value = 0;

    snapshot.destruct ();
    generation.destruct ();
  }

  Snapper::Result
  Main::__load_gen (
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
          &generation)
  {
    Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> latest
        = generation;

    bool validity_verified = false;

    if (latest == "")
      {
        snapper_root.for_each_entry (
            [this, &validity_verified,
             &latest] (const Genode::Directory::Entry &entry) {
              if (__valid_archive (
                      Genode::Directory::join (entry.name (), "archive")))
                {
                  if (latest == "")
                    {
                      latest = entry.name ();
                    }
                  else if (entry.name () > latest)
                    {
                      latest = entry.name ();
                    }
                  validity_verified = true;
                }
            });
      }

    if (latest == "")
      return NoPriorGen;

    if (!validity_verified)
      {
        if (!__valid_archive (Genode::Directory::join (latest, "archive")))
          {
            return NoPriorGen;
          }
      }

    try
      {
        if (config.verbose)
          {
            Genode::log ("loading generation: ", latest);
          }

        archiver.construct (heap, snapper_root, config.verbose);

        // INFO Load the latest valid generation.
        Genode::Readonly_file archive_file (
            snapper_root, Genode::Directory::join (latest, "archive"));

        archiver->extract_from_archive_file (archive_file);
      }
    catch (Genode::File::Open_failed)
      {
        Genode::error ("failed to open archive file of generation: ", latest);

        if (config.integrity)
          {
            Genode::error ("invalid archive file failed integrity check");
            throw CrashStates::INVALID_ARCHIVE_FILE;
          }

        return LoadGenFailed;
      }

    return Ok;
  }

  void
  Main::__abort_snapshot (void)
  {
    if (snapshot.constructed ())
      {
        snapshot->unlink ("/");
        snapshot.destruct ();
      }

    snapshot_dir_path = "/";
    snapshot_file_count = 0;

    if (generation.constructed ())
      {
        generation->unlink ("/");
        generation.destruct ();
      }

    archiver.destruct ();
  }

  void
  Main::__update_references (void)
  {
    bool success = false;

    archiver->archive.for_each ([this, &success] (
                                    const Archive::ArchiveEntry &entry) {
      entry.queue.for_each ([this, &success] (Archive::Backlink &backlink) {
        backlink.get_reference_count ().with_result (
            [&backlink, &success] (Snapper::RC rc) {
              if (backlink.set_reference_count (++rc).ok ())
                {
                  success = true;
                }
            },
            [this] (Snapper::Archive::Backlink::Error) {
              if (config.integrity)
                {
                  Genode::error ("invalid snapshot file");
                  throw CrashStates::INVALID_SNAPSHOT_FILE;
                }
            });

        if (success)
          return;
      });

      if (success)
        return;
    });
  }

  Genode::uint64_t
  Main::__num_gen (void)
  {
    Genode::uint64_t num_generations = 0;

    snapper_root.for_each_entry (
        [this, &num_generations] (const Genode::Directory::Entry &e) {
          if (__valid_archive (Genode::Directory::join (e, "archive")))
            num_generations++;
        });

    return num_generations;
  }

  Genode::uint64_t
  Main::__num_dirent (const Genode::String<Vfs::MAX_PATH_LEN> &dir)
  {
    Genode::uint64_t count = 0;

    Genode::Directory _dir (snapper_root, dir);
    _dir.for_each_entry (
        [&count] (const Genode::Directory::Entry &) { count++; });

    return count;
  }

  void
  Main::__delete_upwards (const char *location)
  {
    if (Genode::strlen (location) > Vfs::MAX_PATH_LEN)
      {
        Genode::error ("path is too long: \"", Genode::Cstring (location),
                       "\"! Remove manually.");

        throw CrashStates::PURGE_FAILED;
      }

    char _location[Vfs::MAX_PATH_LEN];
    Genode::memset (_location, 0, sizeof (_location));
    Genode::copy_cstring (_location, location, sizeof (_location));

    switch (snapper_root.root_dir ().unlink (location))
      {
      case Vfs::Directory_service::UNLINK_ERR_NO_ENTRY:
        return;

      case Vfs::Directory_service::UNLINK_ERR_NOT_EMPTY:
        Genode::error ("could not delete: \"", Genode::Cstring (location),
                       "\" due to directory not being empty! fix this by "
                       "removing manually!");
        throw CrashStates::PURGE_FAILED;

      case Vfs::Directory_service::UNLINK_ERR_NO_PERM:
        Genode::error (
            "could not delete: \"", Genode::Cstring (location),
            "\" due to lack of permission! fix this by removing manually!");
        throw CrashStates::PURGE_FAILED;

      default:
        if (config.verbose)
          Genode::log ("deleted: \"", location, "\"");
        break;
      }

    remove_basename (_location);
    char *parent_dir = _location;

    if (_location[0])
      {
        if (__num_dirent (Genode::Cstring (parent_dir)) < 1)
          __delete_upwards (parent_dir);
      }
  }

  void
  Main::__purge_zombies (const Genode::String<Vfs::MAX_PATH_LEN> &dir)
  {
    Genode::Directory cur_dir (snapper_root, dir);

    cur_dir.for_each_entry ([&] (const Genode::Directory::Entry &entry) {
      Genode::String<Vfs::MAX_PATH_LEN> entry_path (dir, "/", entry.name ());

      // for each directory recurse
      if (snapper_root.directory_exists (entry_path))
        {
          __purge_zombies (entry_path);
        }

      // for each file: check if file appears in valid generations
      if (snapper_root.file_exists (entry_path))
        {
          bool is_needed = false;

          // check each valid generation
          snapper_root.for_each_entry (
              [&] (const Genode::Directory::Entry &gen) {
                if (__valid_archive (
                        Genode::Directory::join (gen.name (), "archive")))
                  {
                    Genode::Directory gen_dir (snapper_root, gen.name ());
                    Genode::Readonly_file archive_file (gen_dir, "archive");

                    // check if backlink is present in a valid generation
                    if (Snapper::Archive::archive_file_contains_backlink (
                            archive_file, entry_path))
                      {
                        is_needed = true;
                      }
                  }

                // INFO If the entry is needed in at least one other
                // generation, we can stop the search.
                if (is_needed)
                  return;
              });

          // delete the entry if it's not needed (i.e. it's a zombie)
          if (!is_needed)
            {
              if (config.verbose)
                {
                  Genode::log ("removing zombie file", entry_path);
                  __delete_upwards (entry_path.string ());
                }
            }
        }
    });
  }
} // namespace Snapper
