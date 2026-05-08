/*
 * \brief  Local utilities for the sandbox API
 * \author Martin Stein
 * \date   2021-02-24
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SANDBOX_H_
#define _SANDBOX_H_

/* Genode includes */
#include <util/string.h>
#include <sandbox/sandbox.h>

namespace File_vault {

	void gen_arg(Xml_generator &xml, auto const &arg)
	{
		xml.node("arg", [&] { xml.attribute("value", arg); });
	}

	void gen_common_start_content(Xml_generator &xml, char const *name, Cap_quota caps, Ram_quota ram)
	{
		xml.attribute("name", name);
		xml.attribute("caps", caps.value);
		gen_named_node(xml, "resource", "RAM", [&] {
			xml.attribute("quantum", String<64>(Number_of_bytes(ram.value))); });
	}

	void gen_route(Xml_generator &xml, char const *service, char const *label, auto const &fn)
	{
		gen_named_node(xml, "service", service, [&] {
			if (strcmp(label, ""))
				xml.attribute("label", label);
			fn(); });
	}

	void gen_child_route(Xml_generator &xml, char const *child, char const *service, char const *label = "")
	{
		gen_route(xml, service, label, [&] { gen_named_node(xml, "child", child, [] { }); });
	}

	void gen_parent_route(Xml_generator &xml, char const *service, char const *src_label = "", char const *dst_label = "")
	{
		gen_route(xml, service, src_label, [&] {
			xml.node("parent", [&] {
				if (strcmp(dst_label, ""))
					xml.attribute("label", dst_label); }); });
	};

	void gen_local_route(Xml_generator &xml, char const *service, char const *label = "")
	{
		gen_route(xml, service, label, [&] { xml.node("local", [] { }); });
	};

	void gen_service(Xml_generator &xml, char const *service)
	{
		gen_named_node(xml, "service", service, [] { });
	};

	void gen_provides(Xml_generator &xml, char const *service)
	{
		xml.node("provides", [&] { gen_service(xml, service); });
	}

	void gen_common_routes(Xml_generator &xml)
	{
		gen_parent_route(xml, "PD");
		gen_parent_route(xml, "ROM");
		gen_parent_route(xml, "CPU");
		gen_parent_route(xml, "LOG");
	}

	void gen_vfs_policy(Xml_generator &xml, char const *label_prefix,
	                    char const *root, bool writeable)
	{
		xml.node("policy", [&] {
			xml.attribute("label_prefix", label_prefix);
			xml.attribute("root", root);
			xml.attribute("writeable", writeable ? "yes" : "no");
		});
	};

	void gen_parent_provides_and_report_nodes(Xml_generator &xml)
	{
		xml.node("report", [&] {
			xml.attribute("provided", "yes");
			xml.attribute("child_ram",  "yes");
			xml.attribute("child_caps", "yes");
			xml.attribute("delay_ms", 500);
		});
		xml.node("parent-provides", [&] {
			gen_service(xml, "ROM");
			gen_service(xml, "CPU");
			gen_service(xml, "PD");
			gen_service(xml, "LOG");
			gen_service(xml, "RM");
			gen_service(xml, "File_system");
			// gen_service(xml, "Gui");
			gen_service(xml, "Timer");
			gen_service(xml, "Report");
			gen_service(xml, "Rtc");
			// gen_service(xml, "Block");
			// gen_parent_route(xml, "Gui");
			gen_service(xml, "Snapper");
		});
	}

