#include <base/allocator.h>
#include <util/construct_at.h>
#include <vfs/directory_service.h>
#include <vfs/vfs_handle.h>

#include "xxhash32.h"
#include "snapper.h"

Snapper::Archive::Archive (Genode::Heap &heap, Genode::Directory &snapper_root,
                           bool verbose)
    : archive (), heap (heap), snapper_root (snapper_root), verbose (verbose)
{
}

Snapper::Archive::~Archive ()
{
  while (true)
    {
      bool not_empty = archive.with_any_element (
          [this] (const Archive::ArchiveEntry &entry) {
            remove (entry.name);
          });

      if (!not_empty)
        break;
    }

  total_backlinks = 0;
}

void
Snapper::Archive::insert (const Archive::ArchiveKey key,
                          const Genode::String<Vfs::MAX_PATH_LEN> &val)
{
  Snapper::Archive::Backlink *backlink
      = new (heap) Archive::Backlink (heap, snapper_root, verbose, val);

  backlink->_self = backlink;

  archive.with_element (
      key,
      [backlink] (Archive::ArchiveEntry &entry) {
        entry.queue.enqueue (*backlink);
      },
      [this, key, backlink] () {
        Archive::ArchiveEntry *entry
            = new (heap) Archive::ArchiveEntry (key, archive);

        entry->_self = entry;

        entry->queue.enqueue (*backlink);
      });

  total_backlinks++;

  if (verbose)
    {
      Genode::log ("archive entry inserted: ", key, " -> \"", backlink->value,
                   "\"");
    }
}

void
Snapper::Archive::commit (Genode::Directory &dir,
                          const Genode::String<Vfs::MAX_PATH_LEN> &file)
{
  if (dir.file_exists (file))
    {
      Genode::error (
          "archive commit to already existing file is not allowed!");
      throw Snapper::CrashStates::SNAPSHOT_NOT_POSSIBLE;
    }

  try
    {
      Genode::New_file archive_file (dir, file);

      constexpr Genode::size_t key_size = sizeof (Archive::ArchiveKey);

      constexpr Genode::size_t val_size
          = sizeof (decltype (Archive::Backlink::value));

      constexpr Genode::size_t kv_pair_size = key_size + val_size;

      const Genode::size_t archive_data_size = total_backlinks * kv_pair_size;
      char *archive_data_buf = new (heap) char[archive_data_size];

      Genode::uint64_t idx = 0;

      archive.for_each ([&] (const Archive::ArchiveEntry &entry) {
        entry.queue.for_each ([archive_data_buf, &idx,
                               &entry] (const Archive::Backlink &backlink) {
          Genode::memcpy (archive_data_buf + (idx * kv_pair_size), &entry.name,
                          key_size);

          Genode::memcpy (archive_data_buf + (idx * kv_pair_size + key_size),
                          backlink.value.string (), val_size);

          idx++;
        });
      });

      const Genode::size_t archive_buf_size
          = sizeof (Snapper::VERSION) + sizeof (Snapper::HASH)
            + sizeof (decltype (total_backlinks)) + archive_data_size;

      if (archive_buf_size == 0) {
          Genode::error ("archive has an invalid size!");
          throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
      }

      char *archive_buf = new (heap) char[archive_buf_size];

      Snapper::VERSION ver = Version;
      Snapper::HASH hash = xxhash32 (archive_data_buf, archive_data_size);

      Genode::memcpy (archive_buf, &ver, sizeof (Snapper::VERSION));

      Genode::memcpy (archive_buf + sizeof (Snapper::VERSION), &hash,
                      sizeof (Snapper::HASH));

      Genode::memcpy (archive_buf + sizeof (Snapper::VERSION)
                          + sizeof (Snapper::HASH),
                      &total_backlinks, sizeof (decltype (total_backlinks)));

      Genode::memcpy (archive_buf + sizeof (Snapper::VERSION)
                          + sizeof (Snapper::HASH)
                          + sizeof (decltype (total_backlinks)),
                      archive_data_buf, archive_data_size);

      heap.free (archive_data_buf, archive_data_size);

      Genode::New_file::Append_result res
          = archive_file.append (archive_buf, archive_buf_size);

      heap.free (archive_buf, archive_buf_size);

      if (res != Genode::New_file::Append_result::OK)
        {
          Genode::error ("failed to write to the archive file!");
          throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
        }
    }
  catch (Genode::New_file::Create_failed)
    {
      Genode::error ("failed to create archive file!");
      throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
    }
  catch (Genode::Out_of_ram)
    {
      Genode::error ("snapper is out of RAM!");
      throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
    }
  catch (Genode::Out_of_caps)
    {
      Genode::error ("snapper is out of capabilities!");
      throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
    }
  catch (Genode::Denied)
    {
      Genode::error ("memory allocation denied!");
      throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
    }
}

