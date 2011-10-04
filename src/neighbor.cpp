#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <vector>
#include <utility>
#include <map>
#include <algorithm>
#include <ext/algorithm>
#include "boost/random.hpp"
#include "rappel_common.h"
#include "topology.h"
#include "timer.h"
#include "node.h"
#include "neighbor.h"
#include "data_objects.h"

/*
  This represents a node's neighbor (in any set)
  - index to the Node
  - hash of the Node's name
  - bloom filter of this node last seen feed set
  - Network coordinates of the node
*/

using namespace std;

/**
 * Initialize neighbor/peer
 */
Neighbor::Neighbor(const node_id_t& node_id, const node_id_t& nbr_id,
  const Coordinate& coord, const Role& role, const BloomFilter& filter) {
  
  _timer = Timer::get_instance();
  assert(_timer != NULL);

  _rng = RNG::get_instance();
  assert(_rng != NULL);

  _node_id = node_id;
  _nbr_id = nbr_id;
  _nc = coord;
  _filter = filter;
  _last_heard_from = _timer->get_time();

  RoleInfo role_info;
  role_info._role = role;
  role_info._last_heard_request = 0LL;
  role_info._last_heard_reply = _timer->get_time();
  role_info._last_sent_request = 0LL;
  
  _roles.push_back(role_info);

  initialize_periodic_checks();
}

void Neighbor::initialize_periodic_checks() {

  PeriodicNeighborCheckEventData* evt_data = new PeriodicNeighborCheckEventData;
  evt_data->neighbor = _nbr_id; // this neighbor

#ifdef DEBUG
  printf("INIT PERIODIC CHECKS for node %s\n", _nbr_id.str().c_str());
#endif // DEBUG

  Event event;
  event.node_id = _node_id; // the node holding the neighbor
  event.layer = LYR_APPLICATION;
  event.flag = EVT_RAPPEL_PERIODIC_NODE_CHECK;
  event.data = evt_data;

/*
  // HACK-ish: this is to prevent a race condition, so that both nodes do not
  // initiate a ping request at the same time: if this is done, both nodes will
  // not send  a ping request in the next time interval (see rappel.cpp code)
  if (_node_id > _nbr_id) {
*/
      assert(globops.is_initialized());
      Clock interval = globops.rappel_heart_beat_interval;
      event.whence = /*_timer->get_time() +*/ _rng->rand_uint(interval/8, 7*interval/8);
/*
  } else {
      event.whence = _timer->get_time() + _rng->rand_uint(DEFAULT_HEART_BEAT_INTERVAL/2, DEFAULT_HEART_BEAT_INTERVAL);
  }
*/
  
  _node_check_event_id = _timer->schedule_event(event);
}

/**
 * Check if the neigbor plays a particular role
 */
bool Neighbor::plays_role(const Role& role) const {

  // make certain peer hass at least 1 role
  assert(_roles.size() > 0);

  // just making sure that if the peer is going to be checked
  assert(_node_check_event_id != NO_SUCH_EVENT_ID);

  for(vector<RoleInfo>::const_iterator it = _roles.begin();
   it != _roles.end(); it++) {

    if (role == it->_role) {
        return true;
    }
  }

  return false;
}

Clock Neighbor::get_last_sent_request_in_role(const Role& role) const {

  // make certain peer hass at least 1 role
  assert(_roles.size() > 0);

  // make sure the role is listed
  assert(plays_role(role));

  for(vector<RoleInfo>::const_iterator it = _roles.begin();
   it != _roles.end(); it++) {

    if (role == it->_role) {
        return it->_last_sent_request;
    }
  }

  assert(false);
  return 0;
}

Clock Neighbor::get_last_heard_request_in_role(const Role& role) const {

  // make certain peer hass at least 1 role
  assert(_roles.size() > 0);

  // make sure the role is listed
  assert(plays_role(role));

  for(vector<RoleInfo>::const_iterator it = _roles.begin();
   it != _roles.end(); it++) {

    if (role == it->_role) {
        return it->_last_heard_request;
    }
  }

  assert(false);
  return 0;
}

