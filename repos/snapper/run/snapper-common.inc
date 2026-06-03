<parent-provides>
	<service name="ROM"/>
	<service name="PD"/>
	<service name="RM"/>
	<service name="CPU"/>
	<service name="LOG"/>
</parent-provides>
<default-route>
	<any-service> <parent/> <any-child/> </any-service>
</default-route>
<default caps="100" ram="100M"/>

<start name="timer">
	<provides>
		<service name="Timer"/>
	</provides>
</start>

<start name="linux_rtc" ld="no">
	<provides>
		<service name="Rtc"/>
	</provides>
</start>

<start name="vfs_block" ram="1G">
	<provides><service name="Block"/></provides>
	<config>
		<vfs>
			<ram/>
			<import>
				<rom name="snapper_block.raw"/>
			</import>
		</vfs>
		<policy label_prefix="vfs"
			      file="/snapper_block.raw" block_size="512" writeable="yes"/>
  </config>
</start>

<start name="vfs" ram="24M">
 	<provides><service name="File_system"/></provides>
 	<config>
 		<vfs>
 			<dir name="dev">
 				<block name="block" block_buffer_count="128"/>
 			</dir>
			<lwext4 block_device="/dev/block" cache_write_back="yes"
				      expand_via_io="yes" writeable="yes"
				      reporting="no"
				      external_cache_size="0M" report_cache="no"/>
 		</vfs>
 		<policy label_prefix="snapper" root="/" writeable="yes"/>
 	</config>
</start>

<start name="snapper" ram="400M" caps="100">
  <provides>
    <service name="Snapper"/>
  </provides>
	<config       verbose="true"
                redundancy="2"
                integrity="true"
                max_snapshots="0"
                min_snapshots="0"
                threshold="100"
                expiration="3600">
    <rtc/>
		<vfs>
			<fs/>
		</vfs>
	</config>
  <route>
		<service name="Rtc"> <any-child/> </service>
		<any-service> <parent/> <any-child/> </any-service>
	</route>
</start>


<!-- Local Variables: -->
<!-- mode: xml -->
<!-- End: -->