void
Snapper::Archive::remove (const ArchiveKey key)
{
  archive.with_element (
      key,
      [this] (Archive::ArchiveEntry &entry) {
        entry.queue.for_each ([this] (Archive::Backlink &backlink) {
          total_backlinks--;
          Genode::destroy (heap, backlink._self);
        });

        Genode::destroy (heap, entry._self);
      },
      [this, key] () {
        if (verbose)
          {
            Genode::warning ("no such key exists in archive: ", key);
          }
      });

  if (verbose)
    {
      Genode::log ("archive entry removed: ", key);
    }
}

/**
 * @brief Helper interator to go through each key-value pair in an
 * archive file and perform an operation fn().
 */
static void
__for_each_pair_in_archive_file (const Genode::Readonly_file &archive_file,
                                 auto const &fn)
{
  Genode::Readonly_file::At pos{ sizeof (Snapper::VERSION)
                                 + sizeof (Snapper::HASH) };

  char _num_backlinks_buf[sizeof (
      decltype (Snapper::Archive::total_backlinks))];
  Genode::Byte_range_ptr num_backlinks_buf (_num_backlinks_buf,
                                            sizeof (_num_backlinks_buf));

  if (archive_file.read (pos, num_backlinks_buf) == 0)
    {
      Genode::error ("missing number of backlinks in the archive file");
      throw Snapper::CrashStates::INVALID_ARCHIVE_FILE;
    }

  pos.value += sizeof (decltype (Snapper::Archive::total_backlinks));

  decltype (Snapper::Archive::total_backlinks) num_backlinks
      = *(reinterpret_cast<decltype (Snapper::Archive::total_backlinks) *> (
          _num_backlinks_buf));

  char _key_buf[sizeof (Snapper::Archive::ArchiveKey)];
  char _val_buf[sizeof (decltype (Snapper::Archive::Backlink::value))];

  Genode::Byte_range_ptr key_buf (_key_buf, sizeof (_key_buf));
  Genode::Byte_range_ptr val_buf (_val_buf, sizeof (_val_buf));

  for (decltype (Snapper::Archive::total_backlinks) i = 0; i < num_backlinks;
       i++)
    {
      Genode::size_t bytes_read = archive_file.read (pos, key_buf);
      if (bytes_read != key_buf.num_bytes)
        {
          Genode::error ("invalid archive file: invalid key size!");
          throw Snapper::CrashStates::INVALID_ARCHIVE_FILE;
        }

      pos.value += bytes_read;

      bytes_read = archive_file.read (pos, val_buf);
      if (bytes_read != val_buf.num_bytes)
        {
          Genode::error ("invalid archive file: invalid value size!");
          throw Snapper::CrashStates::INVALID_ARCHIVE_FILE;
        }

      pos.value += bytes_read;

      Snapper::Archive::ArchiveKey key = *(
          reinterpret_cast<Snapper::Archive::ArchiveKey *> (key_buf.start));

      Genode::Cstring val (val_buf.start);
      fn (key, val);
    }
}

void
Snapper::Archive::extract_from_archive_file (
    const Genode::Readonly_file &archive_file)
{
  __for_each_pair_in_archive_file (
      archive_file,
      [this] (ArchiveKey key, const decltype (Archive::Backlink::value)
                                  & val) { insert (key, val); });
}

bool
Snapper::Archive::archive_file_contains_backlink (
    const Genode::Readonly_file &archive_file,
    const decltype (Backlink::value) & search_val)
{
  Genode::Readonly_file::At pos{ sizeof (Snapper::VERSION)
                                 + sizeof (Snapper::HASH) };

  char _num_backlinks_buf[sizeof (
      decltype (Snapper::Archive::total_backlinks))];
  Genode::Byte_range_ptr num_backlinks_buf (_num_backlinks_buf,
                                            sizeof (_num_backlinks_buf));

  if (archive_file.read (pos, num_backlinks_buf) == 0)
    {
      Genode::error ("missing number of backlinks in the archive file");
      throw Snapper::CrashStates::INVALID_ARCHIVE_FILE;
    }

  pos.value += sizeof (decltype (Snapper::Archive::total_backlinks));

  decltype (Snapper::Archive::total_backlinks) num_backlinks
      = *(reinterpret_cast<decltype (Snapper::Archive::total_backlinks) *> (
          _num_backlinks_buf));

  char _val_buf[sizeof (decltype (Snapper::Archive::Backlink::value))];

  Genode::Byte_range_ptr val_buf (_val_buf, sizeof (_val_buf));

  for (decltype (Snapper::Archive::total_backlinks) i = 0; i < num_backlinks;
       i++)
    {
      pos.value += sizeof (Snapper::Archive::ArchiveKey);

      Genode::size_t bytes_read = archive_file.read (pos, val_buf);
      if (bytes_read != val_buf.num_bytes)
        {
          Genode::error ("invalid archive file: invalid value size!");
          throw Snapper::CrashStates::INVALID_ARCHIVE_FILE;
        }

      pos.value += bytes_read;

      decltype (Snapper::Archive::Backlink::value)
          val (Genode::Cstring (val_buf.start));

      if (val == search_val)
        {
          return true;
        }
    }

  return false;
}