	void gen_mke2fs_start_node(Xml_generator &xml, Child_state const &child)
	{
		child.gen_start_node(xml, [&] {
			xml.node("config", [&] {
				xml.node("libc", [&] {
					xml.attribute("stdout", "/dev/log");
					xml.attribute("stderr", "/dev/log");
					xml.attribute("stdin", "/dev/null");
					xml.attribute("rtc", "/dev/rtc");
				});
				xml.node("vfs", [&] {
					gen_named_node(xml, "dir", "dev", [&] {
						gen_named_node(xml, "block", "block", [&] {
							xml.attribute("label", "default");
							xml.attribute("block_buffer_count", 128);
						});
						gen_named_node(xml, "inline", "rtc", [&] {
							xml.append("2018-01-01 00:01");
						});
						xml.node("null", [&] {});
						xml.node("log", [&] {});
					});
				});
				gen_arg(xml, "mkfs.ext3");
				// gen_arg(xml, "-F");
				// gen_arg(xml, "-O");
				// gen_arg(xml, "^metadata_csum");
				gen_arg(xml, "/dev/block");
			});
			xml.node("route", [&] {
				gen_child_route(xml, "vfs_block", "Block");
				gen_parent_route(xml, "Timer");
				gen_common_routes(xml);
			});
		});
	}

	void gen_resize2fs_start_node(Xml_generator &xml, Child_state const &child)
	{
		child.gen_start_node(xml, [&] {
			xml.node("config", [&] {
				xml.node("libc", [&] {
					xml.attribute("stdout", "/dev/log");
					xml.attribute("stderr", "/dev/log");
					xml.attribute("stdin", "/dev/null");
					xml.attribute("rtc", "/dev/rtc");
				});
				xml.node("vfs", [&] {
					gen_named_node(xml, "dir", "dev", [&] {
						gen_named_node(xml, "block", "block", [&] {
							xml.attribute("label", "default");
							xml.attribute("block_buffer_count", 128);
						});
						gen_named_node(xml, "inline", "rtc", [&] {
							xml.append("2018-01-01 00:01"); }); // TODO: check the date
						xml.node("null", [&] {});
						xml.node("log", [&] {});
					});
				});
				gen_arg(xml, "resize2fs");
				gen_arg(xml, "-f");
				gen_arg(xml, "-p");
				gen_arg(xml, "/dev/block");
			});

			xml.node("route", [&] {
				gen_child_route(xml, "vfs_block", "Block");
				gen_parent_route(xml, "Timer");
				gen_common_routes(xml);
			});
		});
	}

	void gen_tresor_vfs_start_node(Xml_generator &xml, Child_state const &child, File_path const &image)
	{
		child.gen_start_node(xml, [&] {
			gen_provides(xml, "File_system");
			xml.node("config", [&] {
				xml.node("vfs", [&] {
					xml.node("fs", [&] {
						xml.attribute("buffer_size", "1M");
						xml.attribute("label", "tresor_fs -> /");
					});
					gen_named_node(xml, "tresor_crypto_aes_cbc", "crypto", [] { });
					gen_named_node(xml, "dir", "trust_anchor", [&] {
						xml.node("fs", [&] {
							xml.attribute("buffer_size", "1M");
							xml.attribute("label", "trust_anchor -> /");
						});
					});
					gen_named_node(xml, "dir", "dev", [&] {
						gen_named_node(xml, "tresor", "tresor", [&] {
							xml.attribute("block", File_path("/", image));
							xml.attribute("crypto", "/crypto");
							xml.attribute("trust_anchor", "/trust_anchor");
						});
					});
				});
				gen_vfs_policy(xml, "extend_fs_tool -> ", "/dev", true);
				gen_vfs_policy(xml, "rekey_fs_tool -> ", "/dev", true);
				gen_vfs_policy(xml, "lock_fs_tool -> ", "/dev", true);
				gen_vfs_policy(xml, "extend_fs_query -> ", "/dev", true);
				gen_vfs_policy(xml, "rekey_fs_query -> ", "/dev", true);
				gen_vfs_policy(xml, "lock_fs_query -> ", "/dev", true);
				gen_vfs_policy(xml, "vfs_block -> ", "/dev/tresor/current", true);
				gen_vfs_policy(xml, "snapper -> ", "/dev/tresor/current", true);
				gen_vfs_policy(xml, "client_fs_query -> ", "/dev/tresor/current", false);
				gen_vfs_policy(xml, "sync_to_tresor_vfs_init -> ", "/dev", true);
			});
			xml.node("route", [&] {
				gen_child_route(xml, "se_tresor_trust_anchor_vfs", "File_system", "trust_anchor -> /");
				gen_parent_route(xml, "File_system", "tresor_fs -> /");
				gen_common_routes(xml);
			});
		});
	}

