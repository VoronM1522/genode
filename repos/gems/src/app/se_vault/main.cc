/*
 * \brief  Graphical front end for controlling Tresor devices
 * \author Martin Stein
 * \author Norman Feske
 * \date   2021-02-24
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/component.h>
#include <base/attached_rom_dataspace.h>
#include <base/attached_ram_dataspace.h>
#include <base/buffered_output.h>
#include <base/session_object.h>
#include <os/buffered_xml.h>
#include <os/vfs.h>
#include <os/reporter.h>
#include <timer_session/connection.h>
#include <report_session/report_session.h>

/* local includes */
#include <child_state.h>
#include <sandbox.h>


using namespace File_vault;

using Service_name = String<64>;

static bool has_name(Xml_node const &node, Node_name const &name) {
	return node.attribute_value("name", Node_name { }) == name; }


static void with_child(Xml_node const &init_state, Child_state const &child_state, auto const &fn)
{
	bool done = false;
	init_state.for_each_sub_node("child", [&] (Xml_node const &child) {
		if (!done && has_name(child, child_state.start_name())) {
			fn(child);
			done = true; } });
}


static void with_exit_code(Child_state const &child_state, Xml_node const &init_state, auto const &fn)
{
	bool exists = false, exited = false;
	int code = 0;
	with_child(init_state, child_state, [&] (Xml_node const &child) {
		exists = true;
		if (child.has_attribute("exited")) {
			exited = true;
			code = child.attribute_value("exited", (int)0L); } });

	if (exited)
		fn(code);
}


static bool child_succeeded(Child_state const &child, Xml_node const &sandbox)
{
	bool result = false;
	with_exit_code(child, sandbox, [&] (int code) {
		ASSERT(!code);
		result = true; });
	return result;
}

struct Main : Sandbox::Local_service_base::Wakeup, Sandbox::State_handler
{
	static constexpr char const *DEPRECATED_IMAGE_NAME = "cbe.img";
	bool _had_clients {  false };

	enum State {
		INVALID, SETUP_INIT_TRUST_ANCHOR, SETUP_TRESOR_INIT,
		SETUP_START_TRESOR, E2FSCK, SETUP_MKE2FS, LOCKED, UNLOCK_INIT_TRUST_ANCHOR,
		UNLOCK_START_TRESOR, UNLOCKED, START_LOCKING, STOP_SYSTEM_VFS, UNMOUNT1, UNMOUNT2, SETUP_INIT_FLAG, CHECK_INIT_FLAG
	};

	Env &env;
	State state { INVALID };
	Heap heap { env.ram(), env.rm() };
	Timer::Connection timer { env };
	Attached_rom_dataspace config_rom { env, "config" };
	bool jent_avail { config_rom.xml().attribute_value("jitterentropy_available", true) };
	bool fsck_apply { config_rom.xml().attribute_value("fsck_apply", true) };
	Root_directory vfs { env, heap, config_rom.xml().sub_node("vfs") };
	Registry<Child_state> children { };
	Child_state mke2fs { children, "mke2fs", Ram_quota { 32 * 1024 * 1024 }, Cap_quota { 300 } };
	Child_state tresor_vfs { children, "se_tresor_vfs", "vfs", Ram_quota { 512 * 1024 * 1024 }, Cap_quota { 200 } };
	Child_state tresor_trust_anchor_vfs { children, "se_tresor_trust_anchor_vfs", "vfs", Ram_quota { 256 * 1024 * 1024 }, Cap_quota { 200 } };
	Child_state system_vfs { children, "system_vfs", "vfs", Ram_quota { 512 * 1024 * 1024 }, Cap_quota { 200 } };
	Child_state sync_to_tresor_vfs_init { children, "sync_to_tresor_vfs_init", "se_sync_to_tresor_vfs_init", Ram_quota { 256 * 1024 * 1024 }, Cap_quota { 100 } };
	Child_state truncate_file { children, "truncate_file", "se_truncate_file", Ram_quota { 4 * 1024 * 1024 }, Cap_quota { 100 } };
	Child_state setup_init_flag { children, "setup_init_flag", Ram_quota { 4 * 1024 * 1024 }, Cap_quota { 100 } };
	Child_state tresor_vfs_block { children, "vfs_block", Ram_quota { 256 * 1024 * 1024 }, Cap_quota { 100 } };
	Child_state e2fsck { children, "e2fsck", Ram_quota { 32 * 1024 * 1024 }, Cap_quota { 300 } };
	Child_state tresor_init_trust_anchor { children, "se_tresor_init_trust_anchor", Ram_quota { 256 * 1024 * 1024 }, Cap_quota { 300 } };
	Child_state tresor_init { children, "se_tresor_init", Ram_quota { 256 * 1024 * 1024 }, Cap_quota { 200 } };
	Child_state check_init_flag { children, "check_init_flag", Ram_quota { 4 * 1024 * 1024 }, Cap_quota { 100 } };
	Child_state lock_fs_tool { children, "lock_fs_tool", "fs_tool", Ram_quota { 6 * 1024 * 1024 }, Cap_quota { 200 } };
	Sandbox sandbox { env, *this };
	Signal_handler<Main> state_handler { env.ep(), *this, &Main::update_sandbox_config };
	Passphrase passphrase = "P53ud0_Pa55w0rd";
	File_path init_file_name = "/initialized";

