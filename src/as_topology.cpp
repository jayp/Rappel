#include "as_topology.h"

using namespace std;

const unsigned int MAX_FILE_CHECKS = 10; 

ASTopology* ASTopology::_instance = NULL; // initialize pointer

/**
 * Constructor
 */
ASTopology::ASTopology() {

  //printf("Reading AS topology....\n");

  _rng = RNG::get_instance();
  assert(_rng != NULL);

  _timer = Timer::get_instance();
  assert(_timer != NULL);

  read_data_files();

#ifdef TOPO_USE_PRECOMP
  
  char precompute_filename[MAX_STRING_LENGTH];
  sprintf(precompute_filename, "compact-fastpair-%u.%s", globops.sys_seed,
    globops.as_zhang_topology_date.c_str());

#ifdef DEBUG
  printf("as_topology.cpp: Reading precomp delays file %s\n", precompute_filename);
#endif // DEBUG  

  _precompute_file.open(precompute_filename, ios::binary | ios::in);
  if (_precompute_file.fail()) { // JAY: THIS DOESN'T WORK. C++ SUCKS.
    printf("FATAL: fail while opening %s\n", precompute_filename);
    assert(false);
  }
#endif // TOPO_USE_PRECOMP

  //printf("Done (ASTopology).\n");
}

/**
 * 
 */
void ASTopology::read_data_files() {
  
  char line[MAX_STRING_LENGTH];

  assert(globops.is_initialized());
  string nodes_filename = "nodes." + globops.as_zhang_topology_date;

  FILE *file = fopen(nodes_filename.c_str(), "r");
  assert(file != NULL);

  while (fgets(line, MAX_STRING_LENGTH, file) != NULL) {

    unsigned int n1, n2, n3, n4;
    sscanf(line, "%u %u %u %u", &n1, &n2, &n3, &n4);

    ASNetwork network;
    network.id = n1;
    network.type = (ASNetworkType) n4;

    assert(_network_ids.find(n1) == _network_ids.end());

    _network_ids[n1] = _networks.size();
    _networks.push_back(network);
  }
  
  fclose(file);

  string links_filename = "links." + globops.as_zhang_topology_date;
  file = fopen(links_filename.c_str(), "r");
  assert(file != NULL);

  while (fgets(line, MAX_STRING_LENGTH, file) != NULL) {
      
    unsigned int n1, n2, n3, n4, n5;
    sscanf(line, "%u %u %u %u %u", &n1, &n2, &n3, &n4, &n5);

    // check for existence of networks
    assert(_network_ids.find(n1) != _network_ids.end());
    assert(_network_ids.find(n2) != _network_ids.end());

    // check for correct translations of network id
    assert(_networks[_network_ids[n1]].id == n1);
    assert(_networks[_network_ids[n2]].id == n2);

    pair<unsigned int, unsigned int> edge1(_network_ids[n1],
      _network_ids[n2]);
    pair<unsigned int, unsigned int> edge2(_network_ids[n2],
      _network_ids[n1]);

    // make sure these don't already exist
    //assert(_hop_delay.find(edge1) == _hop_delay.end());
    //assert(_hop_delay.find(edge2) == _hop_delay.end());

    if (_hop_delay.find(edge1) == _hop_delay.end()) {

      // TODO: use a more sophisticated delay model
      assert(globops.as_delay_max / 4 > globops.as_delay_min);
      Clock delay = globops.as_delay_min + 
        _rng->rand_ulonglong(globops.as_delay_max - globops.as_delay_min);

      //Clock delay = globops.as_delay_min + globops.as_delay_max / 2 +
      //  _rng->rand_ulonglong(globops.as_delay_max * 1 / 2 - globops.as_delay_min);

      unsigned int r = _rng->rand_uint(1, 10);
      if (r <= 1) {

        //printf("FASTER AS HOP\n");
        //delay = globops.as_delay_min + globops.as_delay_max / 2 +
        //  _rng->rand_ulonglong(globops.as_delay_max * 1 / 2 - globops.as_delay_min);
        delay = (Clock) (0.005 * MILLI_SECONDS)
          + _rng->rand_ulonglong((Clock) (3.995 * MILLI_SECONDS));

      } else if (r <= 3) {

        //delay = 2.005 * MILLI_SECOND + _rng->rand_ulonglong(47.995 * MILLI_SECONDS);
        delay = 4 * MILLI_SECONDS + _rng->rand_ulonglong(26 * MILLI_SECONDS);

      } else  {

        //printf("REGULAR AS HOP\n");
        //delay = globops.as_delay_min +
        //  _rng->rand_ulonglong(globops.as_delay_max / 4 - globops.as_delay_min);
        delay = 32 * MILLI_SECONDS + _rng->rand_ulonglong(115 * MILLI_SECONDS);
     }

      assert(delay <= INF_DELAY);
      _hop_delay[edge1] = _hop_delay[edge2] = delay;

      // add n2 to n1's neighbor list (and vice versa)
      // NOTE: translated network ids to array subscripts.
      //       from here on out, network ids are unimportant
      ((_networks[_network_ids[n1]]).neighbors).push_back(
          _network_ids[n2]);
      ((_networks[_network_ids[n2]]).neighbors).push_back(
          _network_ids[n1]);
    }
  }

  fclose(file);
}

