#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <map>
#include <utility>
#include <cmath>
#include "rappel_common.h"
#include "as_topology.h"
#include "topology.h"

using namespace std;

Topology* Topology::_instance = NULL; // initialize pointer

/**
 * Set up end-node network topology
 *
 * @param num_nodes number of nodes in the network
 */
Topology::Topology() {

  _rng = RNG::get_instance();
  assert(_rng != NULL);

  _as_topology = ASTopology::get_instance();
  assert(_as_topology != NULL);

  _timer = Timer::get_instance();
  assert(_timer != NULL);

  char delay_filename[MAX_STRING_LENGTH];
  sprintf(delay_filename, "harvard-planetlab-trace.data");

  FILE* delay_file = fopen(delay_filename, "r");

  if (delay_file != NULL) {
    // delay file exists, read it.

#ifdef DEBUG
    printf("topology.cpp: Reading delay file (%s)\n", delay_filename);
#endif // DEBUG

    enum Primitive {DELAY_HEADER = 0x3493, LINK_HEADER, MEASUREMENT, };
    Primitive expected = DELAY_HEADER;

    bool first = true;
    unsigned int last_delay = 0;
    unsigned int links_read_counter = 0;
    unsigned int msmnts_read_counter = 0;

    vector_links links;
    vector_msmnts msmnts;
    unsigned int delay, num_links, num_msmnts;

    char line[MAX_STRING_LENGTH];
    while (fgets(line, MAX_STRING_LENGTH, delay_file) != NULL) {

      if (expected == DELAY_HEADER) {

        char primitive;
        assert(sscanf(line, "%c %u %u", &primitive, &delay, &num_links) == 3);
        assert(primitive == 'D');
        assert(delay > last_delay || first);

        // which is the next delay set (if no data available for a certain avg delay)
        for (unsigned int i = last_delay + 1; i <= delay; i++) {
          assert(_next_delay.find(i) == _next_delay.end());
          _next_delay[i] = delay;
          //printf("next delay for %u is %u\n", i, delay);
        }

        if (first) { // special case
          assert(_next_delay.find(0) == _next_delay.end());
          _next_delay[0] = delay;
          first = false;
        }
 
        // mark the highest delay
        _highest_delay = delay;

        // last delay
        last_delay = delay;

        expected = LINK_HEADER;
        links_read_counter = 0;
        links.clear();
        assert(links.size() == 0);
         
      } else if (expected == LINK_HEADER) {
               
        char primitive;

        assert(sscanf(line, "%c %u", &primitive, &num_msmnts) == 2);
        assert(primitive == 'L');

        links_read_counter++;

        // change what is expected next         
        expected = MEASUREMENT;
        msmnts_read_counter = 0;
        msmnts.clear();
        assert(msmnts.size() == 0);
         
      } else if (expected == MEASUREMENT) {

        unsigned int msmnt;      
        char primitive;

        assert(sscanf(line, "%c %u", &primitive, &msmnt) == 2);
        assert(primitive == 'M');

        msmnts_read_counter++;
        msmnts.push_back(msmnt);
         
        // change what is expected next         (if applicable)
        if (msmnts_read_counter == num_msmnts) {

          assert(msmnts.size() == num_msmnts);
          links.push_back(msmnts);
           
          expected = LINK_HEADER;
           
          if (links_read_counter == num_links) {

            assert(links.size() == num_links);
            assert(_delays.find(delay) == _delays.end());
            _delays[delay] = links;
             
            expected = DELAY_HEADER;
          }
        }
      } else {
        assert(false); // should not happen
      }
    } // done reading delay file
    assert(expected == DELAY_HEADER);
  } else {
    printf("no trace based delay file %s\n",delay_filename);
    assert(false);
  }
}

Topology* Topology::get_instance() {

  if (_instance == NULL) {  
    _instance = new Topology(); // create sole instance
    assert(_instance != NULL);
  }

return _instance; // address of sole instance
}

/**
 * Get "base" network delay prior to randomization
 * 
 * @param src source node
 * @param dst destination node
 * @return the network delay between the two nodes
 */
unsigned int Topology::network_delay_native(const node_id_t& src, 
					    const node_id_t& dst) {

  unsigned int as_delay =  _as_topology->get_route(_nodes[src].isp_network,
						   _nodes[dst].isp_network).delay;
  
  return ( as_delay + _nodes[src].isp_delay + _nodes[dst].isp_delay );
}

/**
 * Actual network delay between two end-nodes.
 *
 * @param src source node
 * @param dst destination node
 * @return the network delay between the two nodes
 */