	mutable Timer::One_shot_timeout<Main> pause_timeout {
		timer, *this, &Main::handle_pause_timeout };
		
	void handle_pause_timeout(Duration) {
		set_state(UNMOUNT2);
		update_sandbox_config();
	}

	void generate_sandbox_config(Xml_generator &) const;

	void update_sandbox_config()
	{
		Buffered_xml config { heap, "config", [&] (Xml_generator &xml) { generate_sandbox_config(xml); } };
		sandbox.apply_config(config.xml);
	}

	void set_state(State new_state)
	{
		state = new_state;
	}

	void wakeup_local_service() override;

	void handle_sandbox_state() override;

	Main(Env &env) : env(env) {
		set_state(CHECK_INIT_FLAG);
		update_sandbox_config();
	}
};

void Main::handle_sandbox_state()
{
	Buffered_xml sandbox_state(heap, "sandbox_state", [&] (Xml_generator &xml) { sandbox.generate_state_report(xml); });
	bool update_sandbox_cfg { false };
	Number_of_clients num_clients { 0 };

	switch (state) {
	case CHECK_INIT_FLAG:
		with_exit_code(check_init_flag, sandbox_state.xml, [&] (int code) {

			if (code == 0) {
				set_state(UNLOCK_INIT_TRUST_ANCHOR);
			} else if (code == 1) {
				set_state(SETUP_INIT_TRUST_ANCHOR);
			} else {
				set_state(INVALID);
			}

			update_sandbox_cfg = true;
		});

		break;

	case E2FSCK:
		with_exit_code(e2fsck, sandbox_state.xml, [&] (int code) {

			if (code == 0) {
				set_state(UNLOCKED);
			} else if (code == 1 || code == 2) {
				set_state(UNMOUNT1);
			} else {
				set_state(INVALID);
			}

			update_sandbox_cfg = true;
			});

		break;

		case UNMOUNT1:
			break;
	
		case UNMOUNT2:
		if (child_succeeded(system_vfs, sandbox_state.xml)) {
			set_state(E2FSCK);
			update_sandbox_cfg = true;
		}

		break;
	case SETUP_INIT_TRUST_ANCHOR:
		if (child_succeeded(tresor_init_trust_anchor, sandbox_state.xml)) {
			set_state(SETUP_TRESOR_INIT);
			update_sandbox_cfg = true;
		}
		
		break;

	case SETUP_INIT_FLAG:
		if (child_succeeded(setup_init_flag, sandbox_state.xml)) {
			set_state(UNLOCKED);
			update_sandbox_cfg = true;
		}
		
		break;

	case UNLOCK_INIT_TRUST_ANCHOR:
	{
		if (child_succeeded(tresor_init_trust_anchor, sandbox_state.xml)) {
			set_state(UNLOCK_START_TRESOR);
			update_sandbox_cfg = true;
		}

		break;
	}
	case SETUP_TRESOR_INIT:
		if (child_succeeded(tresor_init, sandbox_state.xml)) {
			set_state(SETUP_START_TRESOR);
			update_sandbox_cfg = true;
		}
		break;

	case SETUP_START_TRESOR:
		if (child_succeeded(sync_to_tresor_vfs_init, sandbox_state.xml)) {
			set_state(SETUP_MKE2FS);
			update_sandbox_cfg = true;
		}
		break;

	case UNLOCK_START_TRESOR:		
		if (child_succeeded(sync_to_tresor_vfs_init, sandbox_state.xml)) {
			if (fsck_apply) {
				set_state(E2FSCK);
			} else {
				set_state(UNLOCKED);
			}
			
			update_sandbox_cfg = true;
		}
		
		break;
	case SETUP_MKE2FS:
		if (child_succeeded(mke2fs, sandbox_state.xml)) {
			set_state(SETUP_INIT_FLAG);
			update_sandbox_cfg = true;
		}
		break;

	case UNLOCKED:
		with_child(sandbox_state.xml, system_vfs, [&] (Xml_node const &child) {
			child.with_optional_sub_node("provided", [&] (Xml_node const &provided) {
				provided.for_each_sub_node("session", [&] (Xml_node const &session) {
					if (session.attribute_value("service", Service_name()) == "File_system")
						num_clients.value++; }); }); });

		if (num_clients.value > 0) {
			_had_clients = true;
		}
          
		if (child_succeeded(system_vfs, sandbox_state.xml)) {
			set_state(START_LOCKING);
			update_sandbox_cfg = true;
		} else if (_had_clients && !num_clients.value) {
			set_state(STOP_SYSTEM_VFS);
			update_sandbox_cfg = true;
		}


		break;
	case STOP_SYSTEM_VFS:
		if (child_succeeded(system_vfs, sandbox_state.xml)) {
			set_state(START_LOCKING);
			update_sandbox_cfg = true;
		}

		break;
    	
	case START_LOCKING:
		if (child_succeeded(lock_fs_tool, sandbox_state.xml)) {
			set_state(LOCKED);
			update_sandbox_cfg = true;
		}
		break;

	default: break;
	}
	sandbox_state.xml.for_each_sub_node("child", [&] (Xml_node const &child) {
		children.for_each([&] (Child_state &child_state) {
			if (child_state.apply_child_state_report(child))
				update_sandbox_cfg = true; }); });
	if (update_sandbox_cfg)
		update_sandbox_config();
}