/**
 * Destructor
 */
ASTopology::~ASTopology() {
}

/**
 * Get singleton instance
 */
ASTopology* ASTopology::get_instance() {

  if (_instance == NULL) {  
    _instance = new ASTopology; // create sole instance
    assert(_instance != NULL);
  }

  //printf("ASTopology [invoked from %s]: instance addr=%d\n", request, _instance);

return _instance; // address of sole instance
}

/**
 * Compute all shortest route length from a src node to all other nodes in the
 * network. This is an optimization routine.
 * @return The time delay from src to each other node in the network
 */
vector<Clock> 
ASTopology::get_all_routes_from(unsigned int src) 
{
  // assert(false);
  assert(src < _networks.size());
  
  vector<Clock> cost(_networks.size(), INF_DELAY);

  fibheap* heap = fh_makekeyheap();

  // initialize the heap
  vector<fibheap_el*> elements(_networks.size(), NULL);
  for (unsigned int i = 0; i < _networks.size(); i++) {
    elements[i] = fh_insertkey(heap, INF_DELAY, (void*) i); 
  }

  // initialize root cost
  cost[src] = 0;
  fh_replacekey(heap, elements[src], 0);

  // dijkstra
  for (unsigned int j = 0; j < _networks.size(); j++) {

    // get cheapest
    unsigned int cheapest = (unsigned int) fh_extractmin(heap);

    // examine its children 
    for (unsigned int i = 0; i < _networks[cheapest].neighbors.size(); i++) {
      // get child
      unsigned int neighbor = _networks[cheapest].neighbors[i];
      
      // get the cost or route to child
      pair<unsigned int, unsigned int> edge(cheapest, neighbor);
      assert(_hop_delay.find(edge) != _hop_delay.end());

      // if lower cost route is found update and replace child in the heap
      if (cost[neighbor] > cost[cheapest] + _hop_delay[edge]) {

        fh_replacekey(heap, elements[neighbor],
          cost[cheapest] + _hop_delay[edge]);

        cost[neighbor] = cost[cheapest] + _hop_delay[edge];
      }
    }
  }

  fh_deleteheap(heap);

  return cost;
}

/**
 * Compute all shortest route length from a src node to all other nodes in the
 * network. This is an optimization routine.
 * @return returns if the route exists. and if so, the entire hop-by-hop route
 * to every other node from the src node.
 */
