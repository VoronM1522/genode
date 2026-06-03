content: include lib LICENSE

include:
	mkdir -p include
	cp -r $(REP_DIR)/include/* $@

lib:
	mkdir -p lib/mk
	cp -r $(REP_DIR)/lib/mk/* lib/mk/

LICENSE:
	cp $(REP_DIR)/LICENSE $@
