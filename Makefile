BIN  ?= strfry
APPS ?= dbutils relay mesh
OPT  ?= -O3 -g

include golpe/rules.mk

LDLIBS += -lsecp256k1 -lzstd
ifeq ($(shell uname -s),Darwin)
LDLIBS += -luv
BREW_PREFIX    := $(shell brew --prefix 2>/dev/null)
OPENSSL_PREFIX := $(shell brew --prefix openssl 2>/dev/null)
ifneq ($(BREW_PREFIX),)
BREW_INCS += -I$(BREW_PREFIX)/include
BREW_LIBS += -L$(BREW_PREFIX)/lib
endif
ifneq ($(OPENSSL_PREFIX),)
BREW_INCS += -I$(OPENSSL_PREFIX)/include
BREW_LIBS += -L$(OPENSSL_PREFIX)/lib
endif
INCS      += $(BREW_INCS)
LDFLAGS   += $(BREW_LIBS)
# Propagate to sub-makes (e.g. uWebSockets) that honor XCXXFLAGS / XLDFLAGS
export XCXXFLAGS += $(BREW_INCS)
export XLDFLAGS  += $(BREW_LIBS)
endif
INCS += -Iexternal/negentropy/cpp

# --- uWebSockets slow-client double-free fix ----------------------------------
# The fix lives in the nested uWebSockets submodule, which is pinned to an
# upstream hoytech commit. Rather than fork the whole submodule chain, we keep
# the change as a patch in this repo and apply it (idempotently) just before the
# uWebSockets static lib is built.
UWS_DIR   := golpe/external/uWebSockets
UWS_PATCH := $(abspath patches/uws-double-free-slow-client.patch)

golpe/external/uWebSockets/libuWS.a: build/.uws-patched.stamp

build/.uws-patched.stamp: $(UWS_PATCH)
	@mkdir -p build
	@if [ ! -e $(UWS_DIR)/src/WebSocket.cpp ]; then \
	  echo "ERROR: $(UWS_DIR) not checked out. Run 'make setup-golpe' first." >&2; \
	  exit 1; \
	fi
	@if git -C $(UWS_DIR) apply --reverse --check $(UWS_PATCH) >/dev/null 2>&1; then \
	  echo "uWebSockets double-free patch already applied."; \
	else \
	  echo "Applying uWebSockets double-free patch..."; \
	  git -C $(UWS_DIR) apply $(UWS_PATCH); \
	fi
	@touch $@
# -----------------------------------------------------------------------------

build/StrfryTemplates.h: $(shell find src/tmpls/ -type f -name '*.tmpl')
	PERL5LIB=golpe/vendor/ perl golpe/external/templar/templar.pl src/tmpls/ strfrytmpl $@

src/apps/relay/RelayWebsocket.o: build/StrfryTemplates.h

.PHONY: test-subid
test-subid: build/subid_tests
	build/subid_tests

build/subid_tests: test/SubIdTests.cpp build/golpe.h
	$(CXX) $(CXXFLAGS) $(INCS) $< -o $@
