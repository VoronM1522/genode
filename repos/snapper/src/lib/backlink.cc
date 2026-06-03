#include <base/allocator.h>
#include <util/construct_at.h>
#include <vfs/directory_service.h>
#include <vfs/vfs_handle.h>

#include "xxhash32.h"
#include "snapper.h"

namespace Snapper
{
  Genode::Attempt<Snapper::VERSION, Snapper::Archive::Backlink::Error>
  Snapper::Archive::Backlink::get_version (void)
  {
    Snapper::VERSION version = 0;

    if (!snapper_root.file_exists (value))
      {
        return Genode::Attempt<Snapper::VERSION,
                               Snapper::Archive::Backlink::Error> (OpenErr);
      }

    try
      {
        Genode::Readonly_file reader (snapper_root, value);
        Genode::Readonly_file::At pos{ 0 };

        char _version_buf[sizeof (Snapper::VERSION)];
        Genode::Byte_range_ptr version_buf (_version_buf,
                                            sizeof (Snapper::VERSION));

        if (reader.read (pos, version_buf) == 0)
          {
            Genode::error ("backlink missing version: ", value);

            return Genode::Attempt<Snapper::VERSION,
                                   Snapper::Archive::Backlink::Error> (
                MissingFieldErr);
          }

        version = *(reinterpret_cast<Snapper::VERSION *> (version_buf.start));
      }
    catch (Genode::Readonly_file::Open_failed)
      {
        return Genode::Attempt<Snapper::VERSION,
                               Snapper::Archive::Backlink::Error> (OpenErr);
      }

    return Genode::Attempt<Snapper::VERSION,
                           Snapper::Archive::Backlink::Error> (version);
  }

  Genode::Attempt<Snapper::HASH, Snapper::Archive::Backlink::Error>
  Snapper::Archive::Backlink::get_integrity (void)
  {
    Snapper::HASH hash = 0;

    if (!snapper_root.file_exists (value))
      {
        return Genode::Attempt<Snapper::HASH,
                               Snapper::Archive::Backlink::Error> (OpenErr);
      }

    try
      {
        Genode::Readonly_file reader (snapper_root, value);
        Genode::Readonly_file::At pos{ sizeof (Snapper::VERSION) };

        char _hash_buf[sizeof (Snapper::HASH)];
        Genode::Byte_range_ptr hash_buf (_hash_buf, sizeof (Snapper::HASH));

        if (reader.read (pos, hash_buf) == 0)
          {
            Genode::error ("backlink missing HASH: ", value);

            return Genode::Attempt<Snapper::HASH,
                                   Snapper::Archive::Backlink::Error> (
                MissingFieldErr);
          }

        hash = *(reinterpret_cast<Snapper::HASH *> (hash_buf.start));
      }
    catch (Genode::Readonly_file::Open_failed)
      {
        return Genode::Attempt<Snapper::HASH,
                               Snapper::Archive::Backlink::Error> (OpenErr);
      }

    return Genode::Attempt<Snapper::HASH, Snapper::Archive::Backlink::Error> (
        hash);
  }

  Genode::Attempt<Snapper::RC, Snapper::Archive::Backlink::Error>
  Snapper::Archive::Backlink::get_reference_count (void)
  {
    Snapper::RC reference_count = 0;

    if (!snapper_root.file_exists (value))
      {
        return Genode::Attempt<Snapper::RC,
                               Snapper::Archive::Backlink::Error> (OpenErr);
      }

    try
      {
        Genode::Readonly_file reader (snapper_root, value);
        Genode::Readonly_file::At pos{ sizeof (Snapper::VERSION)
                                       + sizeof (Snapper::HASH) };

        char _rc_buf[sizeof (Snapper::RC)];
        Genode::Byte_range_ptr rc_buf (_rc_buf, sizeof (Snapper::RC));

        if (reader.read (pos, rc_buf) == 0)
          {
            Genode::error ("backlink missing reference count: ", value);
            return Genode::Attempt<Snapper::RC,
                                   Snapper::Archive::Backlink::Error> (
                MissingFieldErr);
          }

        reference_count = *(reinterpret_cast<Snapper::RC *> (rc_buf.start));
      }
    catch (Genode::Readonly_file::Open_failed)
      {
        Genode::error ("could not open backlink: ", value);
        return Genode::Attempt<Snapper::RC,
                               Snapper::Archive::Backlink::Error> (OpenErr);
      }

    return Genode::Attempt<Snapper::RC, Snapper::Archive::Backlink::Error> (
        reference_count);
  }

