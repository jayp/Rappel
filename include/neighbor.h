#ifndef _NEIGHBOR_H
#define _NEIGHBOR_H

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <vector>
#include <utility>
#include <algorithm>
#include <boost/random.hpp>
#include "rappel_common.h"
#include "timer.h"
#include "coordinate.h"

using namespace std ;

class RoleInfo {
public:
  Role _role;
  Clock _last_heard_request;
  Clock _last_heard_reply;
  Clock _last_sent_request;
};

class Neighbor {

public :

  // constructor(s)
  Neighbor(const node_id_t& node_id, const node_id_t& nbr_id,
    const Coordinate& coord, const Role& role, const BloomFilter& filter);

  // accessors
  /** get node ID */
  node_id_t get_id() const {
    return _nbr_id;
  }
  event_id_t get_node_check_event_id() const {
    return _node_check_event_id;
  }
  /** the bloom filter of the feed subscription of the node */
  const BloomFilter* get_bloom_filter() const {
    return &_filter;
  }
  /** get the NC of the node */
  Coordinate get_nc() const {
    return _nc;
  }
  vector<Role> get_roles() const {
    vector<Role> res;
    for (unsigned int i = 0; i < _roles.size(); i++) {
      res.push_back(_roles[i]._role);
    }
    return res;
  }
  bool plays_role(const Role& role) const;
  Clock get_last_sent_request_in_role(const Role& role) const;
  Clock get_last_heard_request_in_role(const Role& role) const;
  Clock get_last_heard_reply_in_role(const Role& role) const;
  Clock get_last_heard_in_role(const Role& role) const;
  //Clock get_last_ping_sent_in_role(const Role& role) const;

  // mutators
  /** set the bloom filter */
  bool set_bloom_filter(const BloomFilter& filter) {
    if (filter.get_version() > _filter.get_version()) {
      _filter = filter;
      return true;
    }
    return false;
  }
  /** set the NC of the node */
  bool set_nc(const Coordinate& nc) { 
    if (nc.get_version() > _nc.get_version()) {

      // TODO: what if new coord is applied. Should one make sure
      // parent-child relationship invariant is maintained? what about s1
      // candidates?
        _nc = nc;
        return true;
    }
    return true;
  }
  void set_node_check_event_id(event_id_t event_id) {
    _node_check_event_id = event_id;
  }

  // begin periodically pinging the neighbor (done at creation)
  void initialize_periodic_checks ();

  /** update the time when the neighbor last sent a message 
      (in a particular role) */
  void update_sent_request_in_role(const Role& role);
  void update_heard_request_in_role(const Role& role);
  void update_heard_reply_in_role(const Role& role);
  void update_ping_sent_in_role(const Role& role);

  void add_role(const Role& role);
  bool remove_role(const Role& role);

private:

  node_id_t _node_id;
  node_id_t _nbr_id;
  BloomFilter _filter;
  Coordinate _nc;
  Clock _last_heard_from;
  vector< RoleInfo > _roles;
  event_id_t _node_check_event_id;

  Timer* _timer;
  RNG* _rng;
};

class CandidatePeer {

public:
  CandidatePeer(const node_id_t& id, const Coordinate& nc) {
    _timer = Timer::get_instance();
    assert(_timer != NULL);
    _id = id;
    _nc = nc;
    _last_voted_at = _timer->get_time();
    _num_votes = 1;
    _audited = false;
  }

  // accessors
  node_id_t get_id() const {
    return _id;
  }
  Coordinate get_nc() const {
    return _nc;
  }
  Clock get_last_vote_at() const { 
    return _last_voted_at;
  }
  unsigned int get_num_votes() const {
    return _num_votes;
  }
  bool is_audited() const {
    return _audited;
  }
  const BloomFilter* get_bloom_filter() const {
    return &_filter;
  }

  // mutators
  bool set_nc(const Coordinate& nc) {
    if (nc.get_version() > _nc.get_version()) {
      _nc = nc;
      return true;
    }
    return false;
  }
  void add_vote() { 
    Clock curr_time = _timer->get_time();
    assert(curr_time >= _last_voted_at);
    _last_voted_at = curr_time;
    _num_votes++;
  }
  void set_audited() {
    _audited = true;
  }
  void unset_audited() {
    _audited = false;
  }
  bool set_bloom_filter(const BloomFilter& filter) {
    if (filter.get_version() > _filter.get_version()) {
      _filter = filter;
      return true;
    }
    return false;
  }

private:
  Timer* _timer;
  node_id_t _id;
  Coordinate _nc;
  Clock _last_voted_at;
  unsigned int _num_votes;
  BloomFilter _filter;
  bool _audited;
};

#endif // _NEIGHBOR_H