vector<pair<bool, vector<unsigned short> > > 
ASTopology::get_all_routes_details_from (unsigned int src) {
  vector<unsigned int> prev_hop(_networks.size(), _networks.size());
  vector<unsigned int> cost(_networks.size(), INF_DELAY);

  fibheap* heap = fh_makekeyheap();

  // initialize the heap
  vector<fibheap_el*> elements(_networks.size(), NULL);
  for (unsigned int i = 0; i < _networks.size(); i++) {
    elements[i] = fh_insertkey(heap, INF_DELAY, (void*) i); 
  }

  // initialize root cost
  cost[src] = 0;
  fh_replacekey(heap, elements[src], 0);
  prev_hop[src] = src;

  // dijkstra
  for (unsigned int i = 0; i < _networks.size(); i++) {
    // get cheapest
    unsigned int cheapest = (unsigned int) fh_extractmin(heap);

    // examine its children 
    for (unsigned int j = 0;j < _networks[cheapest].neighbors.size(); j++) {
      // get child
      unsigned int neighbor = _networks[cheapest].neighbors[j];
      
      // get the cost or route to child
      pair<unsigned int, unsigned int> edge(cheapest, neighbor);
      assert(_hop_delay.find(edge) != _hop_delay.end());

      // if lower cost route is found update and replace child in the heap
      if (cost[neighbor] > cost[cheapest] + _hop_delay[edge]) {
	
        fh_replacekey(heap, elements[neighbor],
		      cost[cheapest] + _hop_delay[edge]);
	
        cost[neighbor] = cost[cheapest] + _hop_delay[edge];
	     prev_hop[neighbor] = cheapest;
      }
    }
  }

  fh_deleteheap(heap);

  // construct result
  vector<pair<bool, vector<unsigned short> > > ret; 

  for (unsigned int dst = 0; dst < _networks.size(); dst++) {

    vector<unsigned short> path; // path from dst to src (reversed later)

    unsigned int current = prev_hop[dst]; // the previous node

    while (current != src && current != _networks.size()) {

      path.push_back(current);
      current = prev_hop[current];
    }

    bool is_broken = (current == _networks.size()); // is the path broken?
    
    // reverse it
    reverse(path.begin(), path.end());
    ret.push_back(make_pair(is_broken, path));
  }

  return ret;
}

/**
 * Get the route from a src node to a dst node
 * @param src source node
 * @param dst destination node
 */
ASRoute ASTopology::get_route(unsigned int src, unsigned int dst) {

  pair<unsigned int, unsigned int> end_nodes;
  end_nodes.first = (src < dst) ? src : dst;
  end_nodes.second = (src > dst) ? src : dst;

  // check wether we need to keep track of routes
  if (globops.transport_keep_track_of_traffic == "yes" ||
      globops.transport_keep_track_of_traffic == "YES") {
    pair<unsigned int, unsigned int> end_nodes (src,dst);

    map< pair<unsigned int, unsigned int>, unsigned int>::iterator
      it_nm (_num_messages.find(end_nodes));
    if (it_nm == _num_messages.end()) {
      _num_messages[end_nodes] = 1;
    } else {
      it_nm->second = it_nm->second + 1 ;
    }
  }

  // check to see if the route is already in the route cache
  if (_route_cache_no_limit.find(end_nodes) != _route_cache_no_limit.end()) {

    // get route
    unsigned int delay = _route_cache_no_limit[end_nodes].first;
    _route_cache_no_limit[end_nodes].second = _timer->get_time();
    ASRoute rt;
    rt.delay = delay;
    return rt;
  }
  
#ifdef AS_TOPO_USE_RAND_DELAY
  ASRoute r = get_route_rand(src,dst);

#else // AS_TOPO_USE_RAND_DELAY

#ifdef TOPO_USE_PRECOMP
  ASRoute r = get_route_precomp(src, dst);

#else // TOPO_USE_PRECOMP
  ASRoute r = get_route_dijkstra(src, dst);
    
#endif // TOPO_USE_PRECOMP
#endif // AS_TOPO_USE_RAND_DELAY

  _route_cache_no_limit[end_nodes] = make_pair(r.delay, _timer->get_time());
  return r;
}

/**
 * Dijkstra's algorithm
 */