	void gen_tresor_trust_anchor_vfs_start_node(Xml_generator &xml, Child_state const &child,
	                                            bool jent_avail)
	{
		child.gen_start_node(xml, [&] {
			gen_provides(xml, "File_system");
			xml.node("config", [&] {
				xml.node("vfs", [&] {
					gen_named_node(xml, "dir", "storage_dir", [&] {
						xml.node("fs", [&] {
							xml.attribute("buffer_size", "1M");
							xml.attribute("label", "storage_dir -> /");
						});
					});
					gen_named_node(xml, "dir", "dev", [&] {
						gen_named_node(xml, "tresor_trust_anchor", "tresor_trust_anchor", [&] {
							xml.attribute("storage_dir", "/storage_dir"); });
						if (jent_avail)
							gen_named_node(xml, "jitterentropy", "jitterentropy", [&] { });
						else {
							warning("Insecure mode, no entropy source!");
							gen_named_node(xml, "inline", "jitterentropy", [&] {
								xml.append_content(String<33> { "0123456789abcdefghijklmnopqrstuv" }); });
						}
					});
				});
				gen_vfs_policy(xml, "se_tresor_init_trust_anchor -> trust_anchor", "/dev/tresor_trust_anchor", true);
				gen_vfs_policy(xml, "se_tresor_init -> trust_anchor", "/dev/tresor_trust_anchor", true);
				gen_vfs_policy(xml, "se_tresor_vfs -> trust_anchor", "/dev/tresor_trust_anchor", true);
			});
			xml.node("route", [&] {
				gen_parent_route(xml, "File_system", "storage_dir -> /");
				gen_common_routes(xml);
			});
		});
	}
// <dir name="dev">
//    			<block name="block" block_buffer_count="128"/>
//    		</dir>
// <lwext4 block_device="/dev/block" cache_write_back="yes"
//               expand_via_io="no" writeable="yes"
//               reporting="no"
//               external_cache_size="32M" report_cache="yes"/>

	void gen_rump_vfs_start_node(Xml_generator &xml, Child_state const &child)
	{
		child.gen_start_node(xml, [&] {
			gen_provides(xml, "File_system");
			xml.node("config", [&] {
				xml.node("vfs", [&] {
					// xml.node("dir", [&] {
					// 	xml.attribute("name", "dev");
					// 	xml.node("block", [&] {
					// 		xml.attribute("name", "block");
					// 		xml.attribute("block_buffer_count", "128");
					// 	});
					// });
					// xml.node("dir", [&] {
					// 	xml.attribute("name", "root");
					// 	xml.node("lwext4", [&] {
					// 		xml.attribute("block_device", "/dev/block");
					// 		xml.attribute("cache_write_back", "yes");
					// 		xml.attribute("expand_via_io", "no");
					// 		xml.attribute("writeable", "yes");
					// 		xml.attribute("reporting", "no");
					// 		xml.attribute("external_cache_size", "32M");
					// 		xml.attribute("report_cache", "no");
					// 	});
					// });
					
					xml.node("rump", [&] {
						xml.attribute("fs", "ext2fs");
						xml.attribute("ram", "20M");
					});
				});
				xml.node("default-policy", [&] {
					xml.attribute("root", "/");
					xml.attribute("writeable", "yes");
				});
			});
			xml.node("route", [&] {
				gen_child_route(xml, "vfs_block", "Block");
				gen_parent_route(xml, "Timer");
				gen_parent_route(xml, "RM");
				gen_common_routes(xml);
			});
		});
	}

	void gen_truncate_file_start_node(Xml_generator &xml, Child_state const &child,
	                                  char const *path, uint64_t size)
	{
		child.gen_start_node(xml, [&] {
			xml.node("config", [&] {
				xml.attribute("size", size);
				xml.attribute("path", path);
				log("Size:");
				log(size);
				log("path:");
				log(path);
				xml.node("vfs", [&] {
					gen_named_node(xml, "dir", "tresor", [&] {
						xml.node("fs", [&] { xml.attribute("label", "tresor -> /"); }); }); });
			});
			xml.node("route", [&] {
				gen_parent_route(xml, "File_system");
				gen_common_routes(xml);
			});
		});
	}

