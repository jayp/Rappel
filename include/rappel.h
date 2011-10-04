#ifndef _RAPPEL_H
#define _RAPPEL_H

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <vector>
#include <utility>
#include <algorithm>
#include <boost/random.hpp>
#include "rappel_common.h"
#include "topology.h"
#include "timer.h"
#include "transport.h"
#include "bloom_filter.h"
#include "neighbor.h"
#include "feed.h"
#include "coordinate.h"
#include "node.h"
#include "stats.h"

enum RappelInstruction {
  DO_NOTHING              = 0x00000000,
  INVOKE_FAULT_HANDLER    = 0x00000001,
  DONT_SEND_REMOVE_MSG    = 0x00000010,
};


class CandidateAge {

public:

  // operator
  bool operator<(const CandidateAge& rhs) const {
    return (age < rhs.age);
  }

public: //hack
  node_id_t id;
  Clock age;
};


using namespace std;

class Rappel: public Application {

public :
  // construct the node and start appropriate timers
  Rappel(Node* node, bool is_publisher);
  ~Rappel();

  // accessors
  bool is_publisher() const {
    return _is_publisher;
  }
  bool is_subscribed_to(const feed_id_t& feed_id) const {
    return (_feed_set.find(feed_id) != _feed_set.end());
  }
  node_id_t get_a_random_peer() const {
   if (_peers.size() > 0) {
     unsigned int rand = _rng->rand_uint(0, _peers.size() - 1);
     unsigned int counter = 0;
     for (map<node_id_t, Neighbor*>::const_iterator it = _peers.begin();
       it != _peers.end(); it++) {

       if (counter == rand) {
         return it->first;
       }
       counter++;
     }
   }
   return NO_SUCH_NODE; 
  }

  // mutators
  void take_offline();
  void bring_online();
  void bring_online_as_a_landmark(const vector<node_id_t> landmarks);

  void add_feed(const feed_id_t& id);
  void remove_feed(const feed_id_t& id);
#ifndef IGNORE_ETIENNE_STATS
  bool subscribes_to_feed(const feed_id_t& id) { 
    return (_feed_set.find(id) != _feed_set.end());
  }
#endif // !IGNORE_ETIENNE_STATS
  void publish_update(size_t num_bytes);

  void update_own_feed_nc() {

    assert(_is_publisher);
    feed_id_t feed_id = (feed_id_t) _node->get_node_id();
    assert(_feed_set.find(feed_id) != _feed_set.end());
    _feed_set[feed_id]->set_publisher_nc(_node->get_nc());
  }

  int event_callback(Event& event);
  int msg_callback(Message& msg);
  //void signal_nc_changed();
  void debug(unsigned int seq_num);
#if COMPILE_FOR == NETWORK
  void add_feed_rtt_sample(const feed_id_t& feed_id, const Clock& rtt) {
    map<feed_id_t, Feed*>::iterator it = _feed_set.find(feed_id);
    assert(it != _feed_set.end());
    Feed* feed = it->second;
    feed->add_sample_rtt(rtt);
  }
#endif // COMPILE_FOR == NETWORK

private:

  // garbage collect
  int timer_periodic_garbage_collect(Event& event);

  // -------- construct S1
  int timer_periodic_audit(Event& event);
  int receive_bloom_req(Message& recv_msg);
  int timer_wait_after_add_friend_req(Event& event);
  int receive_bloom_reply(Message& recv_msg);
  int timer_wait_after_bloom_req(Event& event);
  int receive_add_friend_req(Message& recv_msg);
  int receive_remove_friend(Message& recv_msg);
  int receive_add_friend_reply(Message& recv_msg);
  int receive_remove_fan(Message& recv_msg);

  // -------- proactive maintenance (PING)
  int timer_periodic_node_check(Event& event);
  int receive_ping_req(Message& recv_msg);
  int timer_wait_after_ping_req(Event& event);
  int receive_ping_reply(Message& recv_msg);

  // -------- tree construction (JOIN)
  int timer_next_feed_join(Event& event);
  int timer_periodic_feed_rejoin(Event& event);
  int receive_feed_join_req(Message& recv_msg);
  int timer_wait_after_feed_join_req(Event& event);
  int receive_feed_join_reply_deny(Message& recv_msg);
  int receive_feed_join_reply_ok(Message& recv_msg);
  int receive_feed_join_reply_fwd(Message& recv_msg);
  int receive_feed_change_parent(Message& recv_msg);
  int receive_feed_no_longer_your_child(Message& recv_msg);
  int receive_feed_flush_ancestry(Message& recv_msg);

  // -------- message dissemination (UPDATE)
  int receive_feed_update(Message& recv_msg);
  int receive_feed_update_pull_req(Message& recv_msg);
  int timer_wait_after_feed_update_pull_req(Event& event);

