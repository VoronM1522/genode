content: include src lib LICENSE

include:
	mkdir -p $@
	cp -r $(REP_DIR)/$@/* $@

src:
	mkdir -p $@
	cp -r $(REP_DIR)/$@/* $@

lib:
	mkdir -p $@/mk
	cp -r $(REP_DIR)/$@/mk/* $@/mk/

LICENSE:
	cp $(REP_DIR)/$@ $@