	void gen_sync_to_tresor_vfs_init_start_node(Xml_generator &xml, Child_state const &child)
	{
		child.gen_start_node(xml, [&] {
			xml.node("config", [&] {
				xml.node("libc", [&] {
					xml.attribute("stdin", "/dev/log");
					xml.attribute("stdout", "/dev/log");
					xml.attribute("stderr", "/dev/log");
				});
				xml.node("vfs", [&] {
					gen_named_node(xml, "dir", "dev", [&] {
						xml.node("log", [] { }); });
					gen_named_node(xml, "dir", "tresor", [&] {
						xml.node("fs", [&] { xml.attribute("writeable", "yes"); }); });
				});
			});
			xml.node("route", [&] {
				gen_child_route(xml, "se_tresor_vfs", "File_system");
				gen_common_routes(xml);
			});
		});
	}

	void gen_tresor_vfs_block_start_node(Xml_generator &xml, Child_state const &child)
	{
		auto gen_policy = [&] (char const *label) {
			xml.node("policy", [&] {
				xml.attribute("label", label);
				xml.attribute("block_size", "512");
				xml.attribute("file", "/data");
				xml.attribute("writeable", "yes");
			});
		};
		child.gen_start_node(xml, [&] {
			gen_provides(xml, "Block");
			xml.node("config", [&] {
				xml.node("vfs", [&] {
					xml.node("fs", [&] { xml.attribute("buffer_size", "1M"); });
				});
				gen_policy("mke2fs -> default");
				gen_policy("resize2fs -> default");
				gen_policy("rump_vfs -> ");
			});
			xml.node("route", [&] {
				gen_child_route(xml, "se_tresor_vfs", "File_system");
				gen_common_routes(xml);
			});
		});
	}

//  <service name="File_system" label_suffix="ta -> /"> <child name="usb_sec_fs"/> </service>
// <vfs>
//           <dir name="se_trust_anchor">
//             <fs label="ta -> /"/>
//           </dir>
//         </vfs>
	void gen_tresor_init_trust_anchor_start_node(Xml_generator &xml, Child_state const &child, Passphrase const &passphrase)
	{
		child.gen_start_node(xml, [&] {
			xml.node("config", [&] {
				xml.attribute("passphrase", passphrase);
				xml.attribute("trust_anchor_dir", "/trust_anchor");
				xml.attribute("verbose", "yes");
				xml.node("vfs", [&] {
					gen_named_node(xml, "dir", "trust_anchor", [&] {
						xml.node("fs", [&] { xml.attribute("label", "trust_anchor -> /"); }); 
					});
					// xml.node("dir", [&] {
					// 	xml.attribute("name", "trust_anchor");
					// 	xml.node("fs", [&] {});
					// });
				});
			});
			xml.node("route", [&] {
				gen_child_route(xml, "se_tresor_trust_anchor_vfs", "File_system", "trust_anchor -> /");
				// xml.node("service", [&] {
				// 	xml.attribute("name", "File_system");
				// 	xml.node("child", [&] {
				// 		xml.attribute("name", "usb_sec_fs");
				// 	});
				// });
				gen_common_routes(xml);
			});
		});
	}