  // -------- landmarks
  int landmark_send_ping(Event& event);
  int landmark_ping_reply (Message& msg); 
  int landmark_ping_req (Message& msg);

private:

  /*****  instance-ambivalent variables *********/
  // utility singletons
  RNG* _rng;
  Timer* _timer;
  Transport* _transport;

  /****** instance-specific variables ********/
  Node* _node;
  bool _is_publisher; // is node also a publisher?
  bool _virgin;
  map<feed_id_t, Feed*> _feed_set;
  Clock _feed_set_change;
  BloomFilter _filter;

  // garbage collect
  event_id_t _garbage_collection_event_id;

  // feed related
  std::vector<feed_id_t> _pending_feed_joins;
  Clock _last_feed_join_req;
  unsigned int _num_feed_joins;
  event_id_t _next_feed_join_event_id;

  // all neighbors
  map<node_id_t, Neighbor*> _peers;

  // friend set
  vector<node_id_t> _friends;
  map<node_id_t, CandidatePeer*> _candidates; // replacement candidates
  map<node_id_t, CandidatePeer*> _protected_candidates;
  map<node_id_t, Clock> _fans; 
  node_id_t _currently_auditing;
  event_id_t _audit_event_id;
  unsigned int _num_audits;
  unsigned int _rapid_audit_until;
  std::set<node_id_t> _outstanding_bloom_requests;

  // wait after events
  std::map<event_id_t, bool> _pending_events;

  /************* INTERNAL FUNCTIONS *********/

  // debug related to messages
  void safe_send(Message & msg);

  // periodic cleanup
  void schedule_garbage_collection(bool initial);

  // bootstrap
  void bring_online_now();

  // feed
  void schedule_next_feed_join(bool initial);

  // friend audit
  void schedule_audit(bool initial);
  void reschedule_audit_now();

  // peer maintanenance
  bool is_a_peer(const node_id_t& peer_id) const;
  Neighbor* get_peer(const node_id_t& peer_id);
  bool peer_plays_role(const node_id_t& peer_id, const Role& role);
  void add_peer_with_role(const node_id_t& peer_id, const Coordinate& nc,
    const Role& role, const BloomFilter& filter);
  void remove_peer_from_role(const node_id_t& peer_id, const Role& role,
    RappelInstruction instruction);
  void peer_has_failed(const node_id_t& peer_id);
  void send_ping_requests(const node_id_t& nbr);

  // candidate maintanenance
  void add_candidate(const node_id_t& node_id, const Coordinate& candidate_nc);
  void remove_old_candidate();
  node_id_t most_popular_candidate();
  void invalidate_candidate(const node_id_t& node_id);
  void invalidate_all_candidates();

  // bloom filter
  unsigned int bloom_filter_version(const node_id_t& node_id) const;
  bool know_bloom_filter(const node_id_t& node_id) const;
  const BloomFilter* get_bloom_filter(const node_id_t& node_id) const;
  void update_bloom_filter(const node_id_t& node_id, const BloomFilter& filter,
    const Coordinate& nc, bool while_auditing);

  // NC
  bool know_nc(const node_id_t& node_id) const;
  Coordinate get_nc(const node_id_t& node_id) const;

  // fault handlers
  void send_bloom_req(const node_id_t& node_id, bool audit_friend);
  void send_friend_req(const node_id_t& target_node_id, 
    const node_id_t& node_to_prune, const vector<node_id_t>& next_targets);
  void audit();
  void initiate_feed_join_req(const feed_id_t& feed_id, 
    /*const --no*/ map<node_id_t, unsigned int>& attempted,
    vector<node_id_t>& failed, const node_id_t& fwd_node, 
    RappelJoinType join_request);

  // debug functions
  void debug_peers(unsigned int seq_num) const;
  void debug_feeds(unsigned int seq_num) const;

  // utility functions
  std::string generate_random_string(unsigned int size) const;
  vector<node_id_t> find_feed_subscribers(const feed_id_t& feed);
  vector<node_id_t> get_candidate_list() const;
  void flush_ancestry(const feed_id_t& feed_id);
/*
  void contact_ancestor(const feed_id_t& feed_id, const node_id_t& ancestor,
    unsigned int ancestors_remaining);
*/
  void audit_with(const node_id_t& replacement_node);
  double calculate_friend_set_utility(const vector<node_id_t>& nodes) const;

#ifndef IGNORE_ETIENNE_STATS

  void reset_cycle_stats();
  stats_node_per_cycle_t _curr_cycle_stats;

#endif // IGNORE_ETIENNE_STATS

};

#endif // _RAPPEL_H