ASRoute ASTopology::get_route_dijkstra(unsigned int src, unsigned int dst) {

  assert(src < _networks.size() && dst < _networks.size());

  // TODO : request num here has no interest ; it is always 1 when
  // inserted in the cache
  static int request_num = 0; 
  request_num++;

  if (src == dst) {  // Etienne: this should never happen!! JAY: not true
    ASRoute rt;
    rt.intermediate_hops.push_back(src);
    rt.delay = 0;
    return rt;
  }

  // compute djikstra
  vector<unsigned int> cost(_networks.size(), INF_DELAY);
  vector<unsigned int> prev_hop(_networks.size(), _networks.size());
  
  fibheap* heap = fh_makekeyheap();
  
  //vector<int> elements(_networks.size(), 0);
  vector<fibheap_el*> elements(_networks.size(), NULL);
  for (unsigned int i = 0; i < _networks.size(); i++) {
    //elements[i] = (int) fh_insertkey(heap, INF_DELAY, (void*) i);
    elements[i] = fh_insertkey(heap, INF_DELAY, (void*) i);
  }
  
  cost[src] = 0;
  fh_replacekey(heap, elements[src], 0);
  prev_hop[src] = src;
  
  for (unsigned int i = 0; i < _networks.size(); i++) {
    
    // 64 bit compliance hack
    void * tmp_cheapest_a =  fh_extractmin(heap);
    unsigned long tmp_cheapest_b = (unsigned long) tmp_cheapest_a ;
    if (sizeof(void *)==8) {
      // compile in 64 bits
      assert(tmp_cheapest_b <= 0x0000ffff);
    }
    unsigned int cheapest = (unsigned int) tmp_cheapest_b;

    for (unsigned int j = 0; j < _networks[cheapest].neighbors.size(); j++) {
      
      unsigned int neighbor = _networks[cheapest].neighbors[j];

      pair<unsigned int, unsigned int> edge(cheapest, neighbor);
      assert(_hop_delay.find(edge) != _hop_delay.end());
      
      if (cost[neighbor] > cost[cheapest] + _hop_delay[edge]) {
	
        fh_replacekey(heap, elements[neighbor],
		      cost[cheapest] + _hop_delay[edge]);
	
        cost[neighbor] = cost[cheapest] + _hop_delay[edge];
        prev_hop[neighbor] = cheapest;
      }
    }
    
    if (cheapest == dst)  { 
      // short circuit here -- we're done
      break;
    }
  }
  
  fh_deleteheap(heap);
  
  vector<unsigned int> steps;
  unsigned int curr = dst;
  
  while (prev_hop[curr] != curr) {
    
    steps.push_back(curr);
    curr = prev_hop[curr];
    
    // hey this code will asserrt !!!! 
    assert (curr < _networks.size());
    if (curr == _networks.size()) {
      // no route
      // code is not reachable --
      
      printf("no route from src=%u to dst=%u, breakage at curr=%u\n", src, dst, curr);
      printf("reverse route up to now:");
      for (unsigned int i = 0; i < steps.size(); i++) {
        printf(" %u", steps[i]);
      }
      printf("\n");
      
      ASRoute r;
      r.delay = INF_DELAY;

      return r;
    }
  }
  
  steps.push_back(curr); // should be the src

  // reverse the steps
  reverse(steps.begin(), steps.end());

  // check
  assert(steps[0] == src);
  assert(steps[steps.size() - 1] == dst);

  unsigned int delay = 0;
  for (unsigned int i = 0; i < steps.size() - 1; i++) {
    
    pair<unsigned int, unsigned int> edge(steps[i], steps[i + 1]);
    assert(_hop_delay.find(edge) != _hop_delay.end());
    
    delay += _hop_delay[edge];
  }
  
  ASRoute r;
  r.intermediate_hops = steps;
  r.delay = delay;
  
  return r;
}

#ifdef AS_TOPO_USE_RAND_DELAY
/**
 *
 */