  Genode::Attempt<Genode::size_t, Snapper::Archive::Backlink::Error>
  Snapper::Archive::Backlink::get_data_size (void)
  {
    Vfs::file_size fsize = 0;

    if (!snapper_root.file_exists (value))
      {
        return Genode::Attempt<Genode::size_t,
                               Snapper::Archive::Backlink::Error> (OpenErr);
      }

    try
      {
        fsize = snapper_root.file_size (value);
      }
    catch (Genode::Directory::Nonexistent_file)
      {
        Genode::error ("could not data size of nonexistent file: ", value);
        return Genode::Attempt<Genode::size_t,
                               Snapper::Archive::Backlink::Error> (
            Snapper::Archive::Backlink::StatsErr);
      }

    Genode::size_t size = fsize - sizeof (Snapper::VERSION)
                          - sizeof (Snapper::HASH) - sizeof (Snapper::RC);

    if (size == 0)
      return Genode::Attempt<Genode::size_t, Snapper::Archive::Backlink::Error>(InsufficientSizeErr);

    return Genode::Attempt<Genode::size_t, Snapper::Archive::Backlink::Error> (
        size);
  }

  Snapper::Archive::Backlink::Error
  Snapper::Archive::Backlink::get_data (Genode::Byte_range_ptr &data)
  {
    // INFO Do not use Backlink::is_valid_backlink() here, as the
    // result from the call to Backlink::get_integrity() is used
    // to return the data buffer.
    Snapper::Archive::Backlink::Error err = None;
    Snapper::HASH hash = 0;

    get_version ().with_result (
        [&] (Snapper::VERSION ver) {
          if (ver != Snapper::Version)
            {
              if (verbose)
                Genode::warning ("backlink has a wrong version: ", value);

              err = InvalidVersion;
            }
        },
        [&] (auto) {
          if (verbose)
            Genode::warning ("could not access backlink's version: ", value);

          err = InvalidVersion;
        });

    if (err != None)
      return err;

    get_integrity ().with_result (
        [&] (Snapper::HASH _hash) { hash = _hash; },
        [&] (Snapper::Archive::Backlink::Error) {
          if (verbose)
            {
              Genode::warning ("could not access backlink's hash: ", value,
                               "! Remove it to "
                               "not receive this warning again.");
            }
          err = InvalidIntegrity;
        });

    if (err != None)
      return err;

    get_data_size ().with_result (
        [&err, &data] (Genode::size_t size) {
          if (size > data.num_bytes)
            {
              Genode::error (
                  "insufficient buffer size to read from snapshot file!");

              err = InsufficientSizeErr;
              return;
            }
        },
        [&err] (Snapper::Archive::Backlink::Error e) { err = e; });

    if (err != None)
      {
        return err;
      }

    try
      {
        Genode::Readonly_file reader (snapper_root, value);
        Genode::Readonly_file::At pos{ sizeof (Snapper::VERSION)
                                       + sizeof (Snapper::HASH)
                                       + sizeof (Snapper::RC) };

        if (reader.read (pos, data) == 0)
          {
            Genode::error ("backlink missing data: ", value);
            return MissingFieldErr;
          }
      }
    catch (Genode::Readonly_file::Open_failed)
      {
        Genode::error ("could not open backlink: ", value);
        return OpenErr;
      }

    if (xxhash32 (data.start, data.num_bytes) != hash)
      {
        if (verbose)
          Genode::warning ("backlink has an invalid HASH: ", value,
                           "! Remove it to "
                           "not receive this warning again.");

        err = InvalidIntegrity;
        Genode::memset (data.start, 0, data.num_bytes);
      }

    return None;
  }