Clock Neighbor::get_last_heard_reply_in_role(const Role& role) const {

  // make certain peer hass at least 1 role
  assert(_roles.size() > 0);

  // make sure the role is listed
  assert(plays_role(role));

  for(vector<RoleInfo>::const_iterator it = _roles.begin();
   it != _roles.end(); it++) {

    if (role == it->_role) {
        return it->_last_heard_reply;
    }
  }

  assert(false);
  return 0;
}

Clock Neighbor::get_last_heard_in_role(const Role& role) const {

  // make certain peer hass at least 1 role
  assert(_roles.size() > 0);

  // make sure the role is listed
  assert(plays_role(role));

  for(vector<RoleInfo>::const_iterator it = _roles.begin();
   it != _roles.end(); it++) {

    if (role == it->_role) {
        return max(it->_last_heard_reply, it->_last_heard_request);
    }
  }

  assert(false);
  return 0;
}

void Neighbor::update_sent_request_in_role(const Role& role) {

  // make certain peer hass at least 1 role
  assert(_roles.size() > 0);

  // make sure the role is listed
  assert(plays_role(role));

  _last_heard_from = _timer->get_time();

  for(vector<RoleInfo>::iterator it = _roles.begin();
   it != _roles.end(); it++) {

    if (role == it->_role) {
        it->_last_sent_request = _timer->get_time();
        return;
    }
  }

  assert(false);
}

void Neighbor::update_heard_request_in_role(const Role& role) {

  // make certain peer hass at least 1 role
  assert(_roles.size() > 0);

  // make sure the role is listed
  assert(plays_role(role));

  _last_heard_from = _timer->get_time();

  for(vector<RoleInfo>::iterator it = _roles.begin();
   it != _roles.end(); it++) {

    if (role == it->_role) {
        it->_last_heard_request = _timer->get_time();
        return;
    }
  }

  assert(false);
}

void Neighbor::update_heard_reply_in_role(const Role& role) {

  // make certain peer hass at least 1 role
  assert(_roles.size() > 0);

  // make sure the role is listed
  assert(plays_role(role));

  _last_heard_from = _timer->get_time();

  for(vector<RoleInfo>::iterator it = _roles.begin();
   it != _roles.end(); it++) {

    if (role == it->_role) {
        it->_last_heard_reply = _timer->get_time();
        return;
    }
  }

  assert(false);
}

void Neighbor::add_role(const Role& role) {
  
  // make certain peer hass at least 1 role
  assert(_roles.size() > 0);

  // make sure the role is not already listed
  assert(plays_role(role) == false);

  // add the role
  RoleInfo role_info;
  role_info._role = role;
  role_info._last_heard_request = 0LL;
  role_info._last_heard_reply = _timer->get_time();
  role_info._last_sent_request = 0LL;

  _roles.push_back(role_info);
  
  // make sure the role is now listed
  assert(plays_role(role));
}

/**
 * Remove the node from a role
 * @return returns true if neighbor has no roles remaining (i.e., indicator to
 * remove from a node's peer list)
 */
bool Neighbor::remove_role(const Role& role) {
  
  // make certain peer hass at least 1 role
  assert(_roles.size() > 0);

  // make sure the role is listed
  assert(plays_role(role));

  for(vector<RoleInfo>::iterator it = _roles.begin();
   it != _roles.end(); it++) {

    if (role == it->_role) {

      // remove this role
      _roles.erase(it); // safe operation (break next line)
      break;
    }
  }

  if (_roles.size() == 0) {

    assert(_node_check_event_id != NO_SUCH_EVENT_ID);
    _timer->cancel_event(_node_check_event_id);
    _node_check_event_id = NO_SUCH_EVENT_ID;
    return true;

  } else {

    // make sure the role is no longer listed
    assert(plays_role(role) == false);
  }

  return false;
}

// neighbor.cpp
