/*
 * \brief  Small utility for truncating a given file
 * \author Martin Stein
 * \date   2021-03-19
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/component.h>
#include <base/attached_rom_dataspace.h>
#include <base/heap.h>
#include <os/vfs.h>
#include <se_vault/types.h>

using namespace Genode;

struct Main
{
	Env &env;
	Heap heap { env.ram(), env.rm() };
	Attached_rom_dataspace config { env, "config" };
	Root_directory vfs { env, heap, config.xml().sub_node("vfs") };
	Vfs::File_system &fs { vfs.root_dir() };
	Directory::Path path { config.xml().attribute_value("path", Directory::Path { }) };
	// Number_of_bytes size { config.xml().attribute_value("size", Number_of_bytes { }) };
	// Directory::Path init_path { File_vault::File_path(path.string(), ".init").string() };

	Main(Env &env) : env(env)
	{
		// bool initialized = false;
		// unsigned mode = Vfs::Directory_service::OPEN_MODE_RDWR; // OPEN_MODE_WRONLY;
		Vfs::Directory_service::Stat stat { };

		// Check initialization
		if (fs.stat(path.string(), stat) == Vfs::Directory_service::STAT_OK) {
			// initialized = true;
			log("Initialized");
			env.parent().exit(0);
		} else {
			log("Not initialized");
			env.parent().exit(1);
		}

		// if (fs.stat(path.string(), stat) != Vfs::Directory_service::STAT_OK) {
		// 	mode |= Vfs::Directory_service::OPEN_MODE_CREATE;
		// } else if (stat.size >= size) {
		// 	// log("File ", path, " exists, size: ", stat.size);
		// 	if (initialized) {
		// 		env.parent().exit(0);
		// 	} else {
		// 		env.parent().exit(1);
		// 	}			
		// } else if (initialized) {
		// 	fs.unlink(init_path.string());
		// 	log("Removed init marker");
		// }
		// // else {
		// // 	log("Create file, size: ", size);
		// // }

		// Vfs::Vfs_handle *handle_ptr = nullptr;
		// auto res = fs.open(path.string(), mode, &handle_ptr, heap);

		// if (res != Vfs::Directory_service::OPEN_OK || (handle_ptr == nullptr)) {
		// 	// log("res: ", (int)res, "; handle_ptr: ", handle_ptr);
		// 	error("failed to create file '", path, "'");
		// 	env.parent().exit(-1);
		// }

		// auto truncate_res = handle_ptr->fs().ftruncate(handle_ptr, size);
		// log("AAA");

		// // Проверить case'ы
		// switch (truncate_res) {
		// case Vfs::File_io_service::FTRUNCATE_OK:
		// 	log("FTRUNCATE_OK");
		// 	if (fs.stat(path.string(), stat) != Vfs::Directory_service::STAT_OK) {
		// 		log("Open check failed");
		// 		env.parent().exit(-1);
		// 	} else if (stat.size < size) {
		// 		log("Size check failed");
		// 		env.parent().exit(-1);
		// 	} else {
		// 		log("Open and size checks passed");
		// 	}
		// 	break;
		// case Vfs::File_io_service::FTRUNCATE_ERR_NO_SPACE:
		// 	error("no space left");
		// 	env.parent().exit(-1);
		// case Vfs::File_io_service::FTRUNCATE_ERR_NO_PERM:
		// 	error("permission denied");
		// 	env.parent().exit(-1);
		// case Vfs::File_io_service::FTRUNCATE_ERR_INTERRUPT:
		// 	error("interrupted");
		// 	env.parent().exit(-1);
		// }

		// handle_ptr->ds().close(handle_ptr);
		// env.parent().exit(1);
	}
};

void Component::construct(Env &env) { static Main main(env); }
