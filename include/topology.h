#ifndef _TOPOLOGY_H
#define _TOPOLOGY_H

#include <cstdio>
#include <map>
#include "rappel_common.h"
#include "timer.h"
#include "as_topology.h"

using namespace std;

class TopologyNode {
public:
  node_id_t node_id; // Etienne: changed to class format
  unsigned int isp_network;
  unsigned int isp_delay;
};

typedef vector<unsigned int> vector_msmnts;
typedef vector<vector_msmnts> vector_links;

class Topology {

public:
  Topology();

  // singleton
  static Topology* get_instance();

  // accessors
  unsigned int network_delay(const node_id_t& src, const node_id_t& dst); //const;
  unsigned int network_delay_native(const node_id_t& src, const node_id_t& dst); //const;
  bool node_exists(const node_id_t& node_id) const {
    return (_nodes.find(node_id) != _nodes.end());
  }
#ifdef USE_DYNAMIC_DELAY
  unsigned int get_num_routes_used() const {
    return _link_info.size();
  }
#endif // USE_DYNAMIC_DELAY

   // mutators
   void add_node(const node_id_t& node_id);
#ifdef USE_DYNAMIC_DELAY
  unsigned int cleanup();
#endif // USE_DYNAMIC_DELAY

private:
  // system-wide singleton
  RNG* _rng;
  ASTopology* _as_topology;
  Timer* _timer;

  // self-referenced singleton
  static Topology* _instance;

  map<node_id_t, TopologyNode> _nodes;
  map<unsigned int, vector_links > _delays; // delay, vector of links measurements
  map<unsigned int, unsigned int> _next_delay;
  // for below: uint is trace link assigned and second is the last packet arrival
  map< pair<unsigned int, unsigned int>, pair<unsigned int, Clock> > _link_info; 
  unsigned int _highest_delay;
};

#endif // _TOPOLOGY_H