	void gen_tresor_init_start_node(Xml_generator &xml, Child_state const &child,
	                                Tresor::Superblock_configuration sb_config)
	{
		child.gen_start_node(xml, [&] {
			xml.node("config", [&] {
				xml.node("trust-anchor", [&] { xml.attribute("path", "/trust_anchor"); });
				xml.node("block-io", [&] {
					xml.attribute("type", "vfs");
					xml.attribute("path", "/tresor.img");
				});
				xml.node("crypto", [&] { xml.attribute("path", "/crypto"); });
				xml.node("vfs", [&] {
					xml.node("fs", [&] { xml.attribute("buffer_size", "1M"); });
					gen_named_node(xml, "tresor_crypto_aes_cbc", "crypto", [] { });
					gen_named_node(xml, "dir", "trust_anchor", [&] {
						xml.node("fs", [&] { xml.attribute("label", "trust_anchor -> /"); }); });
				});
				sb_config.generate_xml(xml);
			});
			xml.node("route", [&] {
				gen_child_route(xml, "se_tresor_trust_anchor_vfs", "File_system", "trust_anchor -> /");
				gen_parent_route(xml, "File_system");
				gen_common_routes(xml);
			});
		});
	}

	void gen_child_service_policy(Xml_generator &xml, char const *service, Child_state const &child)
	{
		gen_named_node(xml, "service", service, [&] {
			xml.node("default-policy", [&] {
				gen_named_node(xml, "child", child.start_name(), [&] { }); }); });
	}

	void gen_fs_tool_start_node(Xml_generator &xml, Child_state const &child, char const *server,
	                            char const *path, auto content)
	{
		child.gen_start_node(xml, [&] {
			xml.node("config", [&] {
				xml.attribute("exit", "yes");
				xml.node("vfs", [&] {
					gen_named_node(xml, "dir", "root", [&] {
						xml.node("fs", [&] { xml.attribute("writeable", "yes"); }); }); });
				xml.node("new-file", [&] {
					xml.attribute("path", String<64>("/root", path));
					xml.append_content(content);
				});
			});
			xml.node("route", [&] {
				gen_child_route(xml, server, "File_system");
				gen_common_routes(xml);
			});
		});
	}

	void gen_fs_query_start_node(Xml_generator &xml, Child_state const &child, char const *server,
	                             char const *path, bool content)
	{
		child.gen_start_node(xml, [&] {
			xml.node("config", [&] {
				xml.node("vfs", [&] {
					xml.node("fs", [&] { xml.attribute("writeable", "no"); }); });
				xml.node("query", [&] {
					xml.attribute("path", path);
					xml.attribute("size", "yes");
					xml.attribute("content", content ? "yes" : "no");
				});
				path = 0;
				content = 0;
			});
			xml.node("route", [&] {
				gen_local_route(xml, "Report");
				if (!strcmp(server, "parent")) {
					gen_parent_route(xml, "File_system");
				}
				else {
					gen_child_route(xml, server, "File_system");
				}
				gen_common_routes(xml);
			});
		});
	}

	void gen_extend_fs_tool_start_node(Xml_generator &xml, Child_state const &child, char const *tree, Number_of_blocks num_blocks) {
		log("gen_extend_fs_tool_start_node");
		gen_fs_tool_start_node(xml, child, "se_tresor_vfs", "/tresor/control/extend", String<64>("tree=", tree, ",blocks=", num_blocks));
	}

	void gen_lock_fs_tool_start_node(Xml_generator &xml, Child_state const &child) {
		log("gen_lock_fs_tool_start_node");
		gen_fs_tool_start_node(xml, child, "se_tresor_vfs", "/tresor/control/deinitialize", "true");
	}

	void gen_rekey_fs_tool_start_node(Xml_generator &xml, Child_state const &child) {
		log("gen_rekey_fs_tool_start_node");
		gen_fs_tool_start_node(xml, child, "se_tresor_vfs", "/tresor/control/rekey", "true");
	}

	void gen_image_fs_query_start_node(Xml_generator &xml, Child_state const &child) {
		log("gen_image_fs_query_start_node");
		gen_fs_query_start_node(xml, child, "parent", "/", false);
	}

	void gen_client_fs_query_start_node(Xml_generator &xml, Child_state const &child) {
		log("gen_client_fs_query_start_node");
		gen_fs_query_start_node(xml, child, "se_tresor_vfs", "/", false);
		log("gen_client_fs_query_start_node");
	}

	void gen_extend_fs_query_start_node(Xml_generator &xml, Child_state const &child) {
		log("gen_extend_fs_query_start_node");
		gen_fs_query_start_node(xml, child, "se_tresor_vfs", "/tresor/control", true);
	}