  Genode::Attempt<Snapper::RC, Snapper::Archive::Backlink::Error>
  Snapper::Archive::Backlink::set_reference_count (
      const Snapper::RC reference_count)
  {
    Genode::Attempt<Snapper::RC, Snapper::Archive::Backlink::Error> res (
        reference_count);

    Snapper::VERSION version = 0;
    Snapper::HASH hash = 0;

    get_version ().with_result (
        [&version] (Snapper::VERSION _version) { version = _version; },
        [&res] (Snapper::Archive::Backlink::Error err) {
          res = Genode::Attempt<Snapper::RC,
                                Snapper::Archive::Backlink::Error> (err);
        });

    if (res.failed ())
      {
        Genode::error ("couldn't update the reference count of: ", value);
        return res;
      }

    get_integrity ().with_result (
        [&hash] (Snapper::HASH _hash) { hash = _hash; },
        [&res] (Snapper::Archive::Backlink::Error err) {
          res = Genode::Attempt<Snapper::RC,
                                Snapper::Archive::Backlink::Error> (err);
        });

    if (res.failed ())
      {
        Genode::error ("couldn't update the reference count of: ", value);
        return res;
      }

    Genode::size_t data_size;

    get_data_size ().with_result (
        [&data_size] (Genode::size_t size) { data_size = size; },
        [&res] (Snapper::Archive::Backlink::Error err) {
          res = Genode::Attempt<Snapper::RC,
                                Snapper::Archive::Backlink::Error> (err);
        });

    if (res.failed ())
      {
        Genode::error ("couldn't update the reference count of: ", value);
        return res;
      }

    char *_data_buf = (char *)heap.alloc (data_size);
    Genode::Byte_range_ptr data (_data_buf, data_size);

    Snapper::Archive::Backlink::Error err = get_data (data);
    if (err != None)
      {
        res = Genode::Attempt<Snapper::RC, Snapper::Archive::Backlink::Error> (
            err);

        goto CLEAN_RET;
      }

    try
      {
        // INFO Genode::Append_file overwrites the file contents.
        Genode::Append_file writer (snapper_root, value);

        Genode::size_t _buf_size = sizeof (Snapper::VERSION)
                                   + sizeof (Snapper::HASH)
                                   + sizeof (Snapper::RC) + data.num_bytes;

        char *_buf = new (heap) char[_buf_size];

        Genode::memcpy (_buf, (char *)&version, sizeof (Snapper::VERSION));

        Genode::memcpy (_buf + sizeof (Snapper::VERSION), (char *)&hash,
                        sizeof (Snapper::HASH));

        Genode::memcpy (_buf + sizeof (Snapper::VERSION)
                            + sizeof (Snapper::HASH),
                        (char *)&reference_count, sizeof (Snapper::RC));

        Genode::memcpy (_buf + sizeof (Snapper::VERSION)
                            + sizeof (Snapper::HASH) + sizeof (Snapper::RC),
                        data.start, data.num_bytes);

        Genode::New_file::Append_result write_res
            = writer.append (_buf, _buf_size);

        heap.free (_buf, _buf_size);

        if (write_res != Genode::Append_file::Append_result::OK)
          {
            res = Genode::Attempt<Snapper::RC,
                                  Snapper::Archive::Backlink::Error> (
                WriteErr);

            goto CLEAN_RET;
          }
      }
    catch (Genode::Append_file::Create_failed)
      {
        res = Genode::Attempt<Snapper::RC, Snapper::Archive::Backlink::Error> (
            OpenErr);

        goto CLEAN_RET;
      }

  CLEAN_RET:
    if (data.start)
      heap.free (data.start, data.num_bytes);

    if (res == WriteErr)
      {
        Genode::error ("could not update reference count: ", value);
      }

    return res;
  }

  bool
  Snapper::Archive::Backlink::is_backlink_valid (Snapper::HASH hash)
  {
    bool is_backlink_valid = true;

    get_version ().with_result (
        [&] (Snapper::VERSION version) {
          if (version != Version)
            {
              if (verbose)
                Genode::log ("backlink has a version mismatch: ", value,
                             ". Creating a new snapshot file.");

              is_backlink_valid = false;
            }
        },
        [&] (Snapper::Archive::Backlink::Error) {
          is_backlink_valid = false;
        });

    if (!is_backlink_valid)
      return false;

    get_integrity ().with_result (
        [&] (Snapper::HASH file_hash) {
          if (file_hash != hash)
            {
              if (verbose)
                Genode::log ("backlink has a mismatching hash: ", value,
                             ". Creating new snapshot file.");

              is_backlink_valid = false;
            }
        },
        [&] (Snapper::Archive::Backlink::Error) {
          is_backlink_valid = false;
        });

    return is_backlink_valid;
  }
};
