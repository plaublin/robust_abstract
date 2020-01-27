include Makefile.common

SUBDIRS = common
SUBDIRS += modular_bft
SUBDIRS += protocols/common
SUBDIRS += protocols/quorum
SUBDIRS += protocols/zlight
SUBDIRS += protocols/ring
SUBDIRS += protocols/chain
SUBDIRS += protocols/pbft
SUBDIRS += protocols/pbft/replica
SUBDIRS += protocols/pbft/client
SUBDIRS += protocols/aardvark

subdirs:
	@for i in $(SUBDIRS); do \
	echo "make all in $$i..."; \
	$(MAKE) ${PARALLEL_MAKE} -C $$i ${MAKE_OPT} C="${C}" CC="${CC}" all ; \
	if [ $$? != 0 ]; then exit 2; fi; \
	done

lib.a: subdirs
	$(AR) rcv $@ `find $(SUBDIRS) -name *.o` 
	$(RANLIB) $@
	mv lib.a benchmarks/

all: lib.a
	$(MAKE) -C benchmarks ${MAKE_OPT}

clean:
	@for i in $(SUBDIRS); do \
		echo "make clean in $$i..."; \
		make -C $$i clean; \
	done
	make -C benchmarks clean

depend:
	@for i in $(SUBDIRS); do \
		echo "make depend in $$i..."; \
		make -C $$i depend; \
	done