	void gen_lock_fs_query_start_node(Xml_generator &xml, Child_state const &child) {
		log("gen_lock_fs_query_start_node");
		gen_fs_query_start_node(xml, child, "se_tresor_vfs", "/tresor/control", true);
	}

	void gen_rekey_fs_query_start_node(Xml_generator &xml, Child_state const &child) {
		log("gen_rekey_fs_query_start_node");
		gen_fs_query_start_node(xml, child, "se_tresor_vfs", "/tresor/control", true);
	}

	void gen_snapper_start_node(Xml_generator &xml) {
		xml.node("start", [&] {
			xml.attribute("name", "snapper");
			xml.attribute("caps", "200");
			xml.attribute("ram", "400M");
			gen_provides(xml, "Snapper");
			xml.node("config", [&] {
				xml.attribute("verbose", "true");
				xml.attribute("redundancy", "2");
				xml.attribute("integrity", "true");
				xml.attribute("max_snapshots", "0");
				xml.attribute("min_snapshots", "0");
				xml.attribute("threshold", "1000");
				xml.attribute("expiration", "3600");
				xml.attribute("bufsize", "10M");
				xml.node("rtc", [&] {});
				xml.node("vfs", [&] {
					xml.node("fs", [&] {});
				});
			});
			xml.node("route", [&] {
				gen_child_route(xml, "rump_vfs", "File_system");
				gen_child_route(xml, "ahci", "Block");
				gen_parent_route(xml, "Rtc");
				gen_parent_route(xml, "Timer");
				gen_common_routes(xml);
				// xml.node("any-service", [&] {
				// xml.node("parent", [&] {});
				// xml.node("any-child", [&] {});
				// });
			});

		});
		
		log("gen_snapper_start_node DONE!");
	}

// <!-- 
// <start name="snapper" ram="400M" caps="200">
//   <provides>
//     <service name="Snapper"/>
//   </provides>

//   <config
//       verbose="true"
//       redundancy="2"
//       integrity="true"
//       max_snapshots="0"
//       min_snapshots="0"
//       threshold="1000"
//       expiration="3600"
//       bufsize="10M">

//     <rtc/>

//     <vfs>
//       <fs/>	  
//     </vfs>
//   </config>
// 	<route>
// -->
// 	<!--
// 		<service name="File_system"><child name="vfs"/></service>
// 	-->
// <!--
// 		<service name="File_system"><child name="se_vault"/></service>
// 		<service name="Block"><child name="ahci"/></service>
// 		<any-service> <parent/> <any-child /> </any-service>
// 	</route>
// </start>
// -->

	void gen_isomem_start_node(Xml_generator &xml) {
		xml.node("start", [&] {
			xml.attribute("name", "isomem");
			xml.attribute("caps", "2000");
			xml.node("resource", [&] {
				xml.attribute("name", "RAM");
				xml.attribute("quantum", "1G");
			});
			xml.node("config", [&] {
				xml.attribute("verbose", "true");
				xml.attribute("ld_verbose", "yes");
			});
			xml.node("route", [&] {
				gen_parent_route(xml, "Block");
				// gen_child_route(xml, "ahci", "Block");
				gen_parent_route(xml, "RM");
				gen_parent_route(xml, "Timer");
				gen_child_route(xml, "snapper", "Snapper");
				gen_service(xml, "Gui");
				gen_common_routes(xml);
				// xml.node("any-service", [&] {
				// xml.node("parent", [&] {});
				// xml.node("any-child", [&] {});
				// });
			});
		});
		

		log("gen_isomem_start_node DONE!");
	}


// <!-- 
// <start name="isomem" caps="2000">
//  	<resource name="RAM" quantum="1G"/>
  
//  	<config verbose="yes" ld_verbose="yes">
//  	</config>
//  	<route>
//  		<service name="Block"><child name="ahci"/></service>
//  		<any-service> <parent/> <any-child /> </any-service>
//  	</route>
// </start>
// --> 

}

#endif /*  SANDBOX_H_ */
