default:
	@@echo "Improper usage."
	@@echo "  Correct syntax: make (network | network_dbg | simulator | simulator_dbg | precompute | precompute_routes | precompute_endhost_delays | topogen)"

###################################################
# Variables (directories, compile flags, etc.)

CPP := g++
CC := gcc

IDIR := ../include
LDIR := ../lib
BDIR := ../bin
ODIR := ../obj

LIBS_COMMON := -lboost_program_options -lsqlite3
LIBS_NETWORK := $(LIBS_COMMON) -lpthread -lboost_serialization
LIBS_SIMULATOR := $(LIBS_COMMON)

CPPFLAGS_COMMON := -I$(IDIR) -L$(LDIR) $(SYS) -Wall
CPPFLAGS_NETWORK := $(CPPFLAGS_COMMON) -O3 -DCOMPILE_FOR=NETWORK \
	-DDEBUG
CPPFLAGS_NETWORK_DBG := $(CPPFLAGS_COMMON) -g -DCOMPILE_FOR=NETWORK \
	-DDEBUG
CPPFLAGS_SIMULATOR := $(CPPFLAGS_COMMON) -O3 -DCOMPILE_FOR=SIMULATOR \
	-DTOPO_USE_PRECOMP -DUSE_DYNAMIC_DELAY
CPPFLAGS_SIMULATOR_DBG := $(CPPFLAGS_COMMON) -g -DCOMPILE_FOR=SIMULATOR \
	-DUSE_DYNAMIC_DELAY -DTOPO_USE_PRECOMP -DDEBUG
CPPFLAGS_SIMULATOR_TOOLS := $(CPPFLAGS_COMMON) -O3 -DCOMPILE_FOR=SIMULATOR \
	-DIGNORE_ETIENNE_STATS
CPPFLAGS_ROUTE_ANALYZER := -g # -O3

###################################################
# Dependencies

_HEADER_COMMON := rappel_common.h \
	data_objects.h \
	config.h \
	timer.h \
	transport.h \
	stats.h \
	coordinate.h \
	bloom_filter.h \
	feed.h \
	node.h \
	rappel.h \
	vivaldi.h

HEADER_COMMON = $(patsubst %, $(IDIR)/%, $(_HEADER_COMMON))

_OBJECTS_COMMON := rappel_common.o \
	config.o \
	debug.o \
	timer.o \
	transport.o \
	stats.o \
	coordinate.o \
	bloom_filter.o \
	feed.o \
	node.o \
	neighbor.o \
	vivaldi.o \
	rappel.o

_OBJECTS_SIMULATOR := $(_OBJECTS_COMMON) \
	fib.o \
	as_topology.o \
	topology.o \
	driver_simulator.o

_OBJECTS_NETWORK := $(_OBJECTS_COMMON) \
	md5.o \
	driver_network.o

_OBJECTS_PRECOMP :=  fib.o \
	as_topology.o \
	config.o \
	timer.o \
	rappel_common.o \
	topology.o \
	driver_precomp_topology.o

_OBJECTS_PRECOMP_ROUTES := fib.o \
	as_topology.o \
	driver_precomp_routes.o \
	config.o \
	timer.o \
	rappel_common.o \
	topology.o

_OBJECTS_PRECOMP_ENDHOST_DELAYS := fib.o \
	as_topology.o \
	debug.o \
	driver_precomp_endhost_delays.o \
	config.o \
	timer.o \
	rappel_common.o \
	topology.o

_OBJECTS_TOPOGEN := $(_OBJECTS_COMMON) \
	fib.o \
	as_topology.o \
	topology.o \
	driver_topogen.o


###################################################
# Targets

ifeq "$(MAKECMDGOALS)" "network"
ODIR := ../obj/network
OBJECTS = $(patsubst %, $(ODIR)_%, $(_OBJECTS_NETWORK))
endif # "network"
network: CPPFLAGS := $(CPPFLAGS_NETWORK)
network: $(BDIR)/rappel_net
$(BDIR)/rappel_net: $(OBJECTS)
	@@echo "Attempting to compile the rappel (network) binary...:"
	$(CPP) -o $(BDIR)/rappel_net $(OBJECTS) $(CPPFLAGS) $(LIBS_NETWORK)

ifeq "$(MAKECMDGOALS)" "network_dbg"
ODIR := ../obj/network_dbg
OBJECTS = $(patsubst %, $(ODIR)_%, $(_OBJECTS_NETWORK))
endif # "network"
network_dbg: CPPFLAGS := $(CPPFLAGS_NETWORK_DBG)
network_dbg: $(BDIR)/rappel_net_dbg
$(BDIR)/rappel_net_dbg: $(OBJECTS)
	@@echo "Attempting to compile the rappel (network_dbg) binary...:"
	$(CPP) -o $(BDIR)/rappel_net_dbg $(OBJECTS) $(CPPFLAGS) $(LIBS_NETWORK)

ifeq "$(MAKECMDGOALS)" "simulator"
ODIR := ../obj/simulator
OBJECTS := $(patsubst %, $(ODIR)_%, $(_OBJECTS_SIMULATOR))
endif # "simulator"
simulator: CPPFLAGS := $(CPPFLAGS_SIMULATOR)
simulator: $(BDIR)/rappel_sim
$(BDIR)/rappel_sim: $(OBJECTS) 
	@@echo "Attempting to compile the rappel (simulator) binary..."
	$(CPP) -o $(BDIR)/rappel_sim $(OBJECTS) $(CPPFLAGS) $(LIBS_SIMULATOR)