ASRoute ASTopology::get_route_rand(unsigned int src, unsigned int dst) {

  assert(src < _networks.size() && dst < _networks.size());

  static map< pair<unsigned int, unsigned int>, Clock> last_msg;

  pair<unsigned int, unsigned int> ends;
  ends.first = src;
  ends.second = dst;

  ASRoute rt;
  rt.intermediate_hops.push_back(src);
  rt.delay = _rng->rand_uint(globops.as_delay_min, globops.as_delay_max);

  // packets from src-dst must maintain FIFO ordering
  if (last_msg.find(ends) != last_msg.end()) {
    if (last_msg[ends] >= _timer->get_time() + rt.delay) {
      // next message arrives 1 milli second after previous packet
      rt.delay = last_msg[ends] - _timer->get_time() + 1 * MILLI_SECOND;
    }
  }

  last_msg[ends] = _timer->get_time() + rt.delay;

  return rt;
}
#endif // AS_TOPO_USE_RAND_DELAY

#ifdef TOPO_USE_PRECOMP
/**
 *
 */
ASRoute ASTopology::get_route_precomp(unsigned int src, unsigned int dst) {
  assert(src < _networks.size() && dst < _networks.size());
  
  unsigned char as_delay_in_ms = 0;

  // canonical ordering
  unsigned int n1 = (src > dst) ? src : dst;
  unsigned int n2 = (n1 == src) ? dst : src;
  assert(n1 >= n2);

/*
  Data layed out in this format:
  d
  dd
  ddd
  dddd
  ddddd
  ... etc
*/

  unsigned long long offset = (int) (n1 * (n1 + 1) / (double) 2 + n2) * sizeof(unsigned char);
  //unsigned long long offset = ((n1 * _networks.size()) + n2) * sizeof(unsigned char);
  
  _precompute_file.seekg(offset);
  _precompute_file.read((char *) &as_delay_in_ms, sizeof (unsigned char));

  ASRoute rt;
  rt.delay = as_delay_in_ms * MILLI_SECONDS;

  // Jay's note: check the first few routes (as defined by MAX_FILE_CHECKS)
  static unsigned int num_checks = 0;

  if (num_checks < MAX_FILE_CHECKS) {
    unsigned int dijk_delay = get_route_dijkstra(src, dst).delay;
    // 255 is the maximum value of unsigned char
    assert(as_delay_in_ms == min(255, (int) (dijk_delay / MILLI_SECONDS)));
    num_checks++;
  }
  
  return rt;
}
#endif // TOPO_USE_PRECOMP

#if COMPILE_FOR == SIMULATOR
/**
 *
 */
void ASTopology::write_sent_messages_to_file(ofstream & out) {
  // This functional can only be called if we are keeping track of traffic
  if (globops.transport_keep_track_of_traffic == "yes" ||
    globops.transport_keep_track_of_traffic == "YES") {

    string separator ("\t");

    for (map<pair<unsigned int, unsigned int>, unsigned int>::iterator it
	   = _num_messages.begin(); it != _num_messages.end(); it++) {
      
      out << it->first.first << separator
	     << it->first.second << separator
        << it->second << endl;
    }
  } else {
    std::cerr << "DID NOT KEEP TRACK OF TRAFFIC" << std::endl;
  }
}
#endif // COMPILE_FOR == SIMULATOR

#ifdef USE_DYNAMIC_DELAY
unsigned int ASTopology::cleanup() {

  unsigned int erased = 0;

  for(map<pair<unsigned int, unsigned int>, pair<unsigned int, Clock> >::iterator
    it = _route_cache_no_limit.begin(); it != _route_cache_no_limit.end(); /* */ ) {

    // it becomes invalid after erase
    map< pair<unsigned int, unsigned int>, pair<unsigned int, Clock> >::iterator
      it_now = it;
    it++;

    if (it_now->second.second + DEF_CLEANUP_BUFFER < _timer->get_time()) {
      _route_cache_no_limit.erase(it_now);
      erased++;
    }
  }

  return erased;
}
#endif // USE_DYNAMIC_DELAY

// as_topology.cpp
