#ifndef _FEED_H
#define _FEED_H

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <set>
#include <utility>
#include <algorithm>
#include <boost/random.hpp>
#include "rappel_common.h"
#include "coordinate.h"
#include "timer.h"
#include "transport.h"
#include "data_objects.h"

using namespace std;

class EntryDetails {
public:
  std::string msg;
  Clock sync_emission_time;
};

class Feed {
public:
  Feed(const node_id_t& owner, const feed_id_t& id);
  Feed(const feed_id_t& id, const Coordinate& publisher_nc);

  // accessors
  /** get the feed ID */
  feed_id_t get_feed_id() const {
    return _id ;
  }
  /** convert the feed ID to a canonical string */
  std::string str() const { 
    return _id.str();
  }
#if COMPILE_FOR == NETWORK
  Clock get_route_rtt() const;
#endif // COMPILE_FOR == NETWORK
  /** get the highest update seq. number recv'd from the publisher */
  unsigned int get_latest_update_seq() const {
    return _latest_update_seq;
  }
  bool obtained_update(unsigned int seq_num) const {
    return (_cache.find(seq_num) != _cache.end());
  }
  /** get the node id for the publisher of the given feed */
  node_id_t get_publisher() const {
    return (node_id_t) _id;
  }
  bool is_publisher_nc_set() const {
    //return _publisher_nc_set;
    return (_publisher_nc.get_version() > 0);
  }
  Coordinate get_publisher_nc() const {
    assert(is_publisher_nc_set());
    return _publisher_nc;
  }
  bool seeking_parent() const {
    return _seeking_parent;
  }
/*
  unsigned int get_num_join_requests() const {
    return _num_join_requests;
  }
  unsigned int get_num_periodic_join_requests() const {
    return _num_periodic_join_requests;
  }
*/
  bool has_parent() const {
    return (_parent != NO_SUCH_NODE);
  }
  node_id_t get_parent() const {
    assert(_parent != NO_SUCH_NODE);
    return _parent;
  }
  double get_path_cost() const {
    assert(_parent != NO_SUCH_NODE);
    return _path_cost;
  }
  node_id_t get_grand_parent() const {
    if(_ancestors.size() > 1) {
      assert(_ancestors[0] == get_publisher());
      assert(_ancestors[_ancestors.size() - 1] == get_parent());
      return _ancestors[_ancestors.size() - 2];
    }
    return NO_SUCH_NODE;
  }
  vector<node_id_t> get_ancestors() const {
    return _ancestors;
  }
  bool is_an_ancestor(const node_id_t& target_node) const {
    for (unsigned int i = 0; i < _ancestors.size(); i++) {
      if (_ancestors[i] == target_node) {
        return true;
      }
    }
    return false;
  }
  node_id_t get_random_ancestor(bool exponential_distribution) const {
    assert (_ancestors.size() > 1);
    assert(_ancestors[0] == get_publisher());
#ifdef DEBUG
    printf("Node=%s, Feed=%s, Ancestors=[", _owner.str().c_str(), _id.str().c_str());
    for (int i = (int) _ancestors.size() - 1; i >= 0; i--) {
      if (i != (int) _ancestors.size() - 1) { printf(", [%u]", i); }
      printf("%s", _ancestors[i].str().c_str());
    }
    printf("]\n");
#endif // DEBUG
    if (has_parent()) {
      assert(_ancestors[_ancestors.size() - 1] == get_parent());
    }
    unsigned int ancestor_num = exponential_distribution ?
      _rng->rand_uint_exp(_ancestors.size() - 2, globops.rappel_num_feed_children) :
      _rng->rand_uint(0, _ancestors.size() - 2);
    assert(ancestor_num <= _ancestors.size() - 2);
    return _ancestors[ancestor_num];
  }
  unsigned int get_num_children() const {
    return _children.size();
  }
  vector<node_id_t> get_children() const {
    return _children;
  }
  bool is_parent(const node_id_t& node_id) const;
  bool is_child(const node_id_t& node_id) const;
  bool is_rejoin_scheduled() const {
    return (_rejoin_event_id != NO_SUCH_EVENT_ID);
  }
  
  // mutators
  void cancel_events();
  void reschedule_rejoin(bool initial_rejoin);

#if COMPILE_FOR == NETWORK
  void add_sample_rtt(const Clock& rtt);
  unsigned int send_rtt_to_publisher();
  unsigned int schedule_rtt_to_publisher(bool initial);
#endif // COMPILE_FOR == NETWORK

  void add_entry(unsigned int seq_num, const std::string& msg,
    const Clock& emission_time);
  unsigned int request_missing_entries(unsigned int seq_num,
    const node_id_t& src);
  void mark_pull_as_failed(unsigned int seq_num, const node_id_t& old_src);
  unsigned int send_entry_to_children(unsigned int seq_num);
  void send_entry(unsigned int seq_num, const node_id_t& node_id, bool pulled);
  void pull_entry(unsigned int seq_num, const node_id_t& src);

  bool set_publisher_nc(const Coordinate& nc) {
    if (nc.get_version() > _publisher_nc.get_version()) {
      _publisher_nc = nc;
      return true;
    }
    return false;
  }
  void set_seeking_parent() {
    _seeking_parent = true;
  }
  void unset_seeking_parent() {
    _seeking_parent = false;
  }
/*
  void another_join_request() {
    ++_num_join_requests;
    ++_num_periodic_join_requests;
  }
  void reset_debug_period() {
    _num_periodic_join_requests = 0;
  }
*/
  void set_parent(const node_id_t& parent);
  void unset_parent();
  void set_path_cost(double cost) {
    _path_cost = cost;
  }
  void set_grand_parent(const node_id_t& parent);
  void set_ancestors(const vector<node_id_t>& ancestors);
  void clear_ancestors();
  void add_child(const node_id_t& child);
  void remove_child(const node_id_t& child);
  
private :

  // private function  
  void schedule_rejoin(bool initial_rejoin);

  node_id_t _owner; // owner of the instance
  feed_id_t _id;
  Coordinate _publisher_nc;
  event_id_t _rejoin_event_id;
  set<event_id_t> _pull_entry_events;
#if COMPILE_FOR == NETWORK
  event_id_t _rtt_event_id;
  vector<Clock> _route_rtts;
#endif // COMPILE_FOR == NETWORK
  
  bool _seeking_parent;
/*
  unsigned int _num_join_requests;
  unsigned int _num_periodic_join_requests;
*/

  node_id_t _parent;
  double _path_cost;
  vector<node_id_t> _ancestors;
  node_id_t _grand_parent;
  vector<node_id_t> _children; // children in regular tree
  
  // num of the last received entry
  unsigned int _latest_update_seq;
  unsigned int _highest_update_seq;
  
  // used to maintain entries
  map<unsigned int, EntryDetails> _cache;
  map<unsigned int, node_id_t> _missing_entries;
  
  // system-wide singleton
  RNG* _rng;
  Timer* _timer;
  Transport* _transport;
};

#endif // _FEED_H