# etienne: tried this but it does not work
# ifeq "$(MAKECMDGOALS)" "simulator_opt"
# ODIR := ../obj/simulator_opt
# OBJECTS := $(patsubst %, $(ODIR)_%, $(_OBJECTS_SIMULATOR))
# endif # "simulator_opt"
# simulator_opt: CPPFLAGS := $(CPPFLAGS_SIMULATOR_OBJ)
# simulator_opt: $(BDIR)/rappel_sim_opt
# $(BDIR)/rappel_sim_opt: $(OBJECTS) 
# 	@@echo "Attempting to compile the rappel (simulator) optimized binary..."
# 	$(CPP) -o $(BDIR)/rappel_sim_opt $(OBJECTS) $(CPPFLAGS) $(LIBS_SIMULATOR)

ifeq "$(MAKECMDGOALS)" "simulator_dbg"
ODIR := ../obj/simulator_dbg
OBJECTS := $(patsubst %, $(ODIR)_%, $(_OBJECTS_SIMULATOR))
endif # "simulator_dbg"
simulator_dbg: CPPFLAGS := $(CPPFLAGS_SIMULATOR_DBG)
simulator_dbg: $(BDIR)/rappel_sim_dbg
$(BDIR)/rappel_sim_dbg: $(OBJECTS) 
	@@echo "Attempting to compile the rappel (simulator_dbg) binary..."
	$(CPP) -o $(BDIR)/rappel_sim_dbg $(OBJECTS) $(CPPFLAGS) $(LIBS_SIMULATOR)

# precompute is closely related to simulator (hence, they share obj files)
ifeq "$(MAKECMDGOALS)" "precompute"
ODIR := ../obj/simulator
OBJECTS := $(patsubst %, $(ODIR)_%, $(_OBJECTS_PRECOMP))
endif # "precompute"
precompute: CPPFLAGS := $(CPPFLAGS_SIMULATOR)
precompute: $(BDIR)/rappel_precompute
$(BDIR)/rappel_precompute: $(OBJECTS)
	@@echo "Attempting to compile the rappel (precomputation)  binary..."
	$(CPP) -o $(BDIR)/rappel_precompute $(OBJECTS) $(CPPFLAGS) $(LIBS_SIMULATOR)

# precompute routes is also related to simulator
ifeq "$(MAKECMDGOALS)" "precompute_routes"
ODIR := ../obj/simulator
OBJECTS := $(patsubst %, $(ODIR)_%, $(_OBJECTS_PRECOMP_ROUTES))
endif # "precompute_routes"
precompute_routes: CPPFLAGS := $(CPPFLAGS_SIMULATOR)
precompute_routes: $(BDIR)/rappel_precompute_routes
$(BDIR)/rappel_precompute_routes: $(OBJECTS)
	@@echo "Attempting to compile the rappel (precomputation of routes)  binary..."
	$(CPP) -o $(BDIR)/rappel_precompute_routes $(OBJECTS) $(CPPFLAGS) $(LIBS_SIMULATOR)

# precompute endhost delays is also related to simulator
ifeq "$(MAKECMDGOALS)" "precompute_endhost_delays"
ODIR := ../obj/simulator
OBJECTS := $(patsubst %, $(ODIR)_%, $(_OBJECTS_PRECOMP_ENDHOST_DELAYS))
endif # "precompute_endhost_delays"
precompute_endhost_delays: CPPFLAGS := $(CPPFLAGS_SIMULATOR_DBG)
precompute_endhost_delays: $(BDIR)/rappel_precompute_endhost_delays
$(BDIR)/rappel_precompute_endhost_delays: $(OBJECTS)
	@@echo "Attempting to compile the rappel (precomputation of endhost delays)  binary..."
	$(CPP) -o $(BDIR)/rappel_precompute_endhost_delays $(OBJECTS) $(CPPFLAGS) $(LIBS_SIMULATOR)

ifeq "$(MAKECMDGOALS)" "topogen"
ODIR := ../obj/simulator_tools
OBJECTS := $(patsubst %, $(ODIR)_%, $(_OBJECTS_TOPOGEN))
endif # "topogen"
topogen: CPPFLAGS := $(CPPFLAGS_SIMULATOR_TOOLS)
topogen: $(BDIR)/rappel_topogen
$(BDIR)/rappel_topogen: $(OBJECTS)
	@@echo "Attempting to compile the rappel (topogen)  binary..."
	$(CPP) -o $(BDIR)/rappel_topogen $(OBJECTS) $(CPPFLAGS) $(LIBS_SIMULATOR)

route_analyzer: 
	g++ -o $(BDIR)/route_analyzer $(CPPFLAGS_ROUTE_ANALYZER) route_analyzer.cpp

###################################################
# Compilation rules

$(ODIR)_%.o: %.c
	$(CC) -c -o $@ $< $(CPPFLAGS)

$(ODIR)_%.o: %.cpp $(IDIR)/%.h $(HEADER_COMMON)
	$(CPP) -c -o $@ $< $(CPPFLAGS)

$(ODIR)_%.o: %.cpp $(IDIR)/%.hpp $(HEADER_COMMON)
	$(CPP) -c -o $@ $< $(CPPFLAGS)

$(ODIR)_%.o: %.cpp $(HEADER_COMMON)
	$(CPP) -c -o $@ $< $(CPPFLAGS)

.PHONY: clean
clean:
	rm -f $(ODIR)/*.o $(BDIR)/rappel_*