void Main::wakeup_local_service()
{}

void Main::generate_sandbox_config(Xml_generator &xml) const
{
	switch (state) {
	case INVALID:
		error("se_vault state: INVALID");
		env.parent().exit(1);
		break;

	case LOCKED:
		env.parent().exit(0);
		break;

	case CHECK_INIT_FLAG:
		gen_parent_provides_and_report_nodes(xml);
		gen_tresor_trust_anchor_vfs_start_node(xml, tresor_trust_anchor_vfs, jent_avail);
		gen_check_init_flag_start_node(xml, check_init_flag, File_path(init_file_name).string());
		break;

	case SETUP_INIT_TRUST_ANCHOR:

		gen_parent_provides_and_report_nodes(xml);
		gen_tresor_trust_anchor_vfs_start_node(xml, tresor_trust_anchor_vfs, jent_avail);
		gen_tresor_init_trust_anchor_start_node(xml, tresor_init_trust_anchor, passphrase);
		break;

	case UNLOCK_INIT_TRUST_ANCHOR:

		gen_parent_provides_and_report_nodes(xml);
		gen_tresor_trust_anchor_vfs_start_node(xml, tresor_trust_anchor_vfs, jent_avail);
		gen_tresor_init_trust_anchor_start_node( xml, tresor_init_trust_anchor, passphrase);
		break;

	case UNLOCK_START_TRESOR:
		gen_parent_provides_and_report_nodes(xml);
		gen_tresor_trust_anchor_vfs_start_node(xml, tresor_trust_anchor_vfs, jent_avail);
		gen_tresor_vfs_start_node(xml, tresor_vfs);
		gen_sync_to_tresor_vfs_init_start_node(xml, sync_to_tresor_vfs_init);
		break;

	case E2FSCK:
		gen_parent_provides_and_report_nodes(xml);
		gen_tresor_trust_anchor_vfs_start_node(xml, tresor_trust_anchor_vfs, jent_avail);
		gen_tresor_vfs_start_node(xml, tresor_vfs);
		gen_tresor_vfs_block_start_node(xml, tresor_vfs_block);	
		gen_e2fsck_start_node(xml, e2fsck);
		break;

	case UNMOUNT1:
		if (!pause_timeout.scheduled())
			pause_timeout.schedule(Microseconds { 3'000'000 });
		gen_parent_provides_and_report_nodes(xml);
		gen_tresor_trust_anchor_vfs_start_node(xml, tresor_trust_anchor_vfs, jent_avail);
		gen_tresor_vfs_start_node(xml, tresor_vfs);
		gen_tresor_vfs_block_start_node(xml, tresor_vfs_block);
		gen_system_vfs_start_node(xml, system_vfs, false);

		break;

	case UNMOUNT2:
		gen_parent_provides_and_report_nodes(xml);
		gen_tresor_trust_anchor_vfs_start_node(xml, tresor_trust_anchor_vfs, jent_avail);
		gen_tresor_vfs_start_node(xml, tresor_vfs);
		gen_tresor_vfs_block_start_node(xml, tresor_vfs_block);	
		gen_system_vfs_start_node(xml, system_vfs, true);

		break;
	
	case SETUP_TRESOR_INIT:
	{
		Tresor::Superblock_configuration sb_config {
			Tree_configuration(TRESOR_VBD_MAX_LVL, TRESOR_VBD_DEGREE, tresor_tree_num_leaves(CLIENT_FS_SIZE)),
			Tree_configuration(TRESOR_FREE_TREE_MAX_LVL, TRESOR_FREE_TREE_DEGREE, tresor_tree_num_leaves(min_journal_buf(CLIENT_FS_SIZE) * 5)) 
		};
		gen_parent_provides_and_report_nodes(xml);
		gen_tresor_trust_anchor_vfs_start_node(xml, tresor_trust_anchor_vfs, jent_avail);
		gen_tresor_init_start_node(xml, tresor_init, sb_config);
		break;
	}
	case SETUP_START_TRESOR:

		gen_parent_provides_and_report_nodes(xml);
		gen_tresor_trust_anchor_vfs_start_node(xml, tresor_trust_anchor_vfs, jent_avail);
		gen_tresor_vfs_start_node(xml, tresor_vfs);
		gen_sync_to_tresor_vfs_init_start_node(xml, sync_to_tresor_vfs_init);
		break;

	case SETUP_MKE2FS:
		gen_parent_provides_and_report_nodes(xml);
		gen_tresor_trust_anchor_vfs_start_node(xml, tresor_trust_anchor_vfs, jent_avail);
		gen_tresor_vfs_start_node(xml, tresor_vfs);
		gen_tresor_vfs_block_start_node(xml, tresor_vfs_block);	
		gen_mke2fs_start_node(xml, mke2fs);
		break;

	case SETUP_INIT_FLAG:
		gen_parent_provides_and_report_nodes(xml);
		gen_tresor_trust_anchor_vfs_start_node(xml, tresor_trust_anchor_vfs, jent_avail);
		gen_tresor_vfs_start_node(xml, tresor_vfs);
		gen_tresor_vfs_block_start_node(xml, tresor_vfs_block);	
		gen_setup_init_flag_start_node(xml, setup_init_flag, File_path(init_file_name).string());
		break;


	case UNLOCKED:
		gen_parent_provides_and_report_nodes(xml);
		gen_tresor_trust_anchor_vfs_start_node(xml, tresor_trust_anchor_vfs, jent_avail);
		gen_tresor_vfs_start_node(xml, tresor_vfs);
		gen_tresor_vfs_block_start_node(xml, tresor_vfs_block);

		gen_child_service_policy(xml, "File_system", system_vfs);
		gen_system_vfs_start_node(xml, system_vfs);
		break;

	case STOP_SYSTEM_VFS:
		gen_parent_provides_and_report_nodes(xml);
		gen_tresor_trust_anchor_vfs_start_node(xml, tresor_trust_anchor_vfs, jent_avail);
		gen_tresor_vfs_start_node(xml, tresor_vfs);
		gen_tresor_vfs_block_start_node(xml, tresor_vfs_block);
		gen_system_vfs_start_node(xml, system_vfs, true);
		break;


	case START_LOCKING:

		gen_parent_provides_and_report_nodes(xml);
		gen_tresor_trust_anchor_vfs_start_node(xml, tresor_trust_anchor_vfs, jent_avail);
		gen_tresor_vfs_start_node(xml, tresor_vfs);
		gen_tresor_vfs_block_start_node(xml, tresor_vfs_block);
		gen_lock_fs_tool_start_node(xml, lock_fs_tool);
		break;
	}
}

void Component::construct(Genode::Env &env) {
	static Main main { env };
}