unsigned int Topology::network_delay(const node_id_t& src, const node_id_t& dst) {

  assert(_nodes.find(src) != _nodes.end());
  assert(_nodes.find(dst) != _nodes.end());

  // make a link pair out of src, dst
  pair<unsigned int, unsigned int> link = make_pair(src.get_address(), dst.get_address());

#ifdef DEBUG
  static node_id_t first_src = NO_SUCH_NODE;
  static node_id_t first_dst = NO_SUCH_NODE;

  if (first_src == NO_SUCH_NODE) {
    assert(first_dst == NO_SUCH_NODE);
    first_src = src;
    first_dst = dst;
  }
#endif // DEBUG

  unsigned int delay = network_delay_native(src, dst);

  // cout << "delay from " << src.str() << " to " << dst.str() << " : " << delay << endl ;

  unsigned int delay_dynamic = delay;

#ifdef USE_DYNAMIC_DELAY

  // below one liner: old pure-random model
  //delay_dynamic = (unsigned int) ((double) delay * 0.01 * (double) _rng->rand_uint(100, 125));
  
  // BEGIN: trace based delays
  unsigned int delay_in_ms = delay / MILLI_SECONDS;
  unsigned next_delay = _highest_delay;

  if (_next_delay.find(delay_in_ms) != _next_delay.end()) {
     //printf("next delay for %u (%u ms) is %u\n", delay, delay_in_ms, _next_delay[delay_in_ms]);
     next_delay = _next_delay[delay_in_ms];
    //printf("next delay for %u (%u ms) is %u\n", delay, delay_in_ms, next_delay);
  } else {
    assert(delay_in_ms > _highest_delay);
  }

  // information for the next delay exists
  //assert(_delays.find(next_delay) != _delays.end());

  // assing a trace link, if not already assigned
  if (_link_info.find(link) == _link_info.end()) {

    //assert(_delays[next_delay].size() > 0);
    unsigned int random_link = _rng->rand_uint(0, _delays[next_delay].size() - 1);
    _link_info[link] = make_pair(random_link, _timer->get_time());
    //printf("delay_in_ms=%u, next_delay=%u\n", delay_in_ms, next_delay);
    //printf("trace link assigned to network link (%u-%u): %u out of %u\n", 
    //  link.first, link.second, random_link, _delays[next_delay].size());
    //assert(random_link < _delays[next_delay].size());
    //assert(_trace_link_assigned[link] < _delays[next_delay].size());
  }

  // get the assigned random link (double check too)
  //assert(_trace_link_assigned.find(link) != _trace_link_assigned.end());
  unsigned int rand_link = _link_info[link].first;

  //cout << "--> rand_link = " << rand_link << endl ;

  //assert(_trace_link_assigned[link] < _delays[next_delay].size());
  //assert(rand_link < _delays[next_delay].size());
  
  // there should be at least 1 msnt for this link
  //assert(_delays[next_delay].at(rand_link).size() > 0);
  unsigned int rand_msmnt = _rng->rand_uint(0, _delays[next_delay].at(rand_link).size() - 1);
  //assert(rand_msmnt < _delays[next_delay].at(rand_link).size());

  //cout << "--> rand_msmnt = " << rand_msmnt << endl ;

  delay_dynamic = _delays[next_delay].at(rand_link).at(rand_msmnt);

  // allow FIFO ordering guarantee for each src-dst pair
  if (_timer->get_time() + delay_dynamic <= _link_info[link].second) {

    // this packet causes FIFO ordering violations -- fix
    _link_info[link].second = _link_info[link].second + 1 * MICRO_SECOND;
    delay_dynamic = _link_info[link].second - _timer->get_time() + 1 * MICRO_SECOND;
#ifdef DEBUG     
    cout << "packet FIFO. craptastic." << endl;
#endif // DEBUG
  } else {

    // this packet causes no FIFO ordering violations
    _link_info[link].second = _timer->get_time() + delay_dynamic;
  }
#endif // USE_DYNAMIC_DELAY

#ifdef DEBUG
  if (src == first_src && dst == first_dst) {
    printf("base delay is %u; randomized delay is %u\n", delay, delay_dynamic);
  }
#endif // DEBUG

  //  cout << "--> delay_dynamic = " << delay_dynamic << endl ;

//   if (delay < delay_dynamic) cout << "#" ; else cout << "." ;
//   if ((double)delay / (double)delay_dynamic < 0.2) {
//     cout << endl << "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" <<endl ;
//   }

  return delay_dynamic;
}

void Topology::add_node(const node_id_t& node_id) {
  
  TopologyNode node;
  
  assert(_nodes.find(node_id) == _nodes.end());
  node.node_id = node_id;
  
  // TODO: currently using uniform random placement of end-nodes to AS
  // networks.  adapt for other distributions?
  
  do {
    node.isp_network = 
      _rng->rand_uint(0, _as_topology->get_num_networks() - 1);
    //printf("trying placing node on isp network = %u\n", node.isp_network);
  } while (_as_topology->get_network_type(node.isp_network) 
     != AS_STUB 
     && _as_topology->get_network_type(node.isp_network) 
     != AS_STUB_AND_TRANSIT);
  
  assert(globops.is_initialized());
  node.isp_delay = _rng->rand_uint(globops.isp_delay_min,
            globops.isp_delay_max - 1);
  
  _nodes[node_id] = node;
}

#ifdef USE_DYNAMIC_DELAY
unsigned int Topology::cleanup() {

  unsigned int erased = 0;

  for(map< pair<unsigned int, unsigned int>, pair<unsigned int, Clock> >::iterator
    it = _link_info.begin(); it != _link_info.end(); /* */ ) {

    // it becomes invalid after erase
    map<pair<unsigned int, unsigned int>, pair<unsigned int, Clock> >::iterator
      it_now = it;
    it++;

    if (it_now->second.second + DEF_CLEANUP_BUFFER < _timer->get_time()) {
      erased++;
      _link_info.erase(it_now);
    }
  }

  return erased;
}
#endif // USE_DYNAMIC_DELAY
