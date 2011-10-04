#ifndef _ASTOPOLOGY_H
#define _ASTOPOLOGY_H

#include <vector>
#include "rappel_common.h"
#include "timer.h"

#ifdef TOPO_USE_PRECOMP
#include <iostream>
#include <fstream>
#endif // TOPO_USE_PRECOMP

extern "C" {
#include "fib.h"
}

using namespace std;

enum ASNetworkType {
  AS_UNKNOWN,
  AS_STUB,
  AS_TRANSIT,
  AS_STUB_AND_TRANSIT,
};

class ASNetwork {
public:
  unsigned int id;
  ASNetworkType type;
  vector<unsigned int> neighbors; // refered by neighbor ids
};

class ASRoute {
public:
  // first hop is the src, last hop is the dst
  vector<unsigned int> intermediate_hops;
  Clock delay;
};


class ASTopology {

public:
  ASTopology();
  ~ASTopology();

  // singleton
  static ASTopology* get_instance();
  unsigned int get_num_networks() const {
    return _networks.size();
  }
  ASNetworkType get_network_type(int index) const {
    return _networks[index].type;
  }
  unsigned int get_num_routes_cached() const {
    return _route_cache_no_limit.size();
  }

  void all_routes_from(unsigned int src);
  ASRoute get_route(unsigned int src, unsigned int dst);
  vector <Clock> get_all_routes_from (unsigned int src);
  vector<pair< bool, vector<unsigned short> > > 
    get_all_routes_details_from (unsigned int src);

#if COMPILE_FOR == SIMULATOR
  void write_sent_messages_to_file(ofstream & out);
#endif // COMPILE_FOR == SIMULATOR

#ifdef USE_DYNAMIC_DELAY
unsigned int cleanup();
#endif // USE_DYNAMIC_DELAY

private:
  // variables
  //unsigned int _cached_items ;

  // system-wide singleton
  RNG* _rng;
  Timer* _timer;

  // self-referenced singleton
  static ASTopology* _instance;

  vector<ASNetwork> _networks;
  map<unsigned int, unsigned int> _network_ids;
  map< pair<unsigned int, unsigned int>, Clock> _hop_delay;

  // route cache
  map< pair<unsigned int, unsigned int>, pair<unsigned int, Clock> > _route_cache_no_limit; // map of ((src, dst), (delay, last_used))
  //map< pair<unsigned int, unsigned int>, fibheap_el*> _route_cache;
  //fibheap* _route_cache_heap; // sorted by lowest request_num (LRU policy)

  // various route computation methods

  // regular
  void read_data_files();
  ASRoute get_route_dijkstra(unsigned int src, unsigned int dst);

#ifdef AS_TOPO_USE_RAND_DELAY
  ASRoute get_route_rand(unsigned int src, unsigned int dst);
#endif // AS_TOPO_USE_RAND_DELAY

#ifdef TOPO_USE_PRECOMP
  ASRoute get_route_precomp(unsigned int src, unsigned int dst);

  ifstream _precompute_file; 
#endif // TOPO_USE_PRECOMP

#if COMPILE_FOR == SIMULATOR
  map< pair<unsigned int, unsigned int>, unsigned int> _num_messages;
#endif // COMPILE_FOR == SIMULATOR

};

#endif // _ASTOPOLOGY_H
