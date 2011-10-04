#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <vector>
#include <utility>
#include <map>
#include <algorithm>
#include <ext/algorithm>
#include <boost/random.hpp>
#include "rappel_common.h"
#include "data_objects.h"
#include "timer.h"
#include "transport.h"
#include "rappel.h"
#include "feed.h"
#include "debug.h"
#include "stats.h"

#ifndef IGNORE_ETIENNE_STATS
// extern function prototype
extern std::vector<node_id_t>
  get_online_subscribers_for_feed(const feed_id_t& feed_id);

// global variables
static sqlite3_stmt* g_stats_insert_node_stmt;
static sqlite3_stmt* g_stats_insert_friend_stmt;
static sqlite3_stmt* g_stats_insert_candidate_stmt;
static sqlite3_stmt* g_stats_insert_fan_stmt;
static sqlite3_stmt* g_stats_insert_parent_stmt;
static sqlite3_stmt* g_stats_insert_child_stmt;
#endif // !IGNORE_ETIENNE_STATS

// initialize object
Rappel::Rappel(Node* node, bool is_publisher) {

  assert(node != NULL);
  assert(globops.is_initialized());

  _timer = Timer::get_instance();
  assert(_timer != NULL);
  
  _transport = Transport::get_instance();
  assert(_transport != NULL);

  _rng = RNG::get_instance();
  assert(_rng != NULL);

  _node = node;
  _is_publisher = is_publisher;
  _virgin = true; // used to differentiate between a virgin node and a rejoining node
  _filter.init();

  // All publishers must maintain a feed* element for their own feed
  if (_is_publisher) {

    // create new feed
    feed_id_t feed_id = (feed_id_t) _node->get_node_id();
    Feed* feed = new Feed(feed_id, _node->get_nc());
    _feed_set[feed_id] = feed;

   // A publisher's bloom filter should include a self-subscription.
    _filter.insert(feed->str());
    assert(_filter.contains(feed->str())); // just making sure
  }

  // initialize feed set 
  Clock curr_time = _timer->get_time();
  _feed_set_change = curr_time;

  // garbage collection
  _garbage_collection_event_id = NO_SUCH_EVENT_ID;

  _last_feed_join_req = curr_time - globops.rappel_feed_join_seperation_min - 1;
  _num_feed_joins = 0;
  _next_feed_join_event_id = NO_SUCH_EVENT_ID;

  // friend audit is not in progress
  _currently_auditing = NO_SUCH_NODE; 
  assert(_protected_candidates.size() == 0);
  _audit_event_id = NO_SUCH_EVENT_ID;
  _num_audits = 0;
  _rapid_audit_until = 0;

#ifndef IGNORE_ETIENNE_STATS
  reset_cycle_stats();
#endif // IGNORE_ETIENNE_STATS

}

Rappel::~Rappel() {
  // TODO: unallocate memory
  debug(FINAL_SEQ_NUM); // magic number
}

int Rappel::event_callback(Event& event) {

  assert(event.layer == LYR_APPLICATION);
  assert(_node->is_rappel_online());

  switch (event.flag) {

  case EVT_RAPPEL_PERIODIC_GARBAGE_COLLECT: {
    return timer_periodic_garbage_collect(event);
  }

  case EVT_RAPPEL_PERIODIC_AUDIT: {
    return timer_periodic_audit(event);
  }

  case EVT_RAPPEL_NEXT_FEED_JOIN: {
    return timer_next_feed_join(event);
  }

  case EVT_RAPPEL_PERIODIC_FEED_REJOIN: {
    return timer_periodic_feed_rejoin(event);
  }

  // bloom req expired
  case EVT_RAPPEL_WAIT_AFTER_BLOOM_REQ: {
    return timer_wait_after_bloom_req(event);
  }

  // add friend req expired
  case EVT_RAPPEL_WAIT_AFTER_ADD_FRIEND_REQ: {
    return timer_wait_after_add_friend_req(event);
  }

  case EVT_RAPPEL_PERIODIC_NODE_CHECK: {
    return timer_periodic_node_check(event);
  }

  // ping request expired (i.e., no ping reply)
  case EVT_RAPPEL_WAIT_AFTER_PING_REQ: {
    return timer_wait_after_ping_req(event);
  }

  // join request expired
  case EVT_RAPPEL_WAIT_AFTER_FEED_JOIN_REQ: {
    return timer_wait_after_feed_join_req(event);
  }

#if COMPILE_FOR == NETWORK
  case EVT_RAPPEL_PERIODIC_FEED_RTT_REQ: {

    PeriodicRTTSampleEventData* data = 
      dynamic_cast<PeriodicRTTSampleEventData*>(event.data);
    assert(data != NULL);
    is_subscribed_to(data->feed_id);
    
    return _feed_set[data->feed_id]->send_rtt_to_publisher();
  }
#endif // COMPILE_FOR == NETWORK

  case EVT_RAPPEL_WAIT_AFTER_FEED_UPDATE_PULL_REQ: {
    return timer_wait_after_feed_update_pull_req(event);
  }

  default: {
    assert(false);
    return -1;
  }

  }

  return 0;
}

int Rappel::msg_callback(Message& msg) {
  
  assert(msg.layer == LYR_APPLICATION);
  assert(_node->is_rappel_online());

  switch (msg.flag) {

  case MSG_RAPPEL_BLOOM_REQ: {
#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_in_bloom_req++;
#endif // IGNORE_ETIENNE_STATS
    return receive_bloom_req(msg);
  }
    
  case MSG_RAPPEL_BLOOM_REPLY: {
#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_in_bloom_reply++;
#endif // IGNORE_ETIENNE_STATS
    return receive_bloom_reply(msg);
  }
    
  case MSG_RAPPEL_ADD_FRIEND_REQ: {
#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_in_friend_req++;
#endif // IGNORE_ETIENNE_STATS
    return receive_add_friend_req(msg);
  }
    
  case MSG_RAPPEL_REMOVE_FRIEND: {
#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_in_friend_deletion++;
#endif // IGNORE_ETIENNE_STATS
    return receive_remove_friend(msg);
  }
    
  case MSG_RAPPEL_ADD_FRIEND_REPLY: {
#ifndef IGNORE_ETIENNE_STATS
   // NOTE: stats collected inside function to determin if reply was accepted
   // or rejected
#endif // IGNORE_ETIENNE_STATS
    return receive_add_friend_reply(msg);
  }
    
  case MSG_RAPPEL_REMOVE_FAN: {
    assert(false); // shouldn't happen anymore
#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_in_fan_deletion++;
#endif // IGNORE_ETIENNE_STATS
    return receive_remove_fan(msg);
  }
    
  case MSG_RAPPEL_PING_REQ: {
#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_in_ping_req++;
#endif // IGNORE_ETIENNE_STATS
    return receive_ping_req(msg);
  }
    
  case MSG_RAPPEL_PING_REPLY: {
#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_in_ping_reply++;
#endif // IGNORE_ETIENNE_STATS
    return receive_ping_reply(msg);
  }
    
  case MSG_RAPPEL_FEED_JOIN_REQ: {
#ifndef IGNORE_ETIENNE_STATS
   // NOTE: stats collected inside function to see if repq was a INITIAL_JOIN,
   // REJOIN_DUE_TO_LOST_PARENT, REJOIN_DUE_TO_CHANGE_PARENT, or PERIODIC_REJOIN
#endif // IGNORE_ETIENNE_STATS
    return receive_feed_join_req(msg);
  }
    
  case MSG_RAPPEL_FEED_JOIN_REPLY_DENY: {
#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_in_join_reply_deny++;
#endif // IGNORE_ETIENNE_STATS
    return receive_feed_join_reply_deny(msg);
  }
    
  case MSG_RAPPEL_FEED_JOIN_REPLY_OK: {
#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_in_join_reply_ok++;
#endif // IGNORE_ETIENNE_STATS
    return receive_feed_join_reply_ok(msg);
  }
    
  case MSG_RAPPEL_FEED_JOIN_REPLY_FWD: {
#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_in_join_reply_fwd++;
#endif // IGNORE_ETIENNE_STATS
    return receive_feed_join_reply_fwd(msg);
  }
    
  case MSG_RAPPEL_FEED_CHANGE_PARENT: {
#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_in_change_parent++;
#endif // IGNORE_ETIENNE_STATS
    return receive_feed_change_parent(msg);
  }
    
  case MSG_RAPPEL_FEED_NO_LONGER_YOUR_CHILD: {
#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_in_no_longer_your_child++;
#endif // IGNORE_ETIENNE_STATS
    return receive_feed_no_longer_your_child(msg);
  }
    
  case MSG_RAPPEL_FEED_FLUSH_ANCESTRY: {
#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_in_flush_ancestry++;
#endif // IGNORE_ETIENNE_STATS
    return receive_feed_flush_ancestry(msg);
  }
    
  case MSG_RAPPEL_FEED_UPDATE: {
#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_in_dissemination++;
#endif // IGNORE_ETIENNE_STATS
    return receive_feed_update(msg);
  }
    
  case MSG_RAPPEL_FEED_UPDATE_PULL_REQ: {
#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_in_dissemination_pull_req++;
#endif // IGNORE_ETIENNE_STATS
    return receive_feed_update_pull_req(msg);
  }

  default: {
    assert(false);
    return -1;
  }
  }
  return 0;
}

// ---------------------------------
// -    "NODE ACTION" FUNCTIONS    -
// ---------------------------------

/*
 * CHURN RELATED FUNCTIONS
 */

/**
 * Bring online
 * TODO: send a request to all landmarks to position our nc
 * we should have the vector of nc's node id in the args of the method
 * and propagate it from the driver3 that hold them
 */
void Rappel::bring_online() { 

#ifdef DEBUG
  printf("\n\n############\n");
  printf("BRING ONLINE BEGINS FOR %s", _node->get_node_id().str().c_str());
  printf("\n############\n");
#endif // DEBUG

  bring_online_now();
}

void Rappel::take_offline() {

  assert(_node->is_rappel_online());

#ifdef DEBUG
  printf("GOING OFFLINE\n");

  //debug_peers(0);
#endif // DEBUG


  // from now on, if the node comes back online, it will be rejoining (i.e., a non-virgin)
  _virgin = false;

  // cancel periodic garbage collection
  assert(_garbage_collection_event_id != NO_SUCH_EVENT_ID);
  _timer->cancel_event(_garbage_collection_event_id);
  _garbage_collection_event_id = NO_SUCH_EVENT_ID;

  // cancel next feed join (if true)
  if (_next_feed_join_event_id != NO_SUCH_EVENT_ID) {
    _timer->cancel_event(_next_feed_join_event_id);
    _next_feed_join_event_id = NO_SUCH_EVENT_ID;
  }

  // reset feed join counter
  _num_feed_joins = 0;

  // clear any pending feed join requests
  if (_pending_feed_joins.size() > 0 ) {
#ifdef DEBUG
    printf("Node=%s, going offline with %u joins stranted\n",
      _node->get_node_id().str().c_str(), _pending_feed_joins.size());
#endif // DEBUG
    _pending_feed_joins.clear();
  }

  // cancel each subscribed feed's rejoin timer
  for (map<feed_id_t, Feed*>::iterator it = _feed_set.begin();
    it != _feed_set.end(); it++) {

    Feed* feed = it->second;
    feed->cancel_events();
  }

  // cancel friend audit
  assert(_audit_event_id != NO_SUCH_EVENT_ID);
  _timer->cancel_event(_audit_event_id);
  _audit_event_id = NO_SUCH_EVENT_ID;

  // cancel ping timer (for peers)
  for (map<node_id_t, Neighbor*>::iterator it = _peers.begin();
    it != _peers.end(); it++) {

    Neighbor* peer = it->second;

#ifdef DEBUG
    printf("dbg: node %s trying to cancel event timer for peer %s\n",
      _node->get_node_id().str().c_str(), peer->get_id().str().c_str());
#endif // DEBUG

    _timer->cancel_event(peer->get_node_check_event_id());
  }

  // clear all pending events
  for (map<event_id_t, bool>::iterator it = _pending_events.begin();
    it != _pending_events.end(); it++) {

    // cancel event, if not already canceled via a reply
    _timer->cancel_event(it->first);
  }
  _pending_events.clear();
}

/**
 * Only bring a node online after it has boostrapped its NC
 */
void Rappel::bring_online_now() {

#ifdef DEBUG
  printf("\n\n############\n");
  printf("BRING ONLINE [[[[[NOW]]]]]]] BEGINS FOR %s", _node->get_node_id().str().c_str());
  printf("\n############\n");

  //debug_peers(0);
#endif // DEBUG

  /////////////////// GENERAL //////////////////////////////////

  // sync app and sys coords
  _node->get_nc().sync();
  
  // schedule garbage collection
  schedule_garbage_collection(true);

  ///////////////////////////// Friend ///////////////////////////

  // mark all candidates as not tested already: DO NOT REMOVE THEM!
  invalidate_all_candidates();

  // remove any and protected s1 candidates
  for (map<node_id_t, CandidatePeer*>::iterator it = _protected_candidates.begin();
    it != _protected_candidates.end(); it++) {

    // free up memory pointed by CandidatePeer
    delete(it->second);
  }

  // clean up all entries
  _protected_candidates.clear();

  // send request to old s1 peers, one by one
  // for friend set: discard friends and send a request for re-addition 
 if (_friends.size() > 0) {

#ifdef DEBUG
    printf("asking friend %s to be back in friend set\n", _friends[0].str().c_str());
#endif // DEBUG

    // queue up the next targets: not the first one
    vector<node_id_t> next_targets;
    for (unsigned int i = 1; i < _friends.size(); i++) {
      next_targets.push_back(_friends[i]);
    }

    // protect all these targets
    for (unsigned int i = 0; i < _friends.size(); i++) {

      assert(get_bloom_filter(_friends[i])->get_version() > 0);

      CandidatePeer* cp = new CandidatePeer(_friends[i], get_nc(_friends[i]));
      cp->set_bloom_filter(*(get_bloom_filter(_friends[i])));

      _protected_candidates[_friends[i]] = cp;
    }

    // send request to old s1 peers (one-by-one)
    _currently_auditing = _friends[0];
/*
    cout << "1 time=" << _timer->get_time_str() << "; node=" << _node->get_node_id().str()
      << "; currently_auditing=" << _currently_auditing.str() << endl;
*/
    send_friend_req(_friends[0], NO_SUCH_NODE, next_targets);
  }

  // remove all old s1 peers
  for (vector<node_id_t>::iterator it = _friends.begin(); _friends.size() > 0
    && it != _friends.end(); /* */)  {

#ifdef DEBUG
    printf("removing friend %s from friend set\n", it->str().c_str());
#endif // DEBUG

    // remove peer from the moment
    Role friend_role(ROLE_FRIEND);
   // HOLY SHEET [BUG DISCOVERED]: can't pass a dereferenced iterator by reference!!!
    node_id_t peer_id = *it;
    remove_peer_from_role(peer_id, friend_role, DO_NOTHING);
  }

  // all _friends peers should be cleared via remove_peer_from_role
  assert(_friends.size() == 0);

  // discard incoming s1 set
  _fans.clear();

  // schedule rapid improvements of s1
  _rapid_audit_until = _num_audits + 2 * globops.rappel_num_friends;
  schedule_audit(true); 

  /////////////////////////// FEEDS ///////////////////////////

  if (_is_publisher) { // update coords for our own feed
    update_own_feed_nc();
  }

  if (_virgin) {

    if (_pending_feed_joins.size() > 0) {

      // attempt to join the first feed
      vector<feed_id_t>::iterator it = _pending_feed_joins.begin();
      feed_id_t feed_id = *it;
      _pending_feed_joins.erase(it);

      // TODO: this should be true as long as remove_feed is not implemented
      assert(_feed_set.find(feed_id) != _feed_set.end());

      assert(_next_feed_join_event_id == NO_SUCH_EVENT_ID);
      _last_feed_join_req = _timer->get_time();
      assert(_num_feed_joins == 0);
      _num_feed_joins = 1;

      map<node_id_t, unsigned int> attempted;
      vector<node_id_t> failed;
      initiate_feed_join_req(feed_id, attempted, failed, NO_SUCH_NODE, INITIAL_JOIN);

      schedule_next_feed_join(true);
    }
  } else { // a rejoining node

    // for all feeds: discard children, and attempt a rejoin at the
    // old parent ; discard also the ancestor set
    for (map<feed_id_t, Feed*>::iterator it = _feed_set.begin();
      it != _feed_set.end(); it++) {

      feed_id_t feed_id = it->first;
      Feed* feed = it->second;
      
      // discard children
      vector<node_id_t> children = feed->get_children();
      for (vector<node_id_t>::iterator itC = children.begin();
        itC != children.end(); itC++) {
  
        Role child_role(ROLE_FEED_CHILD, feed->get_feed_id());
        node_id_t child_id = *itC;
        remove_peer_from_role(child_id, child_role, DO_NOTHING);
      }
  
      // replace parent and remove ancestors (exept if we are the publisher)
      if (feed->get_publisher() != _node->get_node_id()) {
  
        // no longer seeking a parent (if previously)
        feed->unset_seeking_parent();
#if COMPILE_FOR == NETWORK
        feed->schedule_rtt_to_publisher(true);
#endif // COMPILE_FOR == NETWORK
  
        if (_pending_feed_joins.size() > 0
           || _last_feed_join_req + globops.rappel_feed_join_seperation_min
            > _timer->get_time() ) {

          // add incoming feed to _pending list if:
          // (1) there are already other pending feeds, or
          // (2) if we joined the last feed recently -- this is reqd because
          // pending_feed_joins.size() == 0 when the first request is being
          // processed

          assert(_next_feed_join_event_id != NO_SUCH_EVENT_ID);
          _pending_feed_joins.push_back(feed_id);

        } else {

          assert(_next_feed_join_event_id == NO_SUCH_EVENT_ID);
          _last_feed_join_req = _timer->get_time();
          assert(_num_feed_joins == 0);
          _num_feed_joins = 1;

          // replace the parent for that feed
          node_id_t old_parent = NO_SUCH_NODE;
          if (feed->has_parent()) {

            old_parent = feed->get_parent();

            Role parent_role(ROLE_FEED_PARENT, feed->get_feed_id());
            remove_peer_from_role(old_parent, parent_role, DO_NOTHING);
          }

          map<node_id_t, unsigned int> attempted;
          vector<node_id_t> failed;
          initiate_feed_join_req(feed_id, attempted, failed, old_parent, INITIAL_JOIN);

          // just in case (the next add request comes in soon after)
          schedule_next_feed_join(true);
        }
  
        // JAY: why? is this necessary?
        // clear ancestors for that feed
        //feed->clear_ancestors();
      }
    }
  } 
 
#ifdef DEBUG
  // print _peers set
  printf("_peers content\n");
  unsigned int iii = 0;
  for (map<node_id_t, Neighbor*>::iterator itP = _peers.begin();
    itP != _peers.end(); itP++) {

    printf("%d: %s is ", iii++, itP->first.str().c_str());
    vector<Role> r = itP->second->get_roles();
    for (vector<Role>::iterator itR (r.begin()); itR != r.end(); itR++) {
      printf("%s ",itR->get_role_type_str());
    }
  }
  printf("\n");

  printf("\n\n############\n");
  printf("BRING ONLINE ENDS FOR %s", _node->get_node_id().str().c_str());
  printf("\n############\n");
#endif // DEBUG
}

void Rappel::add_feed(const feed_id_t& feed_id) {

#ifdef DEBUG
  printf("adding feed %s to node %s\n", feed_id.str().c_str(),
    _node->get_node_id().str().c_str());
#endif // DEBUG

  // dont add own feed if publisher
  assert((node_id_t) feed_id != _node->get_node_id());

  // create a new feed entry -- update bloom filter
  Feed* feed = new Feed(_node->get_node_id(), feed_id);

  // make sure the feed doesn't already exist -- then add it
  assert(_feed_set.find(feed_id) == _feed_set.end());
  _feed_set[feed_id] = feed;
  
  // update bloom filter
  _filter.insert(feed->str());
  //assert(_filter.contains(feed->str())); // just making sure
  _feed_set_change = _timer->get_time();

  // invalidate all candidates, change in bloom filter
  _rapid_audit_until = _num_audits + 2 * globops.rappel_num_friends;
  reschedule_audit_now();
  invalidate_all_candidates();

  // find the parent for the first feed, right away delay the next feeds
  // (join only gradually -- so that the node can improve its s1 set)

  if (_pending_feed_joins.size() > 0
    || _last_feed_join_req + globops.rappel_feed_join_seperation_min > _timer->get_time()) {

    // add incoming feed to _pending list if:
    // (1) there are already other pending feeds, or
    // (2) if we joined the last feed recently -- this is reqd because
    // pending_feed_joins.size() == 0 when the first request is being processed

    assert(_next_feed_join_event_id != NO_SUCH_EVENT_ID);
    _pending_feed_joins.push_back(feed_id);

  } else {

    assert(_next_feed_join_event_id == NO_SUCH_EVENT_ID);
    _last_feed_join_req = _timer->get_time();
    assert(_num_feed_joins == 0);
    _num_feed_joins = 1;

    map<node_id_t, unsigned int> attempted;
    vector<node_id_t> failed;
    initiate_feed_join_req(feed_id, attempted, failed, NO_SUCH_NODE, INITIAL_JOIN);

    // just in case (the next add request comes in soon after)
    schedule_next_feed_join(true);
  }
}

void Rappel::remove_feed(const feed_id_t& feed) {
  // not implemented for the moment
  assert (false);
}

void Rappel::publish_update(size_t num_bytes) {

  assert(_is_publisher);

  map<feed_id_t, Feed*>::iterator
    it = _feed_set.find((feed_id_t) _node->get_node_id());
  assert(it != _feed_set.end()); // this should exist
  Feed* feed = it->second;

  unsigned int update_seq_num = feed->get_latest_update_seq() + 1;
#if COMPILE_FOR == SIMULATOR
  string update; // no need to actually use up memory
#elif COMPILE_FOR == NETWORK
  string update = generate_random_string(num_bytes);
#endif // COMPILE_FOR
  feed->add_entry(update_seq_num, update, _timer->get_sync_time());
  unsigned int entries_sent = feed->send_entry_to_children(update_seq_num);

#ifndef IGNORE_ETIENNE_STATS
  _curr_cycle_stats.msgs_out_dissemination += entries_sent; 
#endif // IGNORE_ETIENNE_STATS

  //Etienne: print out a message about the new update (
  // for stretch ratio calculations)
#ifdef DEBUG
  printf("[Time=%s] SEND UPDATE_%s_%u_ @ %s\n",
	 _timer->get_time_str().c_str(),
	 feed->str().c_str(), 
	 update_seq_num,
	 _node->get_node_id().str().c_str());
#endif // DEBUG
  
#ifndef IGNORE_ETIENNE_STATS

#if COMPILE_FOR == SIMULATOR
  vector<node_id_t> feed_subscribers =
    get_online_subscribers_for_feed(feed->get_feed_id());

  map<node_id_t, stats_node_per_update_t> curr_recv_node_stats;

  // iterate through each subscriber
  for (vector<node_id_t>::iterator it_sub = feed_subscribers.begin();
    it_sub != feed_subscribers.end(); it_sub++) {

    stats_node_per_update_t curr_node_stats;

    curr_node_stats.first_reception_delay = -1 * MILLI_SECOND;
    curr_node_stats.first_reception_is_pulled = false;
    curr_node_stats.ideal_reception_delay = _transport->get_route_delay_native(
      _node->get_node_id(), *it_sub);
    curr_node_stats.last_reception_delay = -1 * MILLI_SECOND;
    curr_node_stats.num_receptions_pulled = 0;
    curr_node_stats.num_receptions_pushed = 0;
    curr_node_stats.first_reception_height = 10000;

    curr_recv_node_stats[*it_sub] = curr_node_stats;
  }

  stats_feed_update_t curr_feed_update;

  curr_feed_update.sync_emission_time = _timer->get_time();
  curr_feed_update.num_subscribers = feed_subscribers.size();
  curr_feed_update.recv_node_stats = curr_recv_node_stats;

  // stats for this feed should exist
  map<feed_id_t, stats_feed_t>::iterator
    it_feed_stat = feed_stats.find(feed->get_feed_id());
  assert(it_feed_stat != feed_stats.end()); 

  // stats for this update seq num should _NOT_ exist
  map<unsigned int, stats_feed_update_t>::iterator
    it_update_stat = it_feed_stat->second.updates.find(update_seq_num);
  assert(it_update_stat == it_feed_stat->second.updates.end());
 
  it_feed_stat->second.updates[update_seq_num] = curr_feed_update;
  //printf("[DBG] now there are stats for record for %u updates for feed=%s\n",
  //  it_feed_stat->second.updates.size(), feed->get_feed_id().str().c_str());
#endif // COMPILE_FOR == SIMULATOR

#endif // !IGNORE_ETIENNE_STATS
}

// ---------------------------------
// -    PROTOCOL FUNCTIONS         -
// ---------------------------------

void Rappel::debug(unsigned int seq_num) {

  Clock curr_time = _timer->get_time();

#ifdef DEBUG_DISABLED
  printf("[Time=%s][node=%s] PERIODIC DEBUG\n",
    _timer->get_time_str(curr_time).c_str(), _node->get_node_id().str().c_str());

  printf("[Rappel => %s] Coordinate => ", _node->get_node_id().str().c_str());
  _node->get_nc().print();
  printf("\n");

  if (_is_publisher) {
  //  printf("I am publisher.\n");
  }

  //debug_peers(seq_num);
  //debug_feeds(seq_num);
#endif //DEBUG

#ifndef IGNORE_ETIENNE_STATS

  // update stats
  // keep stats iff the node is online otherwise they have no interest
  if (_node->is_rappel_online()) {

    // prepare -- one time cost
    if (g_stats_insert_node_stmt == NULL) {

      // messages
      string sql_insert_node(
        "INSERT INTO node_per_cycle(node_id, seq_num, sync_time, "
  
        // friend stuff
        "friend_set_utility, num_friends, num_fans, num_candidates, "
        "msgs_in_friend_req, msgs_in_friend_reply_accepted, "
          "msgs_in_friend_reply_rejected, msgs_in_friend_deletion, "
          "msgs_in_fan_deletion, " 
        "msgs_out_friend_req, msgs_out_friend_reply_accepted, "
          "msgs_out_friend_reply_rejected, msgs_out_friend_deletion, "
          "msgs_out_fan_deletion, " 
  
        // tree construction messages
        "msgs_in_join_req, msgs_in_lost_parent_rejoin_req, "
          "msgs_in_change_parent_rejoin_req, msgs_in_periodic_rejoin_req, "
          "msgs_in_join_reply_ok, msgs_in_join_reply_deny, "
          "msgs_in_join_reply_fwd, "
        "msgs_out_join_req, msgs_out_lost_parent_rejoin_req, "
          "msgs_out_change_parent_rejoin_req, msgs_out_periodic_rejoin_req, "
          "msgs_out_join_reply_ok, msgs_out_join_reply_deny, "
          "msgs_out_join_reply_fwd, "
        "msgs_in_change_parent, msgs_in_no_longer_your_child, "
          "msgs_in_flush_ancestry, "
        "msgs_out_change_parent, msgs_out_no_longer_your_child, "
          "msgs_out_flush_ancestry, "
        "msgs_in_bloom_req, msgs_in_bloom_reply, "
        "msgs_out_bloom_req, msgs_out_bloom_reply, "
  
         // ping messages
        "msgs_in_ping_req, msgs_in_ping_reply, "
        "msgs_out_ping_req, msgs_out_ping_reply, "
  
        // dissemination messages
        "msgs_in_dissemination, msgs_in_dissemination_pull_req, "
        "msgs_out_dissemination, msgs_out_dissemination_pull_req"

       // here for now (should move it with the other tree msgs) -- for stats_v2_db
       ", msgs_in_redirected_periodic_rejoin_req"
  
#if COMPILE_FOR == NETWORK
        ", local_time"
#endif // COMPILE_FOR == NETWORK

        ") VALUES (?, ?, ?, "
  
        // friend stuff
        "?, ?, ?, ?, "
        "?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, "
  
        // tree construction
        "?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, "
        "?, ?, ?, "
        "?, ?, "
        "?, ?, "
  
        // ping messages
        "?, ?, "
        "?, ?, "
  
        // dissemination messages
        "?, ?, "
        "?, ?"
 
       // here for now (should move it with the other tree msgs) -- for stats_v2_db
        ", ?"
        
#if COMPILE_FOR == NETWORK
        ", ?"
#endif // COMPILE_FOR == NETWORK

        ")");

      if (sqlite3_prepare(stats_db, sql_insert_node.c_str(),
        -1, &g_stats_insert_node_stmt, NULL) != SQLITE_OK) {

        std::cerr << "[[[" << sql_insert_node << "]]]" << std::endl;
        assert(false);
      }
     }

#if COMPILE_FOR == SIMULATOR
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 1,
      _node->get_node_id().get_address()) == SQLITE_OK);
#elif COMPILE_FOR == NETWORK
    assert(sqlite3_bind_text(g_stats_insert_node_stmt, 1,
      _node->get_node_id().str().c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);
#endif
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 2,
      seq_num) == SQLITE_OK);
    assert(sqlite3_bind_int64(g_stats_insert_node_stmt, 3,
      _timer->get_sync_time(curr_time)) == SQLITE_OK);

    assert(sqlite3_bind_double(g_stats_insert_node_stmt, 4,
      calculate_friend_set_utility(_friends)) == SQLITE_OK);
/*
    // Note: not written to db as it is the same as another metric (verfied)
    // Note2: may assert. as msgs are counted prior to callbacks.
       the reply msg may be late beyond rappel_timeout, and thus
       be ignored -- hence friend set changes maybe different
    assert(_curr_cycle_stats.friend_set_changes
      == _curr_cycle_stats.msgs_in_friend_reply_accepted);
*/
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 5,
      _friends.size()) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 6,
      _fans.size()) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 7,
      _candidates.size()) == SQLITE_OK);

    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 8,
      _curr_cycle_stats.msgs_in_friend_req) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 9,
      _curr_cycle_stats.msgs_in_friend_reply_accepted) == SQLITE_OK); 
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 10,
      _curr_cycle_stats.msgs_in_friend_reply_rejected) == SQLITE_OK); 
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 11,
      _curr_cycle_stats.msgs_in_friend_deletion) == SQLITE_OK); 
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 12,
      _curr_cycle_stats.msgs_in_fan_deletion) == SQLITE_OK); 

    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 13,
      _curr_cycle_stats.msgs_out_friend_req) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 14,
      _curr_cycle_stats.msgs_out_friend_reply_accepted) == SQLITE_OK); 
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 15,
      _curr_cycle_stats.msgs_out_friend_reply_rejected) == SQLITE_OK); 
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 16,
      _curr_cycle_stats.msgs_out_friend_deletion) == SQLITE_OK); 
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 17,
      _curr_cycle_stats.msgs_out_fan_deletion) == SQLITE_OK); 

    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 18,
      _curr_cycle_stats.msgs_in_join_req) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 19,
      _curr_cycle_stats.msgs_in_lost_parent_rejoin_req) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 20,
      _curr_cycle_stats.msgs_in_change_parent_rejoin_req) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 21,
      _curr_cycle_stats.msgs_in_periodic_rejoin_req) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 22,
      _curr_cycle_stats.msgs_in_join_reply_ok) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 23,
      _curr_cycle_stats.msgs_in_join_reply_deny) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 24,
      _curr_cycle_stats.msgs_in_join_reply_fwd) == SQLITE_OK);

    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 25,
      _curr_cycle_stats.msgs_out_join_req) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 26,
      _curr_cycle_stats.msgs_out_lost_parent_rejoin_req) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 27,
      _curr_cycle_stats.msgs_out_change_parent_rejoin_req) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 28,
      _curr_cycle_stats.msgs_out_periodic_rejoin_req) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 29,
      _curr_cycle_stats.msgs_out_join_reply_ok) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 30,
      _curr_cycle_stats.msgs_out_join_reply_deny) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 31,
      _curr_cycle_stats.msgs_out_join_reply_fwd) == SQLITE_OK);

    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 32,
      _curr_cycle_stats.msgs_in_change_parent) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 33,
      _curr_cycle_stats.msgs_in_no_longer_your_child) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 34,
      _curr_cycle_stats.msgs_in_flush_ancestry) == SQLITE_OK);

    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 35,
      _curr_cycle_stats.msgs_out_change_parent) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 36,
      _curr_cycle_stats.msgs_out_no_longer_your_child) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 37,
      _curr_cycle_stats.msgs_out_flush_ancestry) == SQLITE_OK);

    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 38,
      _curr_cycle_stats.msgs_in_bloom_req) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 39,
      _curr_cycle_stats.msgs_in_bloom_reply) == SQLITE_OK);

    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 40,
      _curr_cycle_stats.msgs_out_bloom_req) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 41,
      _curr_cycle_stats.msgs_out_bloom_reply) == SQLITE_OK);

    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 42,
      _curr_cycle_stats.msgs_in_ping_req) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 43,
      _curr_cycle_stats.msgs_in_ping_reply) == SQLITE_OK);

    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 44,
      _curr_cycle_stats.msgs_out_ping_req) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 45,
      _curr_cycle_stats.msgs_out_ping_reply) == SQLITE_OK);
  
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 46,
      _curr_cycle_stats.msgs_in_dissemination) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 47,
      _curr_cycle_stats.msgs_in_dissemination_pull_req) == SQLITE_OK);
  
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 48,
      _curr_cycle_stats.msgs_out_dissemination) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 49,
      _curr_cycle_stats.msgs_out_dissemination_pull_req) == SQLITE_OK);

    // here for now (should move it with the other tree msgs) -- for stats_v2_db
    assert(sqlite3_bind_int(g_stats_insert_node_stmt, 50,
      _curr_cycle_stats.msgs_in_redirected_periodic_rejoin_req) == SQLITE_OK);

#if COMPILE_FOR == NETWORK
    assert(sqlite3_bind_int64(g_stats_insert_node_stmt, 51,
      curr_time) == SQLITE_OK);
#endif // COMPILE_FOR == NETWORK

    assert(sqlite3_step(g_stats_insert_node_stmt) == SQLITE_DONE);
    sqlite3_reset(g_stats_insert_node_stmt);

    // friends
    // prepare -- one time cost
    if (g_stats_insert_friend_stmt == NULL) {

      string sql_insert_node_friend(
        "INSERT INTO node_friends_per_cycle(node_id, seq_num, sync_time, "
          "friend_id"
#if COMPILE_FOR == SIMULATOR
          ") VALUES (?, ?, ?, ?)");
#elif COMPILE_FOR == NETWORK
          ", local_time) VALUES (?, ?, ?, ?, ?)");
#endif // COMPILE_FOR

      assert(sqlite3_prepare(stats_db, sql_insert_node_friend.c_str(),
        -1, &g_stats_insert_friend_stmt, NULL) == SQLITE_OK);
    }

    for (vector<node_id_t>::const_iterator it_friend = _friends.begin();
      it_friend != _friends.end(); it_friend++) {

#if COMPILE_FOR == SIMULATOR
      assert(sqlite3_bind_int(g_stats_insert_friend_stmt, 1,
        _node->get_node_id().get_address()) == SQLITE_OK);
#elif COMPILE_FOR == NETWORK
      assert(sqlite3_bind_text(g_stats_insert_friend_stmt, 1,
        _node->get_node_id().str().c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);
#endif
      assert(sqlite3_bind_int(g_stats_insert_friend_stmt, 2,
        seq_num) == SQLITE_OK);
      assert(sqlite3_bind_int64(g_stats_insert_friend_stmt, 3,
        _timer->get_sync_time(curr_time)) == SQLITE_OK);
#if COMPILE_FOR == SIMULATOR
      assert(sqlite3_bind_int(g_stats_insert_friend_stmt, 4,
        it_friend->get_address()) == SQLITE_OK);
#elif COMPILE_FOR == NETWORK
      assert(sqlite3_bind_text(g_stats_insert_friend_stmt, 4,
        it_friend->str().c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);
      assert(sqlite3_bind_int64(g_stats_insert_friend_stmt, 5,
        curr_time) == SQLITE_OK);
#endif

      assert(sqlite3_step(g_stats_insert_friend_stmt) == SQLITE_DONE);
      sqlite3_reset(g_stats_insert_friend_stmt);
    }

    // candidates
    // prepare -- one time cost
    if (g_stats_insert_candidate_stmt == NULL) {

      string sql_insert_node_candidate(
        "INSERT INTO node_candidates_per_cycle(node_id, seq_num, sync_time, "
          "candidate_id"
#if COMPILE_FOR == SIMULATOR
          ") VALUES (?, ?, ?, ?)");
#elif COMPILE_FOR == NETWORK
          ", local_time) VALUES (?, ?, ?, ?, ?)");
#endif // COMPILE_FOR

      assert(sqlite3_prepare(stats_db, sql_insert_node_candidate.c_str(),
        -1, &g_stats_insert_candidate_stmt, NULL) == SQLITE_OK);
    }

    for (map<node_id_t, CandidatePeer*>::const_iterator
      it_candidate = _candidates.begin(); it_candidate != _candidates.end();
      it_candidate++) {
  
#if COMPILE_FOR == SIMULATOR
      assert(sqlite3_bind_int(g_stats_insert_candidate_stmt, 1,
        _node->get_node_id().get_address()) == SQLITE_OK);
#elif COMPILE_FOR == NETWORK
      assert(sqlite3_bind_text(g_stats_insert_candidate_stmt, 1,
        _node->get_node_id().str().c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);
#endif
      assert(sqlite3_bind_int(g_stats_insert_candidate_stmt, 2,
        seq_num) == SQLITE_OK);
      assert(sqlite3_bind_int64(g_stats_insert_candidate_stmt, 3,
        _timer->get_sync_time(curr_time)) == SQLITE_OK);
#if COMPILE_FOR == SIMULATOR
      assert(sqlite3_bind_int(g_stats_insert_candidate_stmt, 4,
        it_candidate->first.get_address()) == SQLITE_OK);
#elif COMPILE_FOR == NETWORK
      assert(sqlite3_bind_text(g_stats_insert_candidate_stmt, 4,
        it_candidate->first.str().c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);
      assert(sqlite3_bind_int64(g_stats_insert_candidate_stmt, 5,
        curr_time) == SQLITE_OK);
#endif

      assert(sqlite3_step(g_stats_insert_candidate_stmt) == SQLITE_DONE);
      sqlite3_reset(g_stats_insert_candidate_stmt);
    }

    // fans
    // prepare -- one time cost
    if (g_stats_insert_fan_stmt == NULL) {

      string sql_insert_node_fan(
        "INSERT INTO node_fans_per_cycle(node_id, seq_num, sync_time, fan_id"
#if COMPILE_FOR == SIMULATOR
          ") VALUES (?, ?, ?, ?)");
#elif COMPILE_FOR == NETWORK
          ", local_time) VALUES (?, ?, ?, ?, ?)");
#endif // COMPILE_FOR

      assert(sqlite3_prepare(stats_db, sql_insert_node_fan.c_str(),
        -1, &g_stats_insert_fan_stmt, NULL) == SQLITE_OK);
    }

    for (map<node_id_t, Clock>::const_iterator it_fan =_fans.begin();
      it_fan != _fans.end(); it_fan++) {
  
#if COMPILE_FOR == SIMULATOR
      assert(sqlite3_bind_int(g_stats_insert_fan_stmt, 1,
        _node->get_node_id().get_address()) == SQLITE_OK);
#elif COMPILE_FOR == NETWORK
      assert(sqlite3_bind_text(g_stats_insert_fan_stmt, 1,
        _node->get_node_id().str().c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);
#endif
      assert(sqlite3_bind_int(g_stats_insert_fan_stmt, 2,
        seq_num) == SQLITE_OK);
      assert(sqlite3_bind_int64(g_stats_insert_fan_stmt, 3,
        _timer->get_sync_time(curr_time)) == SQLITE_OK);
#if COMPILE_FOR == SIMULATOR
      assert(sqlite3_bind_int(g_stats_insert_fan_stmt, 4,
        it_fan->first.get_address()) == SQLITE_OK);
#elif COMPILE_FOR == NETWORK
      assert(sqlite3_bind_text(g_stats_insert_fan_stmt, 4,
        it_fan->first.str().c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);
      assert(sqlite3_bind_int64(g_stats_insert_fan_stmt, 5,
        curr_time) == SQLITE_OK);
#endif

      assert(sqlite3_step(g_stats_insert_fan_stmt) == SQLITE_DONE);
      sqlite3_reset(g_stats_insert_fan_stmt);
    }

    // parent
    // prepare -- one time cost
    if (g_stats_insert_parent_stmt == NULL) {

      string sql_insert_node_parent(
        "INSERT INTO node_parent_per_cycle(node_id, seq_num, sync_time, "
          "feed_id, parent_id, tree_height, num_children, direct_cost, path_cost"
#if COMPILE_FOR == SIMULATOR
          ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
#elif COMPILE_FOR == NETWORK
          ", local_time) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
#endif // COMPILE_FOR

      assert(sqlite3_prepare(stats_db, sql_insert_node_parent.c_str(),
        -1, &g_stats_insert_parent_stmt, NULL) == SQLITE_OK);
    }

    // children
    // prepare -- one time cost
    if (g_stats_insert_child_stmt == NULL) {
  
      string sql_insert_node_child(
        "INSERT INTO node_children_per_cycle(node_id, seq_num, sync_time, "
          "feed_id, child_id"
#if COMPILE_FOR == SIMULATOR
          ") VALUES (?, ?, ?, ?, ?)");
#elif COMPILE_FOR == NETWORK
          ", local_time) ""VALUES (?, ?, ?, ?, ?, ?)");
#endif // COMPILE_FOR

      assert(sqlite3_prepare(stats_db, sql_insert_node_child.c_str(),
        -1, &g_stats_insert_child_stmt, NULL) == SQLITE_OK);
    }
  
    for (map<feed_id_t, Feed*>::const_iterator it_feed = _feed_set.begin();
      it_feed != _feed_set.end(); it_feed++) {

      node_id_t parent = NO_SUCH_NODE;
      double direct_cost = -1.0;
      double path_cost = -1.0;
      unsigned int tree_height = 10000;

      Feed* feed = it_feed->second;
      if (feed->has_parent()) {
        parent = feed->get_parent();
        direct_cost = _node->get_nc().get_distance(feed->get_publisher_nc());
        path_cost = feed->get_path_cost();
        tree_height = feed->get_ancestors().size();
      }

#if COMPILE_FOR == SIMULATOR
      assert(sqlite3_bind_int(g_stats_insert_parent_stmt, 1,
        _node->get_node_id().get_address()) == SQLITE_OK);
#elif COMPILE_FOR == NETWORK
      assert(sqlite3_bind_text(g_stats_insert_parent_stmt, 1,
        _node->get_node_id().str().c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);
#endif
      assert(sqlite3_bind_int(g_stats_insert_parent_stmt, 2,
        seq_num) == SQLITE_OK);
      assert(sqlite3_bind_int64(g_stats_insert_parent_stmt, 3,
        _timer->get_sync_time(curr_time)) == SQLITE_OK);
#if COMPILE_FOR == SIMULATOR
      assert(sqlite3_bind_int(g_stats_insert_parent_stmt, 4,
        it_feed->first.get_address()) == SQLITE_OK);
      assert(sqlite3_bind_int(g_stats_insert_parent_stmt, 5,
        parent.get_address()) == SQLITE_OK);
#elif COMPILE_FOR == NETWORK
      assert(sqlite3_bind_text(g_stats_insert_parent_stmt, 4,
       it_feed->first.str().c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);
      assert(sqlite3_bind_text(g_stats_insert_parent_stmt, 5,
        parent.str().c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);
#endif
      assert(sqlite3_bind_int(g_stats_insert_parent_stmt, 6,
        tree_height) == SQLITE_OK);
      assert(sqlite3_bind_int(g_stats_insert_parent_stmt, 7,
        feed->get_num_children()) == SQLITE_OK);
      assert(sqlite3_bind_double(g_stats_insert_parent_stmt, 8,
        direct_cost) == SQLITE_OK);
      assert(sqlite3_bind_double(g_stats_insert_parent_stmt, 9,
        path_cost) == SQLITE_OK);
#if COMPILE_FOR == NETWORK
      assert(sqlite3_bind_int64(g_stats_insert_parent_stmt, 10,
        curr_time) == SQLITE_OK);
#endif // COMPILE_FOR == NETWORK


      assert(sqlite3_step(g_stats_insert_parent_stmt) == SQLITE_DONE);
      sqlite3_reset(g_stats_insert_parent_stmt);

      // feed -- children
      vector<node_id_t> children = feed->get_children();
      for (unsigned i = 0; i < children.size(); i++) {

#if COMPILE_FOR == SIMULATOR
        assert(sqlite3_bind_int(g_stats_insert_child_stmt, 1,
          _node->get_node_id().get_address()) == SQLITE_OK);
#elif COMPILE_FOR == NETWORK
        assert(sqlite3_bind_text(g_stats_insert_child_stmt, 1,
          _node->get_node_id().str().c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);
#endif
        assert(sqlite3_bind_int(g_stats_insert_child_stmt, 2,
          seq_num) == SQLITE_OK);
        assert(sqlite3_bind_int64(g_stats_insert_child_stmt, 3,
          _timer->get_sync_time(curr_time)) == SQLITE_OK);
#if COMPILE_FOR == SIMULATOR
        assert(sqlite3_bind_int(g_stats_insert_child_stmt, 4,
          it_feed->first.get_address()) == SQLITE_OK);
        assert(sqlite3_bind_int(g_stats_insert_child_stmt, 5,
          children[i].get_address()) == SQLITE_OK);
#elif COMPILE_FOR == NETWORK
        assert(sqlite3_bind_text(g_stats_insert_child_stmt, 4,
          it_feed->first.str().c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);
        assert(sqlite3_bind_text(g_stats_insert_child_stmt, 5,
          children[i].str().c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);
        assert(sqlite3_bind_int64(g_stats_insert_child_stmt, 6,
          curr_time) == SQLITE_OK);
#endif
  
        assert(sqlite3_step(g_stats_insert_child_stmt) == SQLITE_DONE);
        sqlite3_reset(g_stats_insert_child_stmt);

      } // all children
    } // all feeds
  } // if node is online

  reset_cycle_stats();

#endif // IGNORE_ETIENNE_STATS
}

#ifndef IGNORE_ETIENNE_STATS
void Rappel::reset_cycle_stats() {

  memset(&_curr_cycle_stats, 0, sizeof(_curr_cycle_stats));
}
#endif // IGNORE_ETIENNE_STATS

void Rappel::schedule_garbage_collection(bool initial) {

  PeriodicGarbageCollectEventData* evt_data = new PeriodicGarbageCollectEventData;

  Event event;
  event.node_id = _node->get_node_id();
  event.layer = LYR_APPLICATION;
  event.flag = EVT_RAPPEL_PERIODIC_GARBAGE_COLLECT;
  event.data = evt_data;
  if (initial) {
    event.whence = /* _timer->get_time() + */
      _rng->rand_ulonglong(globops.rappel_garbage_collect_interval);
  } else {
    event.whence = /* _timer->get_time() + */ globops.rappel_garbage_collect_interval;
  }

  _garbage_collection_event_id = _timer->schedule_event(event);
}

// -------- construct Friend

void Rappel::schedule_audit(bool initial) {

  //Clock curr_time = _timer->get_time();

  // Schedule the periodic_audit timer event
  PeriodicAuditFriendEventData* evt_data = new PeriodicAuditFriendEventData;

  Event event;
  event.node_id = _node->get_node_id();
  event.layer = LYR_APPLICATION;
  event.flag = EVT_RAPPEL_PERIODIC_AUDIT;
  event.data = evt_data;

  if (initial) {
    event.whence = 
      _rng->rand_ulonglong(globops.rappel_audit_friend_interval_rapid);
  } else if (_num_audits < _rapid_audit_until) {
    event.whence = globops.rappel_audit_friend_interval_rapid;
  } else {
    event.whence = globops.rappel_audit_friend_interval;
  }

  _audit_event_id = _timer->schedule_event(event);
}

/** 
 * used to perform periodic garbage collection (and check the correctness of
 * node state)
 */
int Rappel::timer_periodic_garbage_collect(Event& event) {

  assert(_node->get_node_id() == event.node_id);

  // next garbage collection 
  schedule_garbage_collection(false);

  for (map<feed_id_t, Feed*>::iterator it = _feed_set.begin();
    it != _feed_set.end(); it++) {

    feed_id_t target_feed_id = it->first;
    Feed* feed = it->second;

    // get out, if own feed
    if (feed->get_publisher() == _node->get_node_id()) {
      continue;
    }

    bool is_pending = find(_pending_feed_joins.begin(), _pending_feed_joins.end(),
      target_feed_id) != _pending_feed_joins.end();

    // either a join is pending, it is in progress, or a rejoin has been scheduled.
    assert(is_pending 
     || feed->seeking_parent()
     || feed->is_rejoin_scheduled());
  }

  // free memory
  delete(event.data);
  return 0; 
}

void Rappel::reschedule_audit_now() {

  assert(_audit_event_id != NO_SUCH_EVENT_ID);

  _timer->cancel_event(_audit_event_id);
  schedule_audit(true); // yeah, randomize here
}

/**
 * Replace a peer in Friend with one from Friend-candidates, if a better node is
 * available.
 */
int Rappel::timer_periodic_audit(Event& event) {

  assert(_node->get_node_id() == event.node_id);

  schedule_audit(false);

  //printf("[Time=%s][PERIOIDIC Friend IMPROVEMENT] node=%u\n",
  //  _timer->get_time_str().c_str(), _node->get_node_id());

  PeriodicAuditFriendEventData* data = 
    dynamic_cast<PeriodicAuditFriendEventData*>(event.data);
  assert(data != NULL);

  // We do not maintain a friend set if the node does not subscribe to any
  // feeds. However, publishers maintain a friend set -- so that subscribers
  // to the publisher can receive friend candidates.  

  // if there are no subscribed feeds, a node should have no friends
  if (_feed_set.size() == 0) {
    _num_audits++;
#ifdef DEBUG
    printf("FEED SET SIZE IS 0. Hence, no need for friends.\n");
#endif // DEBUG
  } else {
    audit();
  }

  // unallocate memory
  delete(data);
  return 0;
}

int Rappel::receive_bloom_req(Message& recv_msg) {

  //printf("ZZZ=%u Recvd bloom filter request from =%u\n",
  //  _node->get_node_id(), recv_msg.src);

  // get the request
  BloomReqData* req_data = dynamic_cast<BloomReqData*>(recv_msg.data);
  assert(req_data != NULL);

  // add src node as s1 candidate
  add_candidate(recv_msg.src, req_data->sender_coord);

  // add a reply
  BloomReplyData* reply_data = new BloomReplyData;
  reply_data->sender_coord = _node->get_nc();
  reply_data->wait_for_timer = req_data->wait_for_timer;
  reply_data->bloom_filter = _filter;
  reply_data->audit_friend = req_data->audit_friend;

  // remove uneeded data from original message
  delete(req_data);

  // send a reply
  Message msg;
  msg.src = recv_msg.dst;
  msg.dst = recv_msg.src;
  msg.layer = LYR_APPLICATION;
  msg.flag = MSG_RAPPEL_BLOOM_REPLY;
  msg.data = reply_data;

#ifndef IGNORE_ETIENNE_STATS
  _curr_cycle_stats.msgs_out_bloom_reply++;
#endif // IGNORE_ETIENNE_STATS
  
  safe_send(msg);
  return 0;
}

int Rappel::receive_bloom_reply(Message& recv_msg) {

  //printf("ZZZ=%u Recvd bloom filter reply from =%u\n", _node->get_node_id(), recv_msg.src);

  // get the reply
  BloomReplyData* reply_data = dynamic_cast<BloomReplyData*>(recv_msg.data);
  assert(reply_data != NULL);

  // delete the wait for timer 
  if (_timer->cancel_event(reply_data->wait_for_timer) == false) {

    // cancel failed -- the fault handler for this event has already been executed
    delete(reply_data);
    return 0;
  }

/* commenting out sep 14, 2007
  // remove pending request
  _outstanding_bloom_requests.erase(recv_msg.src);
*/

  if (reply_data->audit_friend) {

    assert(_currently_auditing == recv_msg.src);

    // add the s1 candidate back to list, if it was evicted while the request was made
    if (_candidates.find(recv_msg.src) == _candidates.end()) {
      add_candidate(recv_msg.src, reply_data->sender_coord);
    }
  }

  update_bloom_filter(recv_msg.src, reply_data->bloom_filter,
    reply_data->sender_coord, reply_data->audit_friend);
  if (reply_data->audit_friend) {
    assert(know_bloom_filter(recv_msg.src));
  }

  if (reply_data->audit_friend) {
    audit_with(recv_msg.src);
  } else {
    // this nbr, is now a candidate for friendship (if not already a friend)
    invalidate_candidate(recv_msg.src);
  }

  // remove uneeded data from the message
  delete(reply_data);
  return 0;
}

int Rappel::timer_wait_after_bloom_req(Event& event) {

  // this event is no longer pending
  assert(_pending_events.find(event.id) != _pending_events.end());
  _pending_events.erase(event.id);

  WaitAfterBloomReqData* data = dynamic_cast<WaitAfterBloomReqData*>(event.data);
  assert(data != NULL);

/* commenting out sep 14, 2007
  // remove pending request
  _outstanding_bloom_requests.erase(data->neighbor);
*/

  // mark peer as failed
  peer_has_failed(data->neighbor);

  // end of s1 improvement (did not recieve a BLOOM REPLY)
  if (data->audit_friend) {
    _currently_auditing = NO_SUCH_NODE;
/*
    cout << "2 time=" << _timer->get_time_str() << "; node=" << _node->get_node_id().str()
      << "; currently_auditing=" << _currently_auditing.str() << endl;
*/
    assert(_protected_candidates.size() == 0);

    // a bloom filter request is only sent to a s1-candidate (when in process of
    // s1). Hence, add more candidates to s1 if not enough s1 peers
    if (_friends.size() < globops.rappel_num_friends) {
      audit();
    }
  }

  // free memory
  delete(data);
  return 0;
}

int Rappel::receive_add_friend_req(Message& recv_msg) {

//   printf("ZZZ=%u Recvd s1 add req from =%u\n", _node->get_node_id(), recv_msg.src);
//   fflush(stdout);

  // get the request
  AddFriendReqData* req_data = dynamic_cast<AddFriendReqData*>(recv_msg.data);
  assert(req_data != NULL);

  // add src node as s1 candidate
  add_candidate(recv_msg.src, req_data->sender_coord);
 
#ifdef DEBUG
  printf("-------GOT %u CANDIDATES--------\n", req_data->candidates.size());
#endif // DEBUG

  // add src node's provided s1 candidate list to our s1 candidate list
  for (unsigned int i = 0; i < req_data->candidates.size(); i++) {

    Coordinate dummy_coordinate;
    add_candidate(req_data->candidates[i], dummy_coordinate);
  }

#ifdef DEBUG
  printf("rec add friend req: node %s added as cand.\n", recv_msg.src.str().c_str());
#endif // DEBUG

  // add a reply
  AddFriendReplyData* reply_data = new AddFriendReplyData;
  reply_data->sender_coord = _node->get_nc();
  reply_data->wait_for_timer = req_data->wait_for_timer;
  reply_data->node_to_be_pruned = req_data->node_to_be_pruned;
  reply_data->next_targets = req_data->next_targets;

  //printf("BUZZZZZZZZZZZZZ (incoming): node to prune: %s\n", 
  //  reply_data->node_to_be_pruned.str().c_str());

  reply_data->candidates = get_candidate_list();

  if (_filter.get_version() > req_data->bloom_filter_version) {

    reply_data->ok_to_add = false;
    reply_data->filter_has_changed = true;

  } else {
  
    reply_data->filter_has_changed = false;

    for (map<node_id_t, Clock>::iterator it = _fans.begin();
      _fans.size() > 0 && it != _fans.end(); /* it++ */ )  {

      // it becomes invalid after erase. hence, it++ becomes/ invalid.
      // hence, calculating it++ before it becomes invalid.
      map<node_id_t, Clock>::iterator it_now = it;
      it++;

      if (it_now->second + globops.rappel_keep_alive_interval
        < _timer->get_time()) {

#ifdef DEBUG
        printf("fan too old; node=%s -- purging ... ", 
          it_now->first.str().c_str());
#endif // DEBUG

        _fans.erase(it_now);
      }
    }

    // allow if we can have more friends, or it is already a friend
    map<node_id_t, Clock>::iterator it_fan = _fans.find(recv_msg.src);
    if (_fans.size() < globops.rappel_num_fans || it_fan != _fans.end()) {

      // BUG: the following line creates an error, as we need to insert
      // a new fan if _fans.size() < globops.rappel_num_fans
      //it_fan->second = _timer->get_time();

      // Solution: use operator[]
      _fans[recv_msg.src] = _timer->get_time();
      reply_data->ok_to_add = true;


    } else { // fans set full
/*

     // NOTE: if this is re-enabled, remove assert() from msg_callback

      if (_rng->rand_uint(1, globops.rappel_num_friends)
        == globops.rappel_num_friends) {

        // pick a current random fan to drop
        unsigned int unlucky_fan_num = _rng->rand_uint(0, _fans.size() - 1);
        map<node_id_t, Clock>::iterator it = _fans.begin();
        for (unsigned int counter = 0; counter < unlucky_fan_num; counter++, it++);
        node_id_t unlucky_fan = it->first;
        _fans.erase(it);

        RemoveFanData* msg_data = new RemoveFanData;
        msg_data->candidates = get_candidate_list();

        Message rm_msg;
        rm_msg.src = _node->get_node_id();
        rm_msg.dst = unlucky_fan;
        rm_msg.layer = LYR_APPLICATION;
        rm_msg.flag = MSG_RAPPEL_REMOVE_FAN;
  unsigned int num_fans;
        rm_msg.data = msg_data;
        safe_send(rm_msg);

#ifndef IGNORE_ETIENNE_STATS
        _curr_cycle_stats.msgs_out_fan_deletion++;
#endif // IGNORE_ETIENNE_STATS

#ifdef DEBUG
        printf("Too many fans: however DROPPING %s (fan #%u) for INCOMING %s\n",
          unlucky_fan.str().c_str(), unlucky_fan_num, recv_msg.src.str().c_str());
#endif // DEBUG
        // add the recv_msg.src as a fan
        _fans[recv_msg.src] = _timer->get_time();
        reply_data->ok_to_add = true;

     } else {
*/

#ifdef DEBUG
        printf("Too many fans: %u. DENYING\n", _fans.size());
#endif // DEBUG

        reply_data->ok_to_add = false;
/*
      }
*/
    }
  }
  assert(_fans.size() <= globops.rappel_num_fans);

  // remove uneeded data from original message
  delete(req_data);

  // send a reply
  Message msg;
  msg.src = recv_msg.dst;
  msg.dst = recv_msg.src;
  msg.layer = LYR_APPLICATION;
  msg.flag = MSG_RAPPEL_ADD_FRIEND_REPLY;
  msg.data = reply_data;

  safe_send(msg);

#ifndef IGNORE_ETIENNE_STATS
  // stats for the last debug period
  if (reply_data->ok_to_add) {
    _curr_cycle_stats.msgs_out_friend_reply_accepted++;
  } else {
    _curr_cycle_stats.msgs_out_friend_reply_rejected++;
  }
#endif // IGNORE_ETIENNE_STATS

#ifdef DEBUG
  printf("ZZZ=%s Processed add friend req from =%s\n", _node->get_node_id().str().c_str(),
    recv_msg.src.str().c_str());
#endif // DEBUG

  return 0;
}

int Rappel::receive_remove_friend(Message& recv_msg) {
  // get the request
  RemoveFriendData* req_data = dynamic_cast<RemoveFriendData*>(recv_msg.data);
  assert(req_data != NULL);

#ifdef DEBUG
  printf("-------GOT %u CANDIDATES--------\n", req_data->candidates.size());
#endif // DEBUG

  // add src node's provided candidate list to our candidate list
  for (unsigned int i = 0; i < req_data->candidates.size(); i++) {

    Coordinate dummy_coordinate;
    add_candidate(req_data->candidates[i], dummy_coordinate);
  }

  map<node_id_t, Clock>::iterator it_fan = _fans.find(recv_msg.src);
  if (it_fan != _fans.end()) {
    _fans.erase(it_fan); // only one
  }

  // free memory
  delete(req_data);
  return 0;
}

int Rappel::receive_add_friend_reply(Message& recv_msg) {

  //printf("ZZZ=%u Recvd add friend reply from =%u\n", _node->get_node_id(), recv_msg.src);

  // get the reply
  AddFriendReplyData* reply_data = dynamic_cast<AddFriendReplyData*>(recv_msg.data);
  assert(reply_data != NULL);

  // delete the wait for timer 
  if (_timer->cancel_event(reply_data->wait_for_timer) == false) {

    // cancel failed -- the fault handler for this event has already been executed
    delete(reply_data);
    return 0;
  }

#ifdef DEBUG
  printf("-------GOT %u CANDIDATES--------\n", reply_data->candidates.size());
#endif // DEBUG

  // add src node's provided candidate list to our candidate list
  for (unsigned int i = 0; i < reply_data->candidates.size(); i++) {

    Coordinate dummy_coordinate;
    add_candidate(reply_data->candidates[i], dummy_coordinate);
  }

  if (reply_data->ok_to_add) {

#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_in_friend_reply_accepted++; 
    _curr_cycle_stats.friend_set_changes++; // TODO: check if is this ok
#endif // IGNORE_ETIENNE_STATS

    Role friend_role(ROLE_FRIEND);

    // remove node friend list -- node should be in friend list
    if (reply_data->node_to_be_pruned != NO_SUCH_NODE) {
      // node to be pruned may actually be pruned by the periodic node check procedure
      // so, remove only if still playing that role
      if (peer_plays_role(reply_data->node_to_be_pruned, friend_role)) {
        remove_peer_from_role(reply_data->node_to_be_pruned, friend_role,
          DO_NOTHING);
      }
    }

    // add responding node to friend list
    assert(know_bloom_filter(recv_msg.src)); 
    add_peer_with_role(recv_msg.src, reply_data->sender_coord, friend_role,
      *(get_bloom_filter(recv_msg.src)));

    // add_friend will assert otherwise
    map<node_id_t, CandidatePeer*>::iterator
      it_cand = _protected_candidates.find(recv_msg.src);
    assert(it_cand != _protected_candidates.end());
    delete(it_cand->second);
    _protected_candidates.erase(it_cand);
    assert(_protected_candidates.find(recv_msg.src) == _protected_candidates.end());

    // if there are pending friend adds (for a network rejoining node), request them
    if (reply_data->next_targets.size() > 0) {

      assert(_friends.size() < globops.rappel_num_friends);

      vector<node_id_t> next_targets;
      for (unsigned int i = 1; i < reply_data->next_targets.size(); i++) {
        next_targets.push_back(reply_data->next_targets[i]);
      }
      assert(next_targets.size() == reply_data->next_targets.size() - 1);

      _currently_auditing = reply_data->next_targets[0];
/*
      cout << "3 time=" << _timer->get_time_str() << "; node=" << _node->get_node_id().str()
        << "; currently_auditing=" << _currently_auditing.str() << endl;
*/
      send_friend_req(reply_data->next_targets[0], NO_SUCH_NODE, next_targets);

    } else if (_friends.size() < globops.rappel_num_friends) {

      // end of audit
      _currently_auditing = NO_SUCH_NODE;
/*
      cout << "4 time=" << _timer->get_time_str() << "; node=" << _node->get_node_id().str()
        << "; currently_auditing=" << _currently_auditing.str() << endl;
*/
      assert(_protected_candidates.size() == 0);

      // add more candidates if not enough
      audit();
    } else {

      // end of audit
      _currently_auditing = NO_SUCH_NODE;
/*
      cout << "5 time=" << _timer->get_time_str() << "; node=" << _node->get_node_id().str()
        << "; currently_auditing=" << _currently_auditing.str() << endl;
*/
    }

  } else { // not ok to add
 
#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_in_friend_reply_rejected++; 
#endif // IGNORE_ETIENNE_STATS

#ifdef DEBUG
    printf("ohhh no the reply came back as no\n");
#endif // DEBUG

    // responding node's filter has changed since we calculated it
    if (reply_data->filter_has_changed) {

#ifdef DEBUG
      printf("ohhh because the filter has changed\n");
#endif // DEBUG

      // retry audit -- after fetching new bloom filter
      assert(_currently_auditing == recv_msg.src);
      //_currently_auditing = recv_msg.src;
      send_bloom_req(recv_msg.src, true);

      // TODO: ho ho -- bug discovered on sep 17, 2007 -- pending next_targets
      // will be lost if send_bloom_req doesn't respond. very low probability?

    } else {

      // add_friend will assert otherwise
      map<node_id_t, CandidatePeer*>::iterator
        it_cand = _protected_candidates.find(recv_msg.src);
      assert(it_cand != _protected_candidates.end());
      delete(it_cand->second);
      _protected_candidates.erase(it_cand);
      assert(_protected_candidates.find(recv_msg.src)
        == _protected_candidates.end());

      // if there are pending friend adds (for a network rejoining node), request them
      if (reply_data->next_targets.size() > 0) {

        assert(_friends.size() < globops.rappel_num_friends);

        vector<node_id_t> next_targets;
        for (unsigned int i = 1; i < reply_data->next_targets.size(); i++) {
          next_targets.push_back(reply_data->next_targets[i]);
        }
        assert(next_targets.size() == reply_data->next_targets.size() - 1);

        _currently_auditing = reply_data->next_targets[0];
/*
        cout << "6 time=" << _timer->get_time_str() << "; node=" << _node->get_node_id().str()
          << "; currently_auditing=" << _currently_auditing.str() << endl;
*/
        send_friend_req(reply_data->next_targets[0], NO_SUCH_NODE, next_targets);

      } else if (_friends.size() < globops.rappel_num_friends) {

        // end of audit
        _currently_auditing = NO_SUCH_NODE;
/*
        cout << "7 time=" << _timer->get_time_str() << "; node=" << _node->get_node_id().str()
          << "; currently_auditing=" << _currently_auditing.str() << endl;
*/
        assert(_protected_candidates.size() == 0);

        // add more candidates if not enough
        audit();
      } else {

        // end of audit
        _currently_auditing = NO_SUCH_NODE;
/*
        cout << "8 time=" << _timer->get_time_str() << "; node=" << _node->get_node_id().str()
          << "; currently_auditing=" << _currently_auditing.str() << endl;
*/
      }
    }
  }

  // free memory
  delete(reply_data);
  return 0;
}

int Rappel::timer_wait_after_add_friend_req(Event& event) {

  // this event is no longer pending
  assert(_pending_events.find(event.id) != _pending_events.end());
  _pending_events.erase(event.id);

  WaitAfterAddFriendReqData* evt_data = dynamic_cast<WaitAfterAddFriendReqData*>(event.data);
  assert(evt_data != NULL);

  // mark peer as failed
  peer_has_failed(evt_data->neighbor);

  map<node_id_t, CandidatePeer*>::iterator
    it_cand = _protected_candidates.find(evt_data->neighbor);
  assert(it_cand != _protected_candidates.end());
  delete(it_cand->second);
  _protected_candidates.erase(it_cand);
  assert(_protected_candidates.find(evt_data->neighbor)
    == _protected_candidates.end());

  // a bloom filter request is only sent to a s1-candidate (when in process of
  // s1). Hence, add more candidates to s1 if not enough s1 peers
  if (evt_data->next_targets.size() > 0) {

    assert(_friends.size() < globops.rappel_num_friends);

    vector<node_id_t> next_targets;
    for (unsigned int i = 1; i < evt_data->next_targets.size(); i++) {
      next_targets.push_back(evt_data->next_targets[i]);
    }
    assert(next_targets.size() == evt_data->next_targets.size() - 1);

    _currently_auditing = evt_data->next_targets[0];
/*
    cout << "9 time=" << _timer->get_time_str() << "; node=" << _node->get_node_id().str()
      << "; currently_auditing=" << _currently_auditing.str() << endl;
*/
    send_friend_req(evt_data->next_targets[0], NO_SUCH_NODE, next_targets);

  } else if (_friends.size() < globops.rappel_num_friends) {

    // end of s1 improvement (no reply from replacement candidate)
    _currently_auditing = NO_SUCH_NODE;
/*
    cout << "10 time=" << _timer->get_time_str() << "; node=" << _node->get_node_id().str()
      << "; currently_auditing=" << _currently_auditing.str() << endl;
*/
    assert(_protected_candidates.size() == 0);

    audit();
  } else {

    // end of s1 improvement (no reply from replacement candidate)
    _currently_auditing = NO_SUCH_NODE;
/*
    cout << "11 time=" << _timer->get_time_str() << "; node=" << _node->get_node_id().str()
      << "; currently_auditing=" << _currently_auditing.str() << endl;
*/
  }

  // free memory
  delete(evt_data);
  return 0;
}

int Rappel::receive_remove_fan(Message& recv_msg) {
  // get the request
  RemoveFanData* req_data = dynamic_cast<RemoveFanData*>(recv_msg.data);
  assert(req_data != NULL);

#ifdef DEBUG
  printf("-------GOT %u CANDIDATES--------\n", req_data->candidates.size());
#endif // DEBUG

  // add src node's provided candidate list to our candidate list
  for (unsigned int i = 0; i < req_data->candidates.size(); i++) {

    Coordinate dummy_coordinate;
    add_candidate(req_data->candidates[i], dummy_coordinate);
  }

  vector<node_id_t>::iterator it_friend = find(_friends.begin(), _friends.end(),
    recv_msg.src);

  if (it_friend != _friends.end()) {
    Role friend_role(ROLE_FRIEND);
    // we are going to manually do the fault handler below -- otherwise,
    remove_peer_from_role(recv_msg.src, friend_role, DONT_SEND_REMOVE_MSG); 
    invalidate_all_candidates();

    // make sure recv_msg.src is not tried by setting it as already audited
    map<node_id_t, CandidatePeer*>::iterator it_cand = _candidates.find(recv_msg.src);
    if (it_cand != _candidates.end()) {
      it_cand->second->set_audited();
    }
    
    audit();
  }

  // free memory
  delete(req_data);
  return 0;
}

// -------- proactive maintenance

/**
 * Start pinging around. Verify if a ping from children was received
 * during last period (~ passive pinging)
 *
 * @param event The scheduled event (message) contains ...
 */
int Rappel::timer_periodic_node_check(Event& event) {

  assert(_node->is_rappel_online());

  PeriodicNeighborCheckEventData* evt_data = dynamic_cast<PeriodicNeighborCheckEventData*>(event.data);
  assert(evt_data != NULL);

  // should be a peer
  assert(is_a_peer(evt_data->neighbor));
  Neighbor* nbr = get_peer(evt_data->neighbor);
  
  // schedule the next check (using the same event variable)
  event.whence = /*_timer->get_time() +*/ globops.rappel_heart_beat_interval;
  event_id_t event_id = _timer->schedule_event(event);
  nbr->set_node_check_event_id(event_id);

  // by default, skip sending a ping request
  //bool skip = true;

  // get all roles for this nbr
  vector<Role> roles = nbr->get_roles();

  // BUG (July 1, 2007): Do not pass evt_data->neighbor as a reference
  // parameter to remove_peer_from_role as evt_data maybe freed
  // via Neighbor::remove_role() -> Timer::cancel_event()

  // Solution: copy evt_data->neighbor
  node_id_t peer_id = evt_data->neighbor;

  for (unsigned int i = 0; i < roles.size(); i++) {

    // remove only if the neighbor has responded since the node came online
    if (nbr->get_last_heard_in_role(roles[i]) >= _node->rappel_online_since()) {

      // check if a ping request/reply was heard from the node in the previous
      // KEEP_ALIVE period
      if (nbr->get_last_heard_in_role(roles[i]) + globops.rappel_keep_alive_interval
        < _timer->get_time()) {

#ifdef DEBUG
        printf("YYY=%s: ~~~~~~~REMOVING~~~~~~ peer=%s from role=%s\n",
        _node->get_node_id().str().c_str(),
        evt_data->neighbor.str().c_str(), roles[i].get_role_type_str());
#endif // DEBUG

        // remove peer from role -- it hasn't heard in this role
        remove_peer_from_role(peer_id, roles[i], INVOKE_FAULT_HANDLER);
        continue;
      }
    }

/*
    // if we did not hear a ping request from the nbr in the previous HEARTBEAT
    // interval, send a ping request now. 
    // NOTE: we can not base this check in last_heard_reply because it
    // likely indicates a response to the ping request sent in the last interval
    if (nbr->get_last_heard_request_in_role(roles[i])
      + globops.rappel_heart_beat_interval < _timer->get_time()) {

      // send ping request (we didn't hear from the peer recently)
      skip = false;
    }
*/
  }
/*
  if (skip) {
    // we have heard a request from the nbr in the previous HEARTBEAT interval
    // for all roles. hoorah. we can exit, because we don't need to perform any
    // other operation but send a ping request with the code that follows
    return 0;
  }
*/

  // send ping requests -- if still a neighbor
  if (is_a_peer(peer_id)) {
    send_ping_requests(peer_id);
  }

  // don't delete evt_data -- resued
  return 0;
}

void Rappel::send_ping_requests(const node_id_t& nbr_id) {

  // should be a peer
  assert(is_a_peer(nbr_id));
  Neighbor* nbr = get_peer(nbr_id);
  
  // get roles..
  vector<Role> roles = nbr->get_roles();
    
  // aggregate all ping requests (used for sending the ping request)
  vector<PingReqInfo*> requests;

  // aggregate all roles (used by fault handler)
  vector<Role> request_roles;
  
  for (unsigned int i = 0; i < roles.size(); i++) {

    // update the last ping request sent
    nbr->update_sent_request_in_role(roles[i]);

    switch(roles[i].get_role_type()) {

    case ROLE_FRIEND: {

      PingReqInfoFromFriend* request_info = new PingReqInfoFromFriend;
      request_info->role = roles[i];

      requests.push_back(request_info);
      request_roles.push_back(roles[i]);
      break;
    }

    case ROLE_FEED_PARENT: {

      // make sure we are still subscribing to the feed
      map<feed_id_t, Feed*>::iterator it = _feed_set.find(roles[i].get_feed_id());
      assert(it != _feed_set.end());
      Feed* feed = it->second;

      PingReqInfoFromParent* request_info = new PingReqInfoFromParent;
      request_info->role = roles[i];
      request_info->last_update_seq = feed->get_latest_update_seq();

      requests.push_back(request_info);
      request_roles.push_back(roles[i]);
      break;
    }
       
    case ROLE_FEED_CHILD: {
       
      // make sure we are still subscribing to the feed
      map<feed_id_t, Feed*>::iterator it = _feed_set.find(roles[i].get_feed_id());
      assert(it != _feed_set.end());
      Feed* feed = it->second;

      PingReqInfoFromChild* request_info = new PingReqInfoFromChild;
      request_info->role = roles[i];
      request_info->last_update_seq = feed->get_latest_update_seq();

      requests.push_back(request_info);
      request_roles.push_back(roles[i]);
      break;
    }
       
    default: {

      // shouldn't happen
      assert(false);
    }
       
    } // end switch
  }

  // if there are any requests
  if (requests.size() > 0) {
    // First, schedule the WAIT_AFTER_PING_REQ timer event
    WaitAfterPingReqData* handler_data = new WaitAfterPingReqData;
    handler_data->neighbor = nbr_id;
    handler_data->roles = request_roles;
       
    Event evt;
    evt.node_id = _node->get_node_id();
    evt.layer = LYR_APPLICATION;
    evt.flag = EVT_RAPPEL_WAIT_AFTER_PING_REQ;
    evt.data = handler_data;
    evt.whence = globops.rappel_reply_timeout;

    event_id_t event_id = _timer->schedule_event(evt);
    assert(_pending_events.find(event_id) == _pending_events.end());
    _pending_events[event_id] = true;

    // Next, send the PING REQ message
    PingReqData* req_data = new PingReqData;
    req_data->sender_coord = _node->get_nc();
    req_data->wait_for_timer = event_id;

    req_data->bloom_filter_version = _filter.get_version();
    req_data->candidates = get_candidate_list();
    req_data->requests = requests;

    // prepare PING REQ message
    Message msg;
    msg.layer = LYR_APPLICATION;
    msg.flag = MSG_RAPPEL_PING_REQ;
    msg.src = _node->get_node_id();
    msg.dst = nbr_id;
    msg.data = req_data;
 
    // send
    safe_send(msg);

#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_out_ping_req++;
#endif // IGNORE_ETIENNE_STATS
  }
}

/**
 * Receive a ping request : reply with a ack and pigy back information
 * depending on the ping type
 *
 * @param msg The arriving ping message
 */
int Rappel::receive_ping_req(Message& recv_msg) {

  // get the ping type
  PingReqData* req_data = dynamic_cast<PingReqData*>(recv_msg.data);
  assert(req_data != NULL);

  //printf("DBG: recv ping req from %s\n", /*type %s\n"*, */
  //  recv_msg.src.str().c_str()/*, ping_type_str(req_data->ping_type)*/);

  // add src node as s1 candidate
  add_candidate(recv_msg.src, req_data->sender_coord);

#ifdef DEBUG
  printf("-------GOT %u CANDIDATES--------\n", req_data->candidates.size());
#endif // DEBUG

  // add src node's provided s1 candidate list to our s1 candidate list
  for (unsigned int i = 0; i < req_data->candidates.size(); i++) {

    Coordinate dummy_coordinate;
    add_candidate(req_data->candidates[i], dummy_coordinate);
    //add_candidate(req_data->candidates[i].first,
      //req_data->candidates[i].second);
  }

  // node is a nbr (or at worst, on our s1 candidate list)
  if (is_a_peer(recv_msg.src)) {

    // fetch newer version of bloom filter, if available
    if (req_data->bloom_filter_version > bloom_filter_version(recv_msg.src)) {
      send_bloom_req(recv_msg.src, false);
    }

    Neighbor* nbr = get_peer(recv_msg.src);
    if (nbr->set_nc(req_data->sender_coord)) {
      invalidate_candidate(recv_msg.src);
    }
  } else {

    // TODO: if not a peer (anymore) but on s1 candidate list, invalidate s1 and bloom_filter
  }

/*
  // this is to prevent the race condition where this node is getting a ping
  // request from a nbr, to whom it has just sent a request: race condition
  // is broken via node_id_t
  if (recv_msg.src < recv_msg.dst && is_a_peer(recv_msg.src)) {

    Neighbor* nbr = get_peer(recv_msg.src);
    if (nbr->get_last_sent_request() + globops.rappel_reply_timeout > _timer->get_time()) {

      // we just sent this node a ping request, don't send a reply
      // free memory
      delete(req_data);
      return 0;
    }
  }
*/

  // check-point the start time
  //Clock start_time = _timer->get_time();

  // aggregate all replies
  vector<PingReplyInfo*> replies;

  // to simplify syntax
  vector<PingReqInfo*> requests = req_data->requests;

  for (unsigned int i = 0; i < requests.size(); i++) {
    Role role = requests[i]->role;

    switch(role.get_role_type()) {

    case ROLE_FRIEND: {

      // get ping request info (specific for this role)
      PingReqInfoFromFriend* request_info =
        dynamic_cast<PingReqInfoFromFriend*>(requests[i]);
      assert(request_info != NULL);

      if (peer_plays_role(recv_msg.src, role)) {

        // update last heard from
        get_peer(recv_msg.src)->update_heard_request_in_role(role);
      }

      // check if the src node is part of our _fans
      map<node_id_t, Clock>::iterator it = _fans.find(recv_msg.src);
      if (it != _fans.end()) {

        // update our _fans entry
        it->second = _timer->get_time();

        // create reply
        PingReplyInfoFromFriend* reply_info = new PingReplyInfoFromFriend;
        reply_info->role = role;

        replies.push_back(reply_info);
      }

      // delete(request_info); // no needed -- deleted in ~PingReqData
      break;
    }
    
    case ROLE_FEED_PARENT: {

      // If the requesting node is not a child, DO NOT send a ping reply.
      // WHY? This node could have been sent the child JOIN_CHANGE_PARENT
      // directive recently. However, the child could have requested the PING REQ
      // prior to receiving that message.

      // get ping request info (specific for this role)
      PingReqInfoFromParent* request_info =
        dynamic_cast<PingReqInfoFromParent*>(requests[i]);
      assert(request_info != NULL);

      // SPECIAL CASE: parent and child are reciprocal relationships
      Role role_child(ROLE_FEED_CHILD, role.get_feed_id());

      if (peer_plays_role(recv_msg.src, role_child)) {
 
        // update last heard from
        get_peer(recv_msg.src)->update_heard_request_in_role(role_child);

        // TODO: this will break if we implement the remove_feed feature
        map<feed_id_t, Feed*>::iterator it = _feed_set.find(role.get_feed_id());
        assert(it != _feed_set.end());
        Feed* feed = it->second;

        // we don't have the latest update..
        unsigned int entries_pulled = feed->request_missing_entries(
          request_info->last_update_seq, recv_msg.src);

#ifndef IGNORE_ETIENNE_STATS
        _curr_cycle_stats.msgs_out_dissemination_pull_req += entries_pulled;
#endif // IGNORE_ETIENNE_STATS

        // create reply
        PingReplyInfoFromParent* reply_info = new PingReplyInfoFromParent;
        reply_info->role = role;
        reply_info->last_update_seq = feed->get_latest_update_seq();
        reply_info->publisher_nc = feed->get_publisher_nc();
        //reply_info->ancestors = feed->get_ancestors();

        replies.push_back(reply_info);

      } else {
  
        // Requesting node is not a child -- do not send a message

      }

      // delete(request_info); // no needed -- deleted in ~PingReqData
      break;
    }

    case ROLE_FEED_CHILD: {

      // get ping request info (specific for this role)
      PingReqInfoFromChild* request_info =
        dynamic_cast<PingReqInfoFromChild*>(requests[i]);
      assert(request_info != NULL);

      // If the requesting node is not a parent, DO NOT send a ping reply.

      // SPECIAL CASE: parent and child are reciprocal relationships
      Role role_parent(ROLE_FEED_PARENT, role.get_feed_id());

      if (peer_plays_role(recv_msg.src, role_parent)) {
 
        // update last heard from
        get_peer(recv_msg.src)->update_heard_request_in_role(role_parent);

        // TODO: this will break if we implement the remove_feed feature
        map<feed_id_t, Feed*>::iterator it = _feed_set.find(role.get_feed_id());
        assert(it != _feed_set.end());
        Feed* feed = it->second;

        // we don't have the latest update..
        unsigned int entries_pulled = feed->request_missing_entries(
          request_info->last_update_seq, recv_msg.src);

#ifndef IGNORE_ETIENNE_STATS
        _curr_cycle_stats.msgs_out_dissemination_pull_req += entries_pulled;
#endif // IGNORE_ETIENNE_STATS

        // create reply
        PingReplyInfoFromChild* reply_info = new PingReplyInfoFromChild;
        reply_info->role = role;
        reply_info->last_update_seq = feed->get_latest_update_seq();
        reply_info->publisher_nc = feed->get_publisher_nc();

        replies.push_back(reply_info);

      } else {
  
        // Requesting node is not a child -- do not send a message

      }

      // delete(request_info); // no needed -- deleted in ~PingReqData
      break;
    }

    default: {
      assert(false);
      break;
    }

    } // end switch
  }

  if (replies.size() > 0) {

    // Next, send the PING REQ message
    PingReplyData* reply_data = new PingReplyData;
    reply_data->sender_coord = _node->get_nc();
    reply_data->wait_for_timer = req_data->wait_for_timer;

    reply_data->bloom_filter_version = _filter.get_version();
    reply_data->candidates = get_candidate_list();
    reply_data->replies = replies;

    // send a reply
    Message msg;
    msg.src = recv_msg.dst;
    msg.dst = recv_msg.src;
    msg.layer = LYR_APPLICATION;
    msg.flag = MSG_RAPPEL_PING_REPLY;
    msg.data = reply_data;
 
    // send
    safe_send(msg);

    // keep stats
#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_out_ping_reply++;
#endif // IGNORE_ETIENNE_STATS
  }

  // free memory
  delete(req_data);
  return 0;
}

/**
 * Destroy the timer associated to that ping and register
 * information if needed (depending on the type of the ping)
 *
 * @param event The scheduled event (message) contains ...
 */
int Rappel::receive_ping_reply(Message& recv_msg) {

  PingReplyData* reply_data = dynamic_cast<PingReplyData*>(recv_msg.data) ;
  assert(reply_data != NULL);

  //printf("DBG: recv ping reply from %s\n", /*, type %s\n",*/
  //  recv_msg.src.str().c_str()/*, ping_type_str(reply_data->ping_type)*/);

  // delete the wait for timer 
  if (_timer->cancel_event(reply_data->wait_for_timer) == false) {

    // cancel failed -- the fault handler for this event has already been executed
    delete(reply_data);
    return 0;
  }

  // REMBEMER: this node may no longer be a peer (if network packets arrive OUT-OF-ORDER)
  //assert(is_a_peer());

  // add src node as s1 candidate
  add_candidate(recv_msg.src, reply_data->sender_coord);

#ifdef DEBUG
  printf("-------GOT %u CANDIDATES--------\n", reply_data->candidates.size());
#endif // DEBUG

  // add src node's provided s1 candidate list to our s1 candidate list
  for (unsigned int i = 0; i < reply_data->candidates.size(); i++) {

    Coordinate dummy_coordinate;
    add_candidate(reply_data->candidates[i], dummy_coordinate);
    //add_candidate(reply_data->candidates[i].first,
      //reply_data->candidates[i].second);
  }

  // node is a nbr (or at worst, on our s1 candidate list)
  if (is_a_peer(recv_msg.src)) {

    // fetch newer version of bloom filter, if available
    if (reply_data->bloom_filter_version > bloom_filter_version(recv_msg.src)) {
      send_bloom_req(recv_msg.src, false);
    }

    Neighbor* nbr = get_peer(recv_msg.src);
    if (nbr->set_nc(reply_data->sender_coord)) {
      invalidate_candidate(recv_msg.src);
    }
  }

  // to simplify syntax
  vector<PingReplyInfo*> replies = reply_data->replies;

  for (unsigned int i = 0; i < replies.size(); i++) {

    Role role = replies[i]->role;

    switch(role.get_role_type()) {

    case ROLE_FRIEND: {

      // get ping reply info (specific for this role)
      PingReplyInfoFromFriend* reply_info =
        dynamic_cast<PingReplyInfoFromFriend*>(replies[i]);
      assert(reply_info != NULL);

      if (peer_plays_role(recv_msg.src, role)) {
 
        // update last heard from
        get_peer(recv_msg.src)->update_heard_reply_in_role(role);

       // nothing else to do

      }

      // delete(reply_info); // no needed -- deleted in ~PingReplyData
      break;
    }

    case ROLE_FEED_PARENT: {

      // get ping reply info (specific for this role)
      PingReplyInfoFromParent* reply_info =
        dynamic_cast<PingReplyInfoFromParent*>(replies[i]);
      assert(reply_info != NULL);

      if (peer_plays_role(recv_msg.src, role)) {
 
        // update last heard from
        get_peer(recv_msg.src)->update_heard_reply_in_role(role);

       // nothing else to do

      } else { // peer not found

        // TODO: an "unknown" peer is responding with a PING reply. This peer
        // was probably removed as a child some time between the PING request
        // and reply.
      }

      map<feed_id_t, Feed*>::iterator it = _feed_set.find(role.get_feed_id());
      assert(it != _feed_set.end());
      Feed* feed = it->second;

      // update publisher nc (if needed)
      feed->set_publisher_nc(reply_info->publisher_nc);

      // if we don't have the latest update..
      unsigned int entries_pulled = feed->request_missing_entries(
        reply_info->last_update_seq, recv_msg.src);

#ifndef IGNORE_ETIENNE_STATS
      _curr_cycle_stats.msgs_out_dissemination_pull_req += entries_pulled;
#endif // IGNORE_ETIENNE_STATS

      // delete(reply_info); // no needed -- deleted in ~PingReplyData
      break;
    }

    case ROLE_FEED_CHILD: {

      // get ping reply info (specific for this role)
      PingReplyInfoFromChild* reply_info =
        dynamic_cast<PingReplyInfoFromChild*>(replies[i]);
      assert(reply_info != NULL);

      if (peer_plays_role(recv_msg.src, role)) {
 
        // update last heard from
        get_peer(recv_msg.src)->update_heard_reply_in_role(role);

       // nothing else to do

      } else { // peer not found

        // TODO: an "unknown" peer is responding with a PING reply. This peer
        // was probably removed as a child some time between the PING request
        // and reply.
      }

      map<feed_id_t, Feed*>::iterator it = _feed_set.find(role.get_feed_id());
      assert(it != _feed_set.end());
      Feed* feed = it->second;

      // update publisher nc (if needed)
      feed->set_publisher_nc(reply_info->publisher_nc);

      // if we don't have the latest update..
      unsigned int entries_pulled = feed->request_missing_entries(
        reply_info->last_update_seq, recv_msg.src);

#ifndef IGNORE_ETIENNE_STATS
      _curr_cycle_stats.msgs_out_dissemination_pull_req += entries_pulled;
#endif // IGNORE_ETIENNE_STATS

      // delete(reply_info); // no needed -- deleted in ~PingReplyData
      break;
    }

    default: {
      assert(false);
    }

    }
  } // end for (loop all roles)

  // free memory
  delete(reply_data);
  return 0;
}

/**
 * The timer for a ping reply has expired : the peer may have failed
 *
 * @param event The scheduled event (message) contains ...
 */
int Rappel::timer_wait_after_ping_req(Event& event) {

  // this event is no longer pending
  assert(_pending_events.find(event.id) != _pending_events.end());
  _pending_events.erase(event.id);

  WaitAfterPingReqData* evt_data = dynamic_cast<WaitAfterPingReqData*>(event.data);
  assert(evt_data != NULL);

  // to simplify syntax
  vector<Role> roles = evt_data->roles;

  for (unsigned int i = 0; i < roles.size(); i++) {

    // mark peer as failed
    if (peer_plays_role(evt_data->neighbor, roles[i])) {
      peer_has_failed(evt_data->neighbor);
      break;
    }

  } // end for (loop for all roles)

  // free memory
  delete(evt_data);
  return 0;
}

// ---------------------- JOIN

void Rappel::schedule_next_feed_join(bool initial) {

  assert(_next_feed_join_event_id == NO_SUCH_EVENT_ID);

  NextFeedJoinEventData* evt_data = new NextFeedJoinEventData;
  assert(evt_data != NULL);

  Event event;
  event.node_id = _node->get_node_id();
  event.layer = LYR_APPLICATION;
  event.flag = EVT_RAPPEL_NEXT_FEED_JOIN;
/*
  if (initial) {
    event.whence = _rng->rand_ulonglong(globops.rappel_feed_join_seperation_min);
  } else {
*/
    event.whence = globops.rappel_feed_join_seperation_min;
/*
  }
*/
  event.data = evt_data;

  _next_feed_join_event_id = _timer->schedule_event(event);
}

int Rappel::timer_next_feed_join(Event& event) {

  assert(_node->is_rappel_online());

  NextFeedJoinEventData* evt_data = dynamic_cast<NextFeedJoinEventData*>(event.data);
  assert(evt_data != NULL);

  _next_feed_join_event_id = NO_SUCH_EVENT_ID;

  // proceed only if there are any pending joins
  if (_pending_feed_joins.size() > 0) {

    unsigned num_joins = (_num_feed_joins < globops.rappel_gradual_feed_joins) ?
      1 : _pending_feed_joins.size();

    for (unsigned int i = 0; i < num_joins; i++) {

      vector<feed_id_t>::iterator it = _pending_feed_joins.begin();
      assert(it != _pending_feed_joins.end());
      feed_id_t feed_id = *it;
      _pending_feed_joins.erase(it); // only one, per iteration

      // TODO: this should be true as long as remove_feed is not implemented
      map<feed_id_t, Feed*>::iterator it_feed = _feed_set.find(feed_id);
      assert(it_feed != _feed_set.end());
      Feed* feed = it_feed->second;

      // replace the parent for that feed (if node is rejoining network)
      node_id_t old_parent = NO_SUCH_NODE;
      if (feed->has_parent()) {

        assert(!_virgin); // node can only have a parent, if it is rejoining the network
        node_id_t old_parent = feed->get_parent();

        Role parent_role(ROLE_FEED_PARENT, feed->get_feed_id());
        remove_peer_from_role(old_parent, parent_role, DO_NOTHING);
      }

      _last_feed_join_req = _timer->get_time();
      _num_feed_joins++;

      map<node_id_t, unsigned int> attempted;
      vector<node_id_t> failed;
      initiate_feed_join_req(feed_id, attempted, failed, old_parent, INITIAL_JOIN);
    }

    // schedule the next join
    schedule_next_feed_join(false);
  }

  // free memory
  delete(evt_data);
  return 0;
}

int Rappel::timer_periodic_feed_rejoin(Event& event) {

  assert(_node->is_rappel_online());

  PeriodicRejoinEventData* data = dynamic_cast<PeriodicRejoinEventData*>(event.data);
  
  // TODO: this should be true as long as remove_feed is not implemented
  map<feed_id_t, Feed*>::iterator it = _feed_set.find(data->feed_id);
  assert(it != _feed_set.end());
  Feed* feed = it->second;

#ifdef ETIENNE_DEBUG
  printf("node %s: periodic rejoin ; rescheduling rejoin for feed %s\n", 
    _node->get_node_id().str().c_str(), data->feed_id.str().c_str());
  fflush(stdin);
#endif // ETIENNE_DEBUG

  // schedule the rejoin
  feed->reschedule_rejoin(false);

  // initiate a "optimized" rejoin if not already at level 1
  if (feed->has_parent() == false) {

    // if not seeking parent -- means we lost our parent (likely because the
    //publisher was the parent, and the publisher went offline)
    if (!feed->seeking_parent()) {

      map<node_id_t, unsigned int> attempted;
      vector<node_id_t> failed;
      // TODO: should the fwd_node be the publisher????
      initiate_feed_join_req(data->feed_id, attempted, failed, NO_SUCH_NODE,
        REJOIN_DUE_TO_LOST_PARENT);
    }

  } else if (feed->get_parent() != feed->get_publisher()) {

    double stretch_ratio = 100.0;
    if (feed->has_parent()) {
        double direct_cost = _node->get_nc().get_distance(feed->get_publisher_nc());
        double path_cost = feed->get_path_cost();
        stretch_ratio = path_cost / direct_cost;
    }
    // TODO: variablze the 3.00 stretch ratio factor
    bool exponential_distribution = stretch_ratio > 3.00 ? true : false;
    node_id_t ancestor = feed->get_random_ancestor(exponential_distribution);
#ifdef DEBUG
    printf("@time=%s, my=%s exponential=%s random ancestor=%s\n",
      _timer->get_time_str().c_str(), _node->get_node_id().str().c_str(),
      (exponential_distribution ? "yes" : "no"), ancestor.str().c_str());
#endif // DEBUG

    map<node_id_t, unsigned int> attempted;
    vector<node_id_t> failed;
    initiate_feed_join_req(data->feed_id, attempted, failed, ancestor, PERIODIC_REJOIN);
  }

  if (!feed->has_parent()) {
    assert(feed->seeking_parent());
  }

  // unallocate memory
  delete data;
  return 0;
}

/**
 * Receive a join message from a node :
 *  ask parent its children set size on that universe (or ask it if it is full)
 *  reply with ok if addition ok and parent is full
 *  reply with fwd if parent not full
 *
 * @param event The scheduled event (message) contains a JoinData object
 */
int Rappel::receive_feed_join_req(Message& recv_msg) {
  
  JoinReqData* req_data = dynamic_cast<JoinReqData*>(recv_msg.data);
  assert(req_data != NULL);

  // add src node as s1 candidate
  add_candidate(recv_msg.src, req_data->sender_coord);

#ifdef DEBUG
  printf("-------GOT %u CANDIDATES--------\n", req_data->candidates.size());
#endif // DEBUG

  // add src node's provided s1 candidate list to our s1 candidate list
  for (unsigned int i = 0; i < req_data->candidates.size(); i++) {

    Coordinate dummy_coordinate;
    add_candidate(req_data->candidates[i], dummy_coordinate);
  }

#ifndef IGNORE_ETIENNE_STATS
  switch (req_data->request_type) {
  case INITIAL_JOIN:
    _curr_cycle_stats.msgs_in_join_req++;
    break;
  case REJOIN_DUE_TO_LOST_PARENT:
    _curr_cycle_stats.msgs_in_lost_parent_rejoin_req++;
    break;
  case REJOIN_DUE_TO_CHANGE_PARENT:
    _curr_cycle_stats.msgs_in_change_parent_rejoin_req++;
    break;
  case PERIODIC_REJOIN:
    _curr_cycle_stats.msgs_in_periodic_rejoin_req++;
    break;
  case REDIRECTED_PERIODIC_REJOIN:
    _curr_cycle_stats.msgs_in_periodic_rejoin_req++; // necessary
    _curr_cycle_stats.msgs_in_redirected_periodic_rejoin_req++;
    break;
  default:
    assert(false); // no other request type
  } 
#endif // IGNORE_ETIENNE_STATS

  // send a reply
  Message msg;
  msg.src = recv_msg.dst;
  msg.dst = recv_msg.src;
  msg.layer = LYR_APPLICATION;

#ifdef DEBUG
  printf("JOIN request: %s %s %s\n", _node->get_node_id().str().c_str(), req_data->feed_id.str().c_str(),
    recv_msg.src.str().c_str());
#endif // DEBUG

  bool deny_childhood = false;
  Feed* feed = NULL; // uninitialized

  // The node received a JOIN request may not be subscribed to the feed: (i) as
  // there are false positives when computing get_feed_subscribers(); or, (ii)
  // this node could have unsubscribed to the feed. Part (ii) is not yet
  // implemented.
  map<feed_id_t, Feed*>::iterator it = _feed_set.find(req_data->feed_id);
  if (it == _feed_set.end()) {

    // TODO: this is true as long as remove_feed is unimplemented
    //assert(req_data->request_type == false);
    deny_childhood = true;

#ifdef DEBUG
    printf("DENYING BECAUSE I'M NOT SUBSCRIBED TO THIS FEED\n");
#endif // DEBUG

    // TODO: add false request counter

  } else {

    feed = it->second;

/*
    feed->another_join_request();
*/

    // deny, if parent not set and no information on publisher nc
    // if we have publisher nc, we can still accept peer as child
    // NOTE: we might not have ancestor info -- this can cause an assert
    if(!feed->has_parent() /* && !feed->is_publisher_nc_set() */) {
      deny_childhood = true;

#ifdef DEBUG
      printf("DENYING BECAUSE I'VE NO PARENT TO THIS FEED\n");
#endif // DEBUG
      // NOTE: the following is not true anymore, due to delayed joins
      // assert(feed->seeking_parent());
    }

    // deny, if our own parent is requesting to be our child
    if (feed->has_parent() && feed->get_parent() == recv_msg.src) {

#ifdef DEBUG
      printf("DENYING BECAUSE I'VE GOT THE REQUEST FEED AS MY PARENT\n");
#endif // DEBUG
      deny_childhood = true;
    }
  }

  if (deny_childhood) {

    JoinDenyData* reply_data = new JoinDenyData;
    reply_data->sender_coord = _node->get_nc();
    reply_data->wait_for_timer = req_data->wait_for_timer;

    reply_data->candidates = get_candidate_list();
    reply_data->feed_id = req_data->feed_id;
    reply_data->request_type = req_data->request_type;
    reply_data->publisher_nc_attached = false;
    //reply_data->bloom_filter = _filter;
    reply_data->attempted = req_data->attempted;
    reply_data->failed = req_data->failed;

    msg.flag = MSG_RAPPEL_FEED_JOIN_REPLY_DENY;
    msg.data = reply_data;

    safe_send(msg);

#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_out_join_reply_deny++;
#endif // IGNORE_ETIENNE_STATS

    delete(req_data);
    return 0;
  }

  Coordinate sender_coord = req_data->sender_coord;

#ifdef DEBUG
  printf(" JOIN  my coordinate (app) => %s\n", _node->get_nc().to_string().c_str());
  printf("       my distance to pub => %0.4f\n",
    _node->get_nc().get_distance(feed->get_publisher_nc()));
  printf(" JOIN  sender coordinate (app) => %s\n", sender_coord.to_string().c_str());
  printf("       sender distance to pub => %0.4f\n",
    sender_coord.get_distance(feed->get_publisher_nc()));
#endif // DEBUG

  // requesting node is closer to publisher than this node
  // FWD join request to parent
  if (sender_coord.get_distance(feed->get_publisher_nc()) < 
    _node->get_nc().get_distance(feed->get_publisher_nc())) {

#ifdef DEBUG
    printf("Requester is closer to publisher than I, redirecting to parent: %s\n",
      feed->get_parent().str().c_str());
#endif // DEBUG

    // a publiser can't forward to it's parent (i.e., itself)
    assert(feed->get_publisher() != _node->get_node_id());

    JoinFwdData* reply_data = new JoinFwdData;
    reply_data->sender_coord = _node->get_nc();
    reply_data->wait_for_timer = req_data->wait_for_timer;

    reply_data->candidates = get_candidate_list();
    reply_data->feed_id = req_data->feed_id;

    RappelJoinType request_type = req_data->request_type;
    if (req_data->request_type == PERIODIC_REJOIN) {
      request_type = REDIRECTED_PERIODIC_REJOIN;
    }
    reply_data->request_type = request_type;
    reply_data->publisher_nc = feed->get_publisher_nc();
    //reply_data->bloom_filter = _filter;
    reply_data->attempted = req_data->attempted;
    reply_data->failed = req_data->failed;

    if (feed->has_parent()) {
      reply_data->fwd_node = feed->get_parent();
    } else {
      reply_data->fwd_node = feed->get_publisher(); //random_ancestor();
    }

    msg.flag = MSG_RAPPEL_FEED_JOIN_REPLY_FWD;
    msg.data = reply_data;

    safe_send(msg);

#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_out_join_reply_fwd++;
#endif // IGNORE_ETIENNE_STATS

    delete(req_data);
    return 0;
  }

  // the requesting node is farther away from publisher than this node
#ifdef DEBUG
  printf("I am closer to publisher than requestor\n");
#endif // DEBUG

  Role child_role(ROLE_FEED_CHILD, req_data->feed_id);

  // this node's child set has capacity -- send OK to join
  if (feed->get_num_children() < globops.rappel_num_feed_children ||
    peer_plays_role(recv_msg.src, child_role)) {

#ifdef DEBUG
    printf("I can have more children, accepting requestor as a child\n");
#endif // DEBUG

    JoinOkData* reply_data = new JoinOkData;
    reply_data->sender_coord = _node->get_nc();
    reply_data->wait_for_timer = req_data->wait_for_timer;

    reply_data->candidates = get_candidate_list();
    reply_data->feed_id = req_data->feed_id;
    reply_data->request_type = req_data->request_type;
    if (feed->get_parent() == _node->get_node_id()) { // publisher
      reply_data->path_cost = 0.0;
    } else {
      reply_data->path_cost = feed->get_path_cost();
    }
    reply_data->ancestors = feed->get_ancestors();
    reply_data->publisher_nc = feed->get_publisher_nc();
    reply_data->last_update_seq = feed->get_latest_update_seq();

    if (!peer_plays_role(recv_msg.src, child_role)) {

      if (know_bloom_filter(recv_msg.src)) {
        add_peer_with_role(recv_msg.src, req_data->sender_coord, child_role,
          *(get_bloom_filter(recv_msg.src)));
      } else {
        BloomFilter dummy_filter;
        add_peer_with_role(recv_msg.src, req_data->sender_coord, child_role,
          dummy_filter);
        send_bloom_req(recv_msg.src, false);
      }
    }

    msg.flag = MSG_RAPPEL_FEED_JOIN_REPLY_OK;
    msg.data = reply_data;

    safe_send(msg);

#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_out_join_reply_ok++;
#endif // IGNORE_ETIENNE_STATS

    delete(req_data);
    return 0;
  }

  // this node's children set is full
#ifdef DEBUG
  printf("However, I already have enough children\n");
#endif // DEBUG

  // make sure it isn't oversubscribed
  assert(feed->get_num_children() == globops.rappel_num_feed_children);

  // determine if the requesting node is closer to the publisher than any child 
  bool some_child_is_farther = false;
  vector<node_id_t> children = feed->get_children();

/*
  // if there is any piggy back information on request, use it
  for (unsigned int i = 0; i < children.size(); i++) {
    if (req_data->attempted.find(children[i]) != req_data->attempted.end()) {

#ifdef DEBUG
      printf("Did child=%s coord change?\n", children[i].str().c_str());
      printf("     my information: "); get_foreign_nc(children[i]).print();
      printf("     piggybacked information: "); req_data->attempted[children[i]].second.print();
#endif // DEBUG

      // update child coordinate before progressing any further
      Neighbor* nbr = get_peer(children[i]);
      if (nbr->set_nc(req_data->attempted[children[i]].second)) {
        invalidate_candidate(children[i]);
      }
    }
  }
*/

  for (unsigned int i = 0; i < children.size(); i++) {

    // make sure all children are correctly represented in the _peers list
    // (defined above) Role child_role(ROLE_FEED_CHILD, req_data->feed_id);
    //assert(peer_plays_role(children[i], child_role));

    Neighbor* child = get_peer(children[i]);

    if (find(req_data->failed.begin(), req_data->failed.end(),
      children[i]) != req_data->failed.end()) {

#ifdef DEBUG
      printf("Is child=%s dead?\n", children[i].str().c_str());
#endif // DEBUG

      // this child has failed?
      // TODO: check if this child is alive
      continue;
    }

    if (child->get_nc().get_distance(feed->get_publisher_nc()) >=
      sender_coord.get_distance(feed->get_publisher_nc())) {

      some_child_is_farther = true;
      // TODO: uncommment the below, it is a correct optimization. commented
      // out to produce debug
      break;
    }
  }

  // if all children are closer to the publisher then the requesting node --
  // make the requesting node the grandchild of the child that is closest to
  // the reqeusting node
  if (!some_child_is_farther) {

#ifdef DEBUG
    printf("All children are closer to publiser than requestor\n");
#endif // DEBUG

    // find the child closest to the requesting node 
    double min_distance = 9999999.99; // whatever
    node_id_t closest_child(NO_SUCH_NODE);

    for (unsigned int i = 0; i < children.size(); i++) {

      Neighbor* child = get_peer(children[i]);

      if (find(req_data->failed.begin(), req_data->failed.end(),
        children[i]) != req_data->failed.end()) {

        // this child has failed?
        // TODO: check if this child is alive
        continue;
      }
      double distance = child->get_nc().get_distance(sender_coord);

      if (closest_child == NO_SUCH_NODE || distance < min_distance) {
        min_distance = distance;
        closest_child = children[i];
      }
    }

    assert(closest_child != NO_SUCH_NODE);

#ifdef DEBUG
    printf("Forwarding to closest child (to the requestor): %s\n",
      closest_child.str().c_str());
#endif // DEBUG

    JoinFwdData* reply_data = new JoinFwdData;
    reply_data->sender_coord = _node->get_nc();
    reply_data->wait_for_timer = req_data->wait_for_timer;

    reply_data->candidates = get_candidate_list();
    reply_data->feed_id = req_data->feed_id;

    RappelJoinType request_type = req_data->request_type;
    if (req_data->request_type == PERIODIC_REJOIN) {
      request_type = REDIRECTED_PERIODIC_REJOIN;
    }
    reply_data->request_type = request_type;
    reply_data->publisher_nc = feed->get_publisher_nc();
    reply_data->fwd_node = closest_child;
    reply_data->attempted = req_data->attempted;
    reply_data->failed = req_data->failed;

    msg.flag = MSG_RAPPEL_FEED_JOIN_REPLY_FWD;
    msg.data = reply_data;

    safe_send(msg);

#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_out_join_reply_fwd++;
#endif // IGNORE_ETIENNE_STATS

    delete(req_data);
    return 0;
  }

  // If the requesting node is closer to the publisher than at least one child,
  // make the requesting node a child, and one of the current children (i.e.,
  // the child farthest away from the publisher) a grandchild.
  
#ifdef DEBUG
  printf("At least one child is farther from publiser than requestor\n");
#endif // DEBUG

  // find the child farthest to the publisher 
  double max_distance = -999999.99; // whatever
  node_id_t farthest_child(NO_SUCH_NODE);

  for (unsigned int i = 0; i < children.size(); i++) {

    Neighbor* child = get_peer(children[i]);

    if (find(req_data->failed.begin(), req_data->failed.end(),
      children[i]) != req_data->failed.end()) {

      // this child has failed?
      // TODO: check if this child is alive
      continue;
    }

    double distance = child->get_nc().get_distance(feed->get_publisher_nc());

    if (farthest_child == NO_SUCH_NODE || distance > max_distance) {
      max_distance = distance;
      farthest_child = children[i];
    }
  }

  assert(farthest_child != NO_SUCH_NODE);
  Neighbor* fc = get_peer(farthest_child);

#ifndef USE_DYNAMIC_NCS // only assert with non-dynamic NCs
  assert(fc->get_nc().get_distance(feed->get_publisher_nc()) >=
    req_data->sender_coord.get_distance(feed->get_publisher_nc()));
#endif // USE_DYNAMIC_NCS

  Coordinate fc_coord = fc->get_nc();

  // Ask one of the present children to become a grand child (via the
  // requesting node)
  // (defined above) Role child_role(ROLE_FEED_CHILD, req_data->feed_id);
  remove_peer_from_role(farthest_child, child_role, DO_NOTHING);

  // Make requesting node is not already our child
  if (peer_plays_role(recv_msg.src, child_role) == false) {
    if (know_bloom_filter(recv_msg.src)) {
      add_peer_with_role(recv_msg.src, req_data->sender_coord, child_role,
        *(get_bloom_filter(recv_msg.src)));
    } else {
      BloomFilter dummy_filter;
      add_peer_with_role(recv_msg.src, req_data->sender_coord, child_role,
        dummy_filter);
      send_bloom_req(recv_msg.src, false);
    }
/*
    add_peer_with_role(recv_msg.src, req_data->sender_coord, child_role,
      req_data->bloom_filter);
*/
  }

  JoinOkData* reply_data = new JoinOkData;
  reply_data->sender_coord = _node->get_nc();
  reply_data->wait_for_timer = req_data->wait_for_timer;

  reply_data->candidates = get_candidate_list();
  reply_data->feed_id = req_data->feed_id;
  reply_data->request_type = req_data->request_type;
  if (feed->get_parent() == _node->get_node_id()) { // publisher
    reply_data->path_cost = 0.0;
  } else {
    reply_data->path_cost = feed->get_path_cost();
  }
  reply_data->ancestors = feed->get_ancestors();
  reply_data->publisher_nc = feed->get_publisher_nc();
  reply_data->last_update_seq = feed->get_latest_update_seq();

  msg.flag = MSG_RAPPEL_FEED_JOIN_REPLY_OK;
  msg.data = reply_data;

  safe_send(msg);

#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_out_join_reply_ok++;
#endif // IGNORE_ETIENNE_STATS

  // find the child closest to the removed child
  double min_distance = 9999999.99; // whatever
  node_id_t new_parent(NO_SUCH_NODE);

  // refresh children set (one node removed -- and one added)
  children = feed->get_children();
  for (unsigned int i = 0; i < children.size(); i++) {

    Neighbor* child = get_peer(children[i]);

    if (find(req_data->failed.begin(), req_data->failed.end(),
      children[i]) != req_data->failed.end()) {

      // this child has failed?
      // TODO: check if this child is alive
      continue;
    }

    double distance = child->get_nc().get_distance(fc_coord);

    if (new_parent == NO_SUCH_NODE || distance < min_distance) {
      min_distance = distance;
      new_parent = children[i];
    }
  }

  assert(new_parent != NO_SUCH_NODE);

#ifndef USE_DYNAMIC_NCS // only assert with non-dynamic NCs
  Neighbor* np = get_peer(new_parent);

  assert(np->get_nc().get_distance(feed->get_publisher_nc()) <=
    fc_coord.get_distance(feed->get_publisher_nc()));
#endif // USE_DYNAMIC_NCS

#ifdef DEBUG
  printf("Forcing farthest child %s to become a grand child (child of %s)\n",
    farthest_child.str().c_str(), new_parent.str().c_str());
#endif // DEBUG

  JoinChangeParentData* msg_data  = new JoinChangeParentData;
  msg_data->sender_coord = _node->get_nc();

  msg_data->feed_id = req_data->feed_id;
  msg_data->publisher_nc = feed->get_publisher_nc();
  msg_data->new_parent = new_parent; 

  Message msg2;
  msg2.src = _node->get_node_id();
  msg2.dst = farthest_child;
  msg2.layer = LYR_APPLICATION;
  msg2.flag = MSG_RAPPEL_FEED_CHANGE_PARENT;
  msg2.data = msg_data;

  // delay change parent directive -- wait until the new child gets the
  // message first
  _transport->send_with_delay(msg2, globops.rappel_reply_timeout / 2);

#ifndef IGNORE_ETIENNE_STATS
  _curr_cycle_stats.msgs_out_change_parent++;
#endif // IGNORE_ETIENNE_STATS

  assert(feed->has_parent() || feed->seeking_parent() || feed->is_rejoin_scheduled());

  delete(req_data);
  return 0;
}

int Rappel::receive_feed_join_reply_deny(Message& recv_msg) {

  JoinDenyData* reply_data = dynamic_cast<JoinDenyData*>(recv_msg.data);
  assert(reply_data != NULL);

  // delete the wait for timer 
  if (_timer->cancel_event(reply_data->wait_for_timer) == false) {

    // cancel failed -- the fault handler for this event has already been executed
    delete(reply_data);
    return 0;
  }

  // TODO: add src node to s1 if s1.size() < Friend_SIZE (?)

  // add src node as s1 candidate
  add_candidate(recv_msg.src, reply_data->sender_coord);

#ifdef DEBUG
  printf("-------GOT %u CANDIDATES--------\n", reply_data->candidates.size());
#endif // DEBUG

  // add src node's provided s1 candidate list to our s1 candidate list
  for (unsigned int i = 0; i < reply_data->candidates.size(); i++) {

    Coordinate dummy_coordinate;
    add_candidate(reply_data->candidates[i], dummy_coordinate);
  }

  // TODO: this should be true -- as long as remove_feed() is not implemented
  map<feed_id_t, Feed*>::iterator it = _feed_set.find(reply_data->feed_id);
  assert(it != _feed_set.end());
  Feed* feed = it->second;

  if (reply_data->publisher_nc_attached) {
    feed->set_publisher_nc(reply_data->publisher_nc);
    assert(feed->is_publisher_nc_set());
  }

  // no longer seeking parent (just for a moment)
  feed->unset_seeking_parent();

  // try another node if last node denies
  if (feed->has_parent() == false) {

    // add to failed list
    vector<node_id_t> failed = reply_data->failed;

    if (find(failed.begin(), failed.end(), recv_msg.src) == failed.end()) {
      failed.push_back(recv_msg.src);
    }

    initiate_feed_join_req(reply_data->feed_id, reply_data->attempted,
      failed, NO_SUCH_NODE, reply_data->request_type);
  }

  assert(feed->has_parent() || feed->seeking_parent() || feed->is_rejoin_scheduled());

  delete(reply_data);
  return 0;
}

/**
 * Receive a join ok message : proceed with the join
 *
 * @param event The scheduled event (message) contains a JoinOkData object
 */
int Rappel::receive_feed_join_reply_ok(Message& recv_msg) {

  JoinOkData* reply_data = dynamic_cast<JoinOkData*>(recv_msg.data);
  assert(reply_data != NULL);

  // delete the wait for timer 
  if (_timer->cancel_event(reply_data->wait_for_timer) == false) {

    // cancel failed -- the fault handler for this event has already been executed
    delete(reply_data);
    return 0;
  }
  
  // TODO: add src node to s1 if s1.size() < Friend_SIZE (?)

  // add src node as s1 candidate
  add_candidate(recv_msg.src, reply_data->sender_coord);
  
#ifdef DEBUG
  printf("-------GOT %u CANDIDATES--------\n", reply_data->candidates.size());
#endif // DEBUG

  // add src node's provided s1 candidate list to our s1 candidate list
  for (unsigned int i = 0; i < reply_data->candidates.size(); i++) {

    Coordinate dummy_coordinate;
    add_candidate(reply_data->candidates[i], dummy_coordinate);
  }

  // TODO: this should be true -- as long as remove_feed() is not implemented
  map<feed_id_t, Feed*>::iterator it = _feed_set.find(reply_data->feed_id);
  assert(it != _feed_set.end());
  Feed* feed = it->second;
  
  // add publisher nc
  feed->set_publisher_nc(reply_data->publisher_nc);

  // catch up to missing entries  
  // we don't have the latest update..
  unsigned int entries_pulled = feed->request_missing_entries(
    reply_data->last_update_seq, recv_msg.src);

#ifndef IGNORE_ETIENNE_STATS
  _curr_cycle_stats.msgs_out_dissemination_pull_req += entries_pulled;
#endif // IGNORE_ETIENNE_STATS

  // no longer seeking parent 
  feed->unset_seeking_parent();

  Role parent_role(ROLE_FEED_PARENT, reply_data->feed_id);
  assert(peer_plays_role(recv_msg.src, parent_role) == false);

  if (feed->has_parent()) {

    assert(feed->get_parent() != recv_msg.src);

    NotAChildData* msg_data = new NotAChildData;
    msg_data->sender_coord = _node->get_nc();

    msg_data->feed_id = feed->get_feed_id();
    msg_data->publisher_nc = feed->get_publisher_nc();

    Message msg;
    msg.src = _node->get_node_id();
    msg.dst = feed->get_parent();
    msg.layer = LYR_APPLICATION; 
    msg.flag = MSG_RAPPEL_FEED_NO_LONGER_YOUR_CHILD;
    msg.data = msg_data;

    safe_send(msg);

#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_out_no_longer_your_child++;
#endif // IGNORE_ETIENNE_STATS

    remove_peer_from_role(feed->get_parent(), parent_role, DO_NOTHING);
  } 

#ifndef USE_DYNAMIC_NCS // only assert with non-dynamic NCs
  assert(reply_data->sender_coord.get_distance(feed->get_publisher_nc())
    <= _node->get_nc().get_distance(feed->get_publisher_nc()));
#endif // USE_DYNAMIC_NCS

  // add recv_msg.src as feed parent
  if (know_bloom_filter(recv_msg.src)) {
    add_peer_with_role(recv_msg.src, reply_data->sender_coord, parent_role,
      *(get_bloom_filter(recv_msg.src)));
  } else {
    BloomFilter dummy_filter;
    add_peer_with_role(recv_msg.src, reply_data->sender_coord, parent_role,
      dummy_filter);
    send_bloom_req(recv_msg.src, false);
  }

  // set parent properties
  feed->set_path_cost(reply_data->path_cost +
    _node->get_nc().get_distance(reply_data->sender_coord));
  vector<node_id_t> ancestors = reply_data->ancestors;
  ancestors.push_back(recv_msg.src);
  feed->set_ancestors(ancestors);

  // flush the ancestors down to the children
  flush_ancestry(feed->get_feed_id());

  // reschedule the rejoin
#ifdef ETIENNE_DEBUG
    printf("node %s: received join ok for feed %s\n",
      _node->get_node_id().str().c_str(), reply_data->feed_id.str().c_str());
#endif // ETIENNE_DEBUG

  assert(feed->has_parent() || feed->seeking_parent() || feed->is_rejoin_scheduled());

  delete(reply_data);
  return 0;
}

/**
 * Receive a join fwd : send join to next node (iterative process)
 *
 * @param event The scheduled event (message) contains JoinFwdData object
 */
int Rappel::receive_feed_join_reply_fwd(Message& recv_msg) {

  JoinFwdData* reply_data = dynamic_cast<JoinFwdData*>(recv_msg.data);
  assert(reply_data != NULL);

  // delete the wait for timer 
  if (_timer->cancel_event(reply_data->wait_for_timer) == false) {

    // cancel failed -- the fault handler for this event has already been executed
    delete(reply_data);
    return 0;
  }

  // TODO: add src node to s1 if s1.size() < Friend_SIZE (?)

  // add src node as s1 candidate
  add_candidate(recv_msg.src, reply_data->sender_coord);

#ifdef DEBUG
  printf("-------GOT %u CANDIDATES--------\n", reply_data->candidates.size());
#endif // DEBUG

  // add src node's provided s1 candidate list to our s1 candidate list
  for (unsigned int i = 0; i < reply_data->candidates.size(); i++) {

    Coordinate dummy_coordinate;
    add_candidate(reply_data->candidates[i], dummy_coordinate);
  }

  // TODO: this should be true -- as long as remove_feed() is not implemented
  map<feed_id_t, Feed*>::iterator it = _feed_set.find(reply_data->feed_id);
  assert(it != _feed_set.end());
  Feed* feed = it->second;

  // add publisher nc
  feed->set_publisher_nc(reply_data->publisher_nc);
  assert(feed->is_publisher_nc_set());

  // no longer seeking parent (just for a moment)
  feed->unset_seeking_parent();

#ifdef DEBUG
  printf("The fwd node is node=%s\n", reply_data->fwd_node.str().c_str());
#endif // DEBUG

  // if feed already has a parent, don't request a JOIN to the parent
  if (feed->has_parent()) {
    if (reply_data->fwd_node == feed->get_parent()) {
    //if (feed->is_an_ancestor(reply_data->fwd_node)) {
      delete(reply_data);
      return 0; 
    }
  }

  initiate_feed_join_req(reply_data->feed_id, reply_data->attempted, reply_data->failed,
    reply_data->fwd_node, reply_data->request_type);

  assert(feed->has_parent() || feed->seeking_parent() || feed->is_rejoin_scheduled());

  delete(reply_data);
  return 0;
}

int Rappel::receive_feed_change_parent(Message& recv_msg) {

  JoinChangeParentData* msg_data = dynamic_cast<JoinChangeParentData*>(recv_msg.data);
  assert(msg_data != NULL);

  // add src node as s1 candidate
  add_candidate(recv_msg.src, msg_data->sender_coord);

  // TODO: this should be true -- as long as remove_feed() is not
  map<feed_id_t, Feed*>::iterator it = _feed_set.find(msg_data->feed_id);
  assert(it != _feed_set.end());
  Feed* feed = it->second;

  // add publisher nc
  feed->set_publisher_nc(msg_data->publisher_nc);

  // listen to this only if the parent sends the message...
  Role role(ROLE_FEED_PARENT, msg_data->feed_id);
  if (peer_plays_role(recv_msg.src, role)) {

    // just to make sure
    assert(recv_msg.src != msg_data->new_parent);

    // remove parent from role
    remove_peer_from_role(recv_msg.src, role, DO_NOTHING);

    map<node_id_t, unsigned int> attempted;
    vector<node_id_t> failed;
    initiate_feed_join_req(msg_data->feed_id, attempted, failed, msg_data->new_parent,
      REJOIN_DUE_TO_CHANGE_PARENT);

  } else {
    // this situation is minimized because we give a "changed parent"
    // notification to the old parent
    //assert(false);
  }

  assert(feed->has_parent() || feed->seeking_parent() || feed->is_rejoin_scheduled());

  //free memory
  delete(msg_data);
  return 0;
}

/**
 * The timer associated to the waiting for a answer for a join has expired.
 * Remove the failed peer (try a new ping ?) and do the join again.
 *
 * @param event The scheduled event (message) contains 
 */
int Rappel::timer_wait_after_feed_join_req(Event& event) {

  // this event is no longer pending
  assert(_pending_events.find(event.id) != _pending_events.end());
  _pending_events.erase(event.id);

  WaitAfterJoinReqData* evt_data = dynamic_cast<WaitAfterJoinReqData*>(event.data);
  assert(evt_data != NULL);
  
  // TODO: this should be true -- as long as remove_feed() is not
  map<feed_id_t, Feed*>::iterator it = _feed_set.find(evt_data->feed_id);
  assert(it != _feed_set.end());
  Feed* feed = it->second;

  // no longer seeking parent (just for a moment)
  feed->unset_seeking_parent();

  // Continue with replacing parent only for a non-rejoin request. If a node
  // failed during the rejoin process, it doesn't really affect us -- we're
  // still connected to the parent

  // Also, if the join failed at the publisher, there is nothing we can do.

  if (!feed->has_parent() && evt_data->neighbor != feed->get_publisher()) {

    if (find(evt_data->failed.begin(), evt_data->failed.end(),
      evt_data->neighbor) == evt_data->failed.end()) {

      evt_data->failed.push_back(evt_data->neighbor);
    }

    initiate_feed_join_req(evt_data->feed_id, evt_data->attempted, evt_data->failed,
      NO_SUCH_NODE, evt_data->request_type);

    assert(feed->seeking_parent());
  }

  // publisher has failed, check if it is alive periodically
  if (evt_data->neighbor == feed->get_publisher()) {
    feed->reschedule_rejoin(false);
  }

  // mark peer as failed
  peer_has_failed(evt_data->neighbor);

  // free memory
  delete(evt_data);
  return 0;
}

int Rappel::receive_feed_no_longer_your_child(Message& recv_msg) {

  NotAChildData* msg_data = dynamic_cast<NotAChildData*>(recv_msg.data);
  assert(msg_data != NULL);

  // add src node as s1 candidate
  add_candidate(recv_msg.src, msg_data->sender_coord);

  // TODO: this should be true -- as long as remove_feed() is not
  map<feed_id_t, Feed*>::iterator it = _feed_set.find(msg_data->feed_id);
  assert(it != _feed_set.end());

  Role role(ROLE_FEED_CHILD, msg_data->feed_id);
  if (peer_plays_role(recv_msg.src, role)) {
    // listen to this only if the child sends the message...
    // (check required because this node could have already sent this node a
    // directive to find a new parent)

    remove_peer_from_role(recv_msg.src, role, DO_NOTHING);
  }

  // add publisher nc
  it->second->set_publisher_nc(msg_data->publisher_nc);

  delete(msg_data);
  return 0;
}

int Rappel::receive_feed_flush_ancestry(Message& recv_msg) {

  FlushAncestryData* msg_data = dynamic_cast<FlushAncestryData*>(recv_msg.data);
  assert(msg_data != NULL);

  // add src node as s1 candidate
  add_candidate(recv_msg.src, msg_data->sender_coord);

  // TODO: this should be true -- as long as remove_feed() is not
  map<feed_id_t, Feed*>::iterator it = _feed_set.find(msg_data->feed_id);
  assert(it != _feed_set.end());
  Feed* feed = it->second;

  // update publisher nc (if needed)
  feed->set_publisher_nc(msg_data->publisher_nc);

  Role role(ROLE_FEED_PARENT, msg_data->feed_id);
  if (peer_plays_role(recv_msg.src, role)) {

    vector<node_id_t> ancestors = msg_data->ancestors;
    ancestors.push_back(recv_msg.src);
    feed->set_ancestors(ancestors);
    feed->set_path_cost(msg_data->path_cost +
      _node->get_nc().get_distance(msg_data->sender_coord));

    // now, flush it down to the kids
    flush_ancestry(msg_data->feed_id);
  }

  // delete memory
  delete(msg_data);
  return 0;
}

void Rappel::flush_ancestry(const feed_id_t& feed_id) {

  map<feed_id_t, Feed*>::iterator it = _feed_set.find(feed_id);
  assert(it != _feed_set.end());
  Feed* feed = it->second;

  vector<node_id_t> children = feed->get_children();
  for (unsigned int i = 0; i < children.size(); i++) {

    FlushAncestryData* msg_data = new FlushAncestryData;
    msg_data->sender_coord = _node->get_nc();

    msg_data->feed_id = feed->get_feed_id();
    msg_data->publisher_nc = feed->get_publisher_nc();

    msg_data->ancestors = feed->get_ancestors();
    if (feed->get_parent() == _node->get_node_id()) { // publisher
      msg_data->path_cost = 0.0;
    } else {
      msg_data->path_cost = feed->get_path_cost();
    }

    Message msg;
    msg.src = _node->get_node_id();
    msg.dst = children[i];
    msg.layer = LYR_APPLICATION;
    msg.flag = MSG_RAPPEL_FEED_FLUSH_ANCESTRY;
    msg.data = msg_data;

    safe_send(msg);

#ifndef IGNORE_ETIENNE_STATS
    _curr_cycle_stats.msgs_out_flush_ancestry++;
#endif // IGNORE_ETIENNE_STATS
  }
}

// -------- message dissemination

/**
 * Reception of a new entry for a feed
 * @param event The scheduled event (message) contains ...
 */
int Rappel::receive_feed_update(Message& recv_msg) {

  FeedUpdateData* recv_data = dynamic_cast<FeedUpdateData*>(recv_msg.data);
  assert(recv_data != NULL);

  Clock curr_time = _timer->get_time();

  // make sure the peer plays the parent role for that feed

  // get the feed pointer
  map<feed_id_t, Feed*>::iterator it = _feed_set.find(recv_data->feed_id);
  assert(it != _feed_set.end());
  Feed* feed = it->second;

  // print on stdout (TOBEREMOVED: costly and redundant with global stats)
#ifdef DEBUG
  printf("[Time=%s] RECVD UPDATE_%s_%u_ @ %s from %s) ( delay_to_pub %s )\n", 
    _timer->get_time_str(curr_time).c_str(), 
    recv_data->feed_id.str().c_str(), 
    recv_data->seq_num,
    _node->get_node_id().str().c_str(), 
    recv_msg.src.str().c_str(),
#if COMPILE_FOR == SIMULATOR 
    _timer->get_time_str(
      _transport->get_route_delay(_node->get_node_id(), 
      (node_id_t) recv_data->feed_id)
    ).c_str()
#elif COMPILE_FOR == NETWORK
    "undef" // we do not want to ping the publisher here
#endif
  );
#endif // DEBUG

#ifndef IGNORE_ETIENNE_STATS

  map<feed_id_t, stats_feed_t>::iterator
    it_feed_stat = feed_stats.find(feed->get_feed_id());
  assert(it_feed_stat != feed_stats.end());
  
  // stats for this update seq num should exist
  map<unsigned int, stats_feed_update_t>::iterator
    it_update_stat = it_feed_stat->second.updates.find(recv_data->seq_num);

#if COMPILE_FOR == NETWORK
  // check if this is the first time rec'ving an update
  if (it_update_stat == it_feed_stat->second.updates.end()) {
  
    // hackish below
    stats_feed_update_t curr_feed_update;
    curr_feed_update.sync_emission_time = recv_data->sync_emission_time;
    curr_feed_update.details.first_reception_delay = -1 * MILLI_SECOND;
    curr_feed_update.details.ideal_reception_delay = feed->get_route_rtt() / 2;
    curr_feed_update.details.num_receptions_pulled = 0;
    curr_feed_update.details.num_receptions_pushed = 0;

    it_feed_stat->second.updates[recv_data->seq_num] = curr_feed_update;

    // change pointer to it_update_state
    it_update_stat = it_feed_stat->second.updates.find(recv_data->seq_num);
  }
#endif // COMPILE_FOR == NETWORK
  assert(it_update_stat != it_feed_stat->second.updates.end());

#if COMPILE_FOR == NETWORK
  stats_node_per_update_t* update_stat = &(it_update_stat->second.details);
#endif

#if COMPILE_FOR == SIMULATOR
  // save stats ONLY if this is one of the designated recv nodes
  map<node_id_t, stats_node_per_update_t>::iterator
    it_update_node_stat = it_update_stat->second.recv_node_stats.find(
      _node->get_node_id());

  if (it_update_node_stat != it_update_stat->second.recv_node_stats.end()) {

    stats_node_per_update_t* update_stat = &(it_update_node_stat->second);
#endif

    // update first reception information, if applicable
    if (update_stat->first_reception_delay == -1 * MILLI_SECOND) {
  
      update_stat->first_reception_delay =
        _timer->get_sync_time(curr_time) - recv_data->sync_emission_time;
      update_stat->first_reception_is_pulled = recv_data->pulled;
  
      if (feed->has_parent()) {
        update_stat->first_reception_height = feed->get_ancestors().size();
      } else if (feed->seeking_parent() || feed->is_rejoin_scheduled()) {
        update_stat->first_reception_height = 10000;
      } else {
        assert(false);
      }
    }
  
    if (recv_data->pulled) {
      update_stat->num_receptions_pulled++;
    } else {
      update_stat->num_receptions_pushed++;
    }

    update_stat->last_reception_delay =
      _timer->get_sync_time(curr_time) - recv_data->sync_emission_time;
#if COMPILE_FOR == SIMULATOR
  }
#endif // COMPILE_FOR == SIMULATOR
#endif // !IGNORE_ETIENNE_STATS

  feed->add_entry(recv_data->seq_num, recv_data->msg,
    recv_data->sync_emission_time);
  // DO NOT PULL missing entries
  //feed->request_missing_entries(recv_data->seq_num, recv_msg.src);
  unsigned int entries_sent = feed->send_entry_to_children(recv_data->seq_num);

#ifndef IGNORE_ETIENNE_STATS
  _curr_cycle_stats.msgs_out_dissemination += entries_sent; 
#endif // IGNORE_ETIENNE_STATS

  // delete uneeded message content
  delete (recv_data);
  return 0;
}

/**
 * Reception of a demand for some missed updates from a node
 * we may not have the feed (false positive) but if we have it 
 * send back the needed updates. 
 *
 * @param message The scheduled event (message) contains ...
 */
int Rappel::receive_feed_update_pull_req(Message& recv_msg) {

  FeedUpdatePullData* data = dynamic_cast<FeedUpdatePullData*>(recv_msg.data);
  assert(data != NULL);

  // TODO Etienne: false positive causes the simulator to stop ?
  map<feed_id_t, Feed*>::iterator it = _feed_set.find(data->feed_id);
  assert(it != _feed_set.end());
  Feed* feed = it->second;

  //assert(data->seq_num <= feed->get_latest_update_seq());
  feed->send_entry(data->seq_num, recv_msg.src, true);

#ifndef IGNORE_ETIENNE_STATS
  _curr_cycle_stats.msgs_out_dissemination++;
#endif // IGNORE_ETIENNE_STATS

  // free memory
  delete(data);
  return 0;
}

/**
 * The tiemr associated to the demand of a pull request expires.
 * Declare the node as failed and ask another node
 *
 * @param event
 */
int Rappel::timer_wait_after_feed_update_pull_req(Event& event) {

  WaitAfterUpdatePullData* data = dynamic_cast<WaitAfterUpdatePullData*>(event.data);
  assert(data != NULL);

  map<feed_id_t, Feed*>::iterator it = _feed_set.find(data->feed_id);
  assert(it != _feed_set.end());
  Feed* feed = it->second;

  // if the entry has not already been fetched, implies that the node from
  // which the pull was requested has failed
  if (!feed->obtained_update(data->seq_num)) {
    feed->mark_pull_as_failed(data->seq_num, data->neighbor);
  
    // mark peer as failed
    peer_has_failed(data->neighbor);
  }

  // free memory
  delete(data);
  return 0;
}

// -------------- INTERNAL FUNCTIONS

/**
 * Check if a node is a peer (in any role capacity)
 */
bool Rappel::is_a_peer(const node_id_t& peer_id) const {
  return (_peers.find(peer_id) != _peers.end());
}

/**
 * Returns the peer (Neighbor) entry for a peer_id
 */
Neighbor* Rappel::get_peer(const node_id_t& peer_id) {
  assert(is_a_peer(peer_id));
  return _peers[peer_id];
}

/**
 * Does a given peer play a role
 */
bool Rappel::peer_plays_role(const node_id_t& peer_id, const Role& role) {

  if (is_a_peer(peer_id)) {
    return _peers[peer_id]->plays_role(role);
  }

  return false;
}

/**
 * Add role for a peer 
 */
void Rappel::add_peer_with_role(const node_id_t& peer_id, const Coordinate& nc,
  const Role& role, const BloomFilter& filter) {

  // Peer is already present
  if (is_a_peer(peer_id)) {

    // change the coordinate and filter (if the need be)
    Neighbor* peer = _peers[peer_id];
    bool filter_changed = peer->set_bloom_filter(filter);
    bool nc_changed = peer->set_nc(nc);

    if (filter_changed || nc_changed) {
      invalidate_candidate(peer_id);
    }

    if (peer_plays_role(peer_id, role)) {
      return;

    } else {

      // add role to peer
      peer->add_role(role);
    
      // feed* instance modifed below...(in common with a completely new peer)
    }

  } else {

    Neighbor* nbr = new Neighbor(_node->get_node_id(), peer_id, nc, role, filter);
    _peers[peer_id] = nbr;

  }

  // perform role specific tasks
  switch (role.get_role_type()) {

  case ROLE_FRIEND: {
    _friends.push_back(peer_id);

    // reset all s1_candidate's audited
    // since s1 (the set) has changed, retest all candidates
    invalidate_all_candidates();

   break;
  }

  case ROLE_FEED_PARENT: {

    map<feed_id_t, Feed*>::iterator it = _feed_set.find(role.get_feed_id());
    assert(it != _feed_set.end());
    Feed* feed = it->second;
    feed->set_parent(peer_id);
    break;
  }

  case ROLE_FEED_CHILD: {

    map<feed_id_t, Feed*>::iterator it = _feed_set.find(role.get_feed_id());
    assert(it != _feed_set.end());
    Feed* feed = it->second;
    feed->add_child(peer_id);
    break;
  }

  default: {
    assert(false);
  }

  } // end switch

  // make certain the peer is added
  assert(peer_plays_role(peer_id, role));
}

/**
 * Remove a peer from role
 */
void Rappel::remove_peer_from_role(const node_id_t& peer_id, 
 const Role& role, RappelInstruction instruction) {
  
  map<node_id_t, Neighbor*>::iterator it_peer = _peers.find(peer_id);
  assert(it_peer != _peers.end()); //assert(peer_plays_role(peer_id, role));

  // get bloom filter from peer(to set it for a s1 candidate)
  const BloomFilter* const filter = it_peer->second->get_bloom_filter();
  Coordinate nc = it_peer->second->get_nc();

  // remove from main _peers list -- if it has no more roles to play
  if (it_peer->second->remove_role(role)) {

    // erase peer now -- so update_bloom_filter below updates candidate properly
    Neighbor* nbr = it_peer->second;
    _peers.erase(it_peer);

    // if a peer is an s1 candidate, apply its filter and nc to the candidate
    if (_candidates.find(peer_id) != _candidates.end()) {
      update_bloom_filter(peer_id, *filter, nc, false);
    }

    // unallocate memory after update_bloom_filter so that *filter remains valid
    delete(nbr);
  }

  // remove from role-specific list
  switch (role.get_role_type()) {

  case ROLE_FRIEND: {

    //printf("HOLY M. JOLY. GONEER #1 &peer_id=%d, &role=%d\n", &peer_id, &role);
    //printf("removing friend %s from friend set\n", peer_id.str().c_str());

    vector<node_id_t>::iterator result = find(_friends.begin(), _friends.end(),
      peer_id);
    assert(result != _friends.end());
    _friends.erase(result);

    bool send_remove_msg = ~(instruction & DONT_SEND_REMOVE_MSG);
    if (send_remove_msg) {

      RemoveFriendData* msg_data = new RemoveFriendData;
      msg_data->candidates = get_candidate_list();

      Message msg;
      msg.src = _node->get_node_id();
      msg.dst = peer_id;
      msg.layer = LYR_APPLICATION;
      msg.flag = MSG_RAPPEL_REMOVE_FRIEND;
      msg.data = msg_data;
      safe_send(msg);

#ifndef IGNORE_ETIENNE_STATS
      _curr_cycle_stats.msgs_out_friend_deletion++;
#endif // IGNORE_ETIENNE_STATS
    }

    if (instruction & INVOKE_FAULT_HANDLER) {

      invalidate_all_candidates();
      audit();
    }

    break;
  }

  case ROLE_FEED_PARENT: {

    map<feed_id_t, Feed*>::iterator it = _feed_set.find(role.get_feed_id());
    assert(it != _feed_set.end());
    Feed* feed = it->second;

    node_id_t old_parent = feed->get_parent();
    node_id_t grand_parent = NO_SUCH_NODE;
    if (old_parent != feed->get_publisher()) {
      grand_parent = feed->get_grand_parent();
    }
    feed->unset_parent();

#ifdef DEBUG
    printf("\n ---> remove peer %s from role ROLE_FEED_PARENT\n",
      peer_id.str().c_str());
    printf("publisher of the feed %s is %s\n", role.get_feed_id().str().c_str(),
      feed->get_publisher().str().c_str());
#endif // DEBUG

    if (instruction & INVOKE_FAULT_HANDLER) {

      // if the parent is not publisher, try and find another parent (first
      // attempt: grand parent)
      if (old_parent != feed->get_publisher()) {

        // use the failed node as already used
        map<node_id_t, unsigned int> attempted;
        vector<node_id_t> failed;
        failed.push_back(peer_id);
        initiate_feed_join_req(role.get_feed_id(), attempted, failed,
          grand_parent, REJOIN_DUE_TO_LOST_PARENT);
      } else {

        // publisher failed -- ping it again in a while
        feed->reschedule_rejoin(false);
      }
    }

    break;
  }

  case ROLE_FEED_CHILD: {

    map<feed_id_t, Feed*>::iterator it = _feed_set.find(role.get_feed_id());
    assert(it != _feed_set.end());
    Feed* feed = it->second;
    feed->remove_child(peer_id);

    // No fault handler -- simply remove peer from role
    if (instruction & INVOKE_FAULT_HANDLER) {
      // nothing
    }

    break;
  }

  default: {
    assert(false);
  }

  }
 
  bool plays = peer_plays_role(peer_id, role);
  assert(plays == false);
}

void Rappel::peer_has_failed(const node_id_t& peer_id) {

  if (!is_a_peer(peer_id)) {
    return;
  }

  // get roles for peer
  Neighbor* nbr = _peers[peer_id];
  vector<Role> roles = nbr->get_roles();

  for (unsigned int i = 0; i < roles.size(); i++) {

#ifdef DEBUG
    printf("YYY=%s: removing FAILED peer=%s from role=%s\n", _node->get_node_id().str().c_str(),
      peer_id.str().c_str(), roles[i].get_role_type_str());
#endif // DEBUG

    // remove peer from role -- it hasn't heard in this role
    remove_peer_from_role(peer_id, roles[i], INVOKE_FAULT_HANDLER);
  }
 
  assert(is_a_peer(peer_id) == false);
}

/**
 * Add a node to the s1_candidate list. New peers are discovered as because
 * each neighbor piggybacks its s1 list on top onf PING replies.
 * The candidates list is restricted to a maximum of _num_candidates
 * nodes, and whenever a new candidate is discovered, the oldest node based on
 * last_vote_at time is evicted. In other words, an LRU policy is enforced.
 */
void Rappel::add_candidate(const node_id_t& node_id, const Coordinate& candidate_nc) {

  // do not add the node itself as a candidate
  if (node_id == _node->get_node_id()) {
    return;
  }

  map<node_id_t, CandidatePeer*>::iterator it_cand = _candidates.find(node_id);
  if (it_cand != _candidates.end()) {
    // candidate node is already a candidate

    //printf("ZZZ=%s adding candidate=%s [new vote]\n", _node->get_node_id().str().c_str(),
    //  node_id.str().c_str());

    CandidatePeer* candidate =  it_cand->second;

    // update the time the node was last voted
    if (candidate->set_nc(candidate_nc)) {
      invalidate_candidate(node_id);
    }
    candidate->add_vote();

  } else if (_candidates.size() < globops.rappel_num_candidates) {
    // node_id NOT currently a s1-candidate, AND
    // we currently have fewer than optimal number of candidates -->
    // add candidate node to s1-candidates heap

#ifdef DEBUG
    printf("ZZZ=%s adding candidate=%s [new]\n", _node->get_node_id().str().c_str(), node_id.str().c_str());
#endif // DEBUG

    CandidatePeer* candidate = new CandidatePeer(node_id, candidate_nc);

    // if we already know this node's bloom filter
    map<node_id_t, CandidatePeer*>::iterator
      it_prot = _protected_candidates.find(node_id);

    if (it_prot != _protected_candidates.end()) {
      candidate->set_nc(it_prot->second->get_nc());
      candidate->set_bloom_filter(*(it_prot->second->get_bloom_filter()));
    }

    // remember it (in the map)
    _candidates[node_id] = candidate;

  } else {

    // node_id NOT currently a s1-candidate, AND
    // ok, we already have the maximum number of candidates ->
    // add candidate note to s1_candidate list, -AND- 
    // evict the oldest s1_candidate (by LRU policy)

    // add candidate node to s1-candidates heap
    CandidatePeer* new_candidate = new CandidatePeer(node_id, candidate_nc);

    // if we already know this node's bloom filter
    map<node_id_t, CandidatePeer*>::iterator
      it_prot = _protected_candidates.find(node_id);

    if (it_prot != _protected_candidates.end()) {
      new_candidate->set_nc(it_prot->second->get_nc());
      new_candidate->set_bloom_filter(*(it_prot->second->get_bloom_filter()));
    }

    // remember it (in the map)
    _candidates[node_id] = new_candidate;

    // remove LRU node from s1-candidates map
    remove_old_candidate();

#ifdef DEBUG
    printf("ZZZ=%s adding candidate=%s [replacing old (unkown)]\n",
      _node->get_node_id().str().c_str(), node_id.str().c_str());
#endif // DEBUG
  }

  assert(_candidates.size() <= globops.rappel_num_candidates);
}

void Rappel::remove_old_candidate() {

  const unsigned int CANDIDATES_TO_REMOVE = 10;
  assert(_candidates.size() > CANDIDATES_TO_REMOVE);
  std::priority_queue<CandidateAge,
    std::vector<CandidateAge>, 
    std::less<CandidateAge> > oldest_candidates;

  for (map<node_id_t, CandidatePeer*>::iterator
    it = _candidates.begin(); it != _candidates.end(); it++) {

    // dereference the iterator to get pointer to the peer
    CandidatePeer* peer = it->second;

    // find node with the oldest vote
    CandidateAge newest_old_candidate = oldest_candidates.top();
    if (oldest_candidates.size() < CANDIDATES_TO_REMOVE
      || peer->get_last_vote_at() < newest_old_candidate.age) {

      if (oldest_candidates.size() >= CANDIDATES_TO_REMOVE) {
        oldest_candidates.pop();
      }

      // add current candidate
      CandidateAge old_candidate;
      old_candidate.id = peer->get_id();
      old_candidate.age =  peer->get_last_vote_at();
      oldest_candidates.push(old_candidate);
    }
  }

  assert(oldest_candidates.size() == CANDIDATES_TO_REMOVE);

  while (!oldest_candidates.empty()) {

    CandidateAge old_candidate = oldest_candidates.top();
    oldest_candidates.pop();
    map<node_id_t, CandidatePeer*>::iterator it = _candidates.find(old_candidate.id);
    assert(it != _candidates.end());

    delete(it->second); // unallocate memory
    _candidates.erase(it);
  }
}   

/**
 * Pick one of the nodes from s1-candidates to be a replacement candidate:
 * Selects a candidate from s1-candidates that has not yet been selected for
 * replacement and has the maximum number of votes
 * @returns If no plausible candidate is found returns NO_NODE, else returns
 * the node_id of the replacement candidate
 */
node_id_t Rappel::most_popular_candidate() {

  deque<node_id_t> most_popular_candidates;
  unsigned int max_votes = 0;
  Role friend_role(ROLE_FRIEND);

  for (map<node_id_t, CandidatePeer*>::const_iterator
    it = _candidates.begin(); it != _candidates.end(); it++) {

    // dereference the iterator to get pointer to the peer
    const node_id_t peer_id = it->first;
    const CandidatePeer* const peer = it->second;

    // find node with most votes (that has not yet been audited)
    if (!peer->is_audited() && !peer_plays_role(peer_id, friend_role)) {
      unsigned int peer_votes = peer->get_num_votes();
      if (peer_votes > max_votes) {
        most_popular_candidates.clear();
        most_popular_candidates.push_back(peer_id);
        max_votes = peer_votes;
      } else if (peer_votes == max_votes) {
        most_popular_candidates.push_back(peer_id);
      }
    }
  }   

  // mark most popular candidate as audited
  if (most_popular_candidates.size() > 0) {

    // select a random candidate
    node_id_t candidate = most_popular_candidates[_rng->rand_uint(0,
      most_popular_candidates.size() - 1)];

    // mark current candidate as audited
    _candidates[candidate]->set_audited();
    return candidate;

  } else {
    return NO_SUCH_NODE;
  }
}

void Rappel::invalidate_candidate(const node_id_t& node_id) {

  // the node may not be a candidate -- if not, ignore
  map<node_id_t, CandidatePeer*>::iterator it_cand = _candidates.find(node_id);
  if (it_cand != _candidates.end()) {
    it_cand->second->unset_audited();
  }
}

/**
 * Replace the "last tested for replacement" value for all s1 candidates as s1
 * has recently changed.
 */
void Rappel::invalidate_all_candidates() {

  map<node_id_t, CandidatePeer*>::iterator it;
  for (it = _candidates.begin(); it != _candidates.end(); it++) {

    // dereference the iterator to get pointer to the peer
    CandidatePeer* peer = it->second;
    peer->unset_audited();
  }
}

unsigned int Rappel::bloom_filter_version(const node_id_t& node_id) const {
  return get_bloom_filter(node_id)->get_version();
}

bool Rappel::know_bloom_filter(const node_id_t& node_id) const {

  // if candidate is a peer, get its bloom filter
  if (is_a_peer(node_id)) {

    const Neighbor* nbr = _peers.find(node_id)->second;
    return (nbr->get_bloom_filter()->get_version() > 0);
  }

  // if not a peer, node may be in s1_candidate
  map<node_id_t, CandidatePeer*>::const_iterator
    it_cand = _candidates.find(node_id);
  if (it_cand != _candidates.end()) {
    return (it_cand->second->get_bloom_filter()->get_version() > 0);
  }

  // finaly: the last option, the node must be the protected s1 candidate
  map<node_id_t, CandidatePeer*>::const_iterator
    it_prot = _protected_candidates.find(node_id);
  if (it_prot != _protected_candidates.end()) {
    return (it_prot->second->get_bloom_filter()->get_version() > 0);
  }

  // default: nope, don't know
  return false;
}

const BloomFilter* Rappel::get_bloom_filter(const node_id_t& node_id) const {

  // if candidate is a peer, get its bloom filter
  if (is_a_peer(node_id)) {

    const Neighbor* nbr = _peers.find(node_id)->second;
//    printf("node=%s filter comes froyym nbr\n", node_id.str().c_str());
    return nbr->get_bloom_filter();
  }

  // if not a peer, node may be in s1_candidate
  map<node_id_t, CandidatePeer*>::const_iterator
    it_cand = _candidates.find(node_id);
  if (it_cand != _candidates.end()) {
//    printf("node=%s filter comes froyym candidate\n", node_id.str().c_str());
    return it_cand->second->get_bloom_filter();
  }

  // finaly: the last option, the node must be the protected s1 candidate
  map<node_id_t, CandidatePeer*>::const_iterator
    it_prot = _protected_candidates.find(node_id);
  if (it_prot != _protected_candidates.end()) {
//    printf("node=%s filter comes from protected\n", node_id.str().c_str());
    return it_prot->second->get_bloom_filter();
  }

  assert(false);
  return NULL;
}

/**
 * Update the bloom filter (at the same time also update foreign nc, if
 * necessary
 */
void Rappel::update_bloom_filter(const node_id_t& node_id, const BloomFilter& filter,
  const Coordinate& nc, bool while_auditing) {

  // if candidate is a peer, updat the bloom filter there
  if (is_a_peer(node_id)) {

    Neighbor* nbr = _peers[node_id];
    /* bool filter_changed = */nbr->set_bloom_filter(filter);
    /* bool nc_changed = */nbr->set_nc(nc);
/*
    if (invalidate && (filter_changed || nc_changed)) {
      invalidate_candidate(node_id);
    }
*/
    return;
  }

  // if not a peer, node must be in s1_candidate -- but only
  // if fetching bloom filter while auditing
  map<node_id_t, CandidatePeer*>::iterator it_cand = _candidates.find(node_id);
  if (it_cand == _candidates.end()) {
    assert(while_auditing == false);
    return;
  }

  // dereference the iterator to get pointer to the candidate peer 
  CandidatePeer* peer = it_cand->second;

  /* bool filter_changed = */peer->set_bloom_filter(filter);
  /* bool nc_changed = */peer->set_nc(nc);
/*
  if (invalidate && (filter_changed || nc_changed)) {
    invalidate_candidate(node_id);
  }
*/
}

bool Rappel::know_nc(const node_id_t& node_id) const {

  // if candidate is a peer, get its bloom filter
  map<node_id_t, Neighbor*>::const_iterator it_peer = _peers.find(node_id);
  if (it_peer != _peers.end()) {
    return (it_peer->second->get_nc().get_version() > 0);
  }

  // if not a peer, node may be a candidate
  map<node_id_t, CandidatePeer*>::const_iterator
    it_cand = _candidates.find(node_id);
  if (it_cand != _candidates.end()) {
    return (it_cand->second->get_nc().get_version() > 0);
  }

  // finaly: the last option, the node must be a protected candidate
  map<node_id_t, CandidatePeer*>::const_iterator
    it_prot = _protected_candidates.find(node_id);
  if (it_prot != _protected_candidates.end()) {
    return (it_prot->second->get_nc().get_version() > 0);
  }

  // default: nope, don't know
  return false;
}

Coordinate Rappel::get_nc(const node_id_t& node_id) const {

  // if candidate is a peer, get the nc from _peers
  map<node_id_t, Neighbor*>::const_iterator it_peer = _peers.find(node_id);
  if (it_peer != _peers.end()) {
    return it_peer->second->get_nc();
  }

  // if not a peer, node may be a candidate
  map<node_id_t, CandidatePeer*>::const_iterator
    it_cand = _candidates.find(node_id);
  if (it_cand != _candidates.end()) {
    return it_cand->second->get_nc();
  }

  // finaly: the last option, the node must be a protected candidate
  map<node_id_t, CandidatePeer*>::const_iterator
    it_prot = _protected_candidates.find(node_id);
  if (it_prot != _protected_candidates.end()) {
    return it_prot->second->get_nc();
  }

  assert(false);
  Coordinate ret;
  return ret;
}

void Rappel::send_bloom_req(const node_id_t& node_id, bool audit_friend) {

/* commenting out sep 14, 2007
  // see if there is an outstanding request
  if (_outstanding_bloom_requests.find(node_id)
    != _outstanding_bloom_requests.end()) {

    // this shouldn't happen
    //assert(audit_friend);

    // already fetching
    return;
  }

  _outstanding_bloom_requests.insert(node_id);
*/

  // First, schedule the wait_after_bloom_req timer event
  WaitAfterBloomReqData* evt_data = new WaitAfterBloomReqData;
  evt_data->neighbor = node_id;
  evt_data->audit_friend = audit_friend;

  Event event;
  event.node_id = _node->get_node_id();
  event.layer = LYR_APPLICATION;
  event.flag = EVT_RAPPEL_WAIT_AFTER_BLOOM_REQ;
  event.data = evt_data;
  event.whence = /*_timer->get_time() +*/ globops.rappel_reply_timeout;

  event_id_t event_id = _timer->schedule_event(event);
  assert(_pending_events.find(event_id) == _pending_events.end());
  _pending_events[event_id] = true;
      
  // Next, send the bloom_req message
  BloomReqData* req_data = new BloomReqData;
  req_data->sender_coord = _node->get_nc();
  req_data->wait_for_timer = event_id;
  req_data->audit_friend = audit_friend;

  Message msg;
  msg.layer = LYR_APPLICATION;
  msg.flag = MSG_RAPPEL_BLOOM_REQ;
  msg.src = _node->get_node_id();
  msg.dst = node_id;
  msg.data = req_data;

  safe_send(msg);

#ifndef IGNORE_ETIENNE_STATS
  _curr_cycle_stats.msgs_out_bloom_req++;
#endif // IGNORE_ETIENNE_STATS
}

void Rappel::audit() {

  // s1's size should be bounded by size
  assert(_friends.size() <= globops.rappel_num_friends);

  if (_currently_auditing != NO_SUCH_NODE) {
#ifdef DEBUG
    printf("time=%s, node=%s already auditing=%s\n",
      _timer->get_time_str().c_str(),
      _node->get_node_id().str().c_str(),
      _currently_auditing.str().c_str());
#endif // DEBUG
    return;
  }
      
  assert(_protected_candidates.size() == 0);

  node_id_t replacement_candidate = most_popular_candidate();

  if (replacement_candidate == NO_SUCH_NODE) {
#ifdef DEBUG
    printf("time=%s, node=%s No suitable candidates (num_candidates=%u)\n",
     _timer->get_time_str().c_str(), _node->get_node_id().str().c_str(),
     _candidates.size());
#endif // DEBUG
    // do nothing if there is no candidate
    return;
  }

  _num_audits++;

  // mark s1 improvement in progress
  _currently_auditing = replacement_candidate;
/*
  cout << "12 time=" << _timer->get_time_str() << "; node=" << _node->get_node_id().str()
    << "; currently_auditing=" << _currently_auditing.str() << endl;
*/

  //printf("ZZZ=%u Replacement candidate=%u\n", _node->get_node_id(), replacement_candidate);
  if (know_bloom_filter(replacement_candidate) == false) {

    // bloom filter not known for replacement candidate, fetch directly
    //printf("ZZZ=%u Don't know bloom filter...fetching\n", _node->get_node_id());

    send_bloom_req(replacement_candidate, true);
    
  } else { 

    //printf("ZZZ=%u I know bloom filter...\n", _node->get_node_id());
    // we have the bloom fitler for the replacement_candidate. proceed...
    audit_with(replacement_candidate);
  }
}


// TODO: need to optimize this!!!!!!!!!!!!!!!! july 01, 2007

void Rappel::initiate_feed_join_req(const feed_id_t& feed_id, 
  map<node_id_t, unsigned int>& attempted, vector<node_id_t>& failed,
  const node_id_t& fwd_node, RappelJoinType request_type) {

  // feed must exist
  map<feed_id_t, Feed*>::iterator it_feed = _feed_set.find(feed_id);
  assert(it_feed != _feed_set.end());
  Feed* feed = it_feed->second;

  // don't seek two parents at the same time
  if (feed->seeking_parent()) {
#ifdef DEBUG
    printf("STOPPPING MULTIPLE SEEK\n");
#endif // DEBUG
    return;
  }

  node_id_t publisher_node = feed->get_publisher();
  node_id_t target_node = NO_SUCH_NODE; // default target_node

  // get a joining point in Friend U all S2 if exists
  // if joining point not found use the publisher
  vector<node_id_t> feed_subscribers = find_feed_subscribers(feed_id);
#ifdef DEBUG
  printf("There are a possible %u subscribers for feed %s (amongst my peers)\n",
    feed_subscribers.size(), feed_id.str().c_str());
#endif // DEBUG

  // add the publisher -- at the end
  if (find(feed_subscribers.begin(), feed_subscribers.end(), publisher_node) ==
    feed_subscribers.end()) {

    feed_subscribers.push_back(publisher_node);
  }

  // add the fwd node if not already part of feed_subscribers -- at the beginnig
  if (fwd_node != NO_SUCH_NODE) {
  
    // send the join request to fwding node -- if not already attempted before
    if (attempted.find(fwd_node) == attempted.end() &&
      find(failed.begin(), failed.end(), fwd_node) == failed.end()) {

        target_node = fwd_node;

    } else { // else, add fwd node to subscriber list (if not already present)

      vector<node_id_t>::iterator it = find(feed_subscribers.begin(),
        feed_subscribers.end(), fwd_node);

      if (it == feed_subscribers.end()) {
        feed_subscribers.insert(feed_subscribers.begin(), fwd_node);
      }
    }
  }

  // if target_node not already found, find the first node from feed_subscribers
  // that we have not already sent a request to (i.e., not in attempted or failed)
  if (target_node == NO_SUCH_NODE) {

    for (unsigned int i = 0; i < feed_subscribers.size(); i++) {

#ifdef DEBUG
      printf("possible subzcriber=%s\n", feed_subscribers[i].str().c_str());
#endif // DEBUG

      if (attempted.find(feed_subscribers[i]) == attempted.end() &&
        find(failed.begin(), failed.end(), feed_subscribers[i]) == failed.end()) {

        target_node = feed_subscribers[i];
        break;
      }
    }
  }

  // if all nodes are "attempted" or "failed" -- find the node
  // with the min attempts that is not yet failed
  if (target_node == NO_SUCH_NODE) {

    node_id_t min_node = NO_SUCH_NODE;
    unsigned int min_attempts = 999999; // whatever
    unsigned int total_attempts = 0;

    for (unsigned int i = 0; i < feed_subscribers.size(); i++) {

      if (find(failed.begin(), failed.end(), feed_subscribers[i]) ==
        failed.end()) {

        assert(attempted.find(feed_subscribers[i]) != attempted.end());
        total_attempts += attempted[feed_subscribers[i]];

        if (min_node == NO_SUCH_NODE || 
          attempted[feed_subscribers[i]] < min_attempts) {

          min_attempts = attempted[feed_subscribers[i]];
          min_node = feed_subscribers[i];
        }
      }
    }

    target_node = min_node;
#ifdef DEBUG
    printf("Contacting node=%s total attempts: %u\n", target_node.str().c_str(), total_attempts);
#endif // DEBUG
  }

  fflush(stdin);
  assert(target_node != NO_SUCH_NODE);

  // no point sending a rejoin request to parent
  if (request_type == PERIODIC_REJOIN && feed->has_parent()
    && feed->get_parent() == target_node) {

    assert(!feed->seeking_parent());
    return;
  }

  // make sure that a node is not being retried (i.e., no cycles)
  // TODO: Jay's note: This is probably a likely scenario (with dynamic NC)
  //assert(find(attempted.begin(), attempted.end(), target_node) ==
  //  attempted.end());

  if (attempted.find(target_node) != attempted.end()) {
    attempted[target_node]++;
    assert(attempted[target_node] <= 3);
  } else {
    attempted[target_node] = 1;
  }

  feed->set_seeking_parent();

  // First, schedule the wait_after_join_req timer event
  WaitAfterJoinReqData * evt_data = new WaitAfterJoinReqData;
  evt_data->feed_id = feed_id;
  evt_data->request_type = request_type;
  evt_data->neighbor = target_node;
  evt_data->attempted = attempted;
  evt_data->failed = failed;

  Event event;
  event.node_id = _node->get_node_id();
  event.layer = LYR_APPLICATION;
  event.flag = EVT_RAPPEL_WAIT_AFTER_FEED_JOIN_REQ;
  event.data = evt_data;
  event.whence = /*_timer->get_time() +*/ globops.rappel_reply_timeout;

  event_id_t event_id = _timer->schedule_event(event);
  assert(_pending_events.find(event_id) == _pending_events.end());
  _pending_events[event_id] = true;

  // Next, send the message
  JoinReqData* req_data = new JoinReqData;
  req_data->sender_coord = _node->get_nc();
  req_data->wait_for_timer = event_id;

  req_data->candidates = get_candidate_list();
  req_data->feed_id = feed_id;
  req_data->request_type = request_type;
  req_data->attempted = attempted;
  req_data->failed = failed;

  Message msg;
  msg.layer = LYR_APPLICATION;
  msg.flag = MSG_RAPPEL_FEED_JOIN_REQ;
  msg.src = _node->get_node_id();
  msg.dst = target_node;
  msg.data = req_data;

  safe_send(msg);

#ifndef IGNORE_ETIENNE_STATS
  switch (request_type) {
  case INITIAL_JOIN:
    _curr_cycle_stats.msgs_out_join_req++;
    break;
  case REJOIN_DUE_TO_LOST_PARENT:
    _curr_cycle_stats.msgs_out_lost_parent_rejoin_req++;
    break;
  case REJOIN_DUE_TO_CHANGE_PARENT:
    _curr_cycle_stats.msgs_out_change_parent_rejoin_req++;
    break;
  case PERIODIC_REJOIN:
    _curr_cycle_stats.msgs_out_periodic_rejoin_req++;
    break;
  case REDIRECTED_PERIODIC_REJOIN:
    _curr_cycle_stats.msgs_out_periodic_rejoin_req++;
    // not keep tracking of outgoing redirected periodic rejoins
    break;
  default:
    assert(false); // no other request type
  } 
#endif // IGNORE_ETIENNE_STATS

  // join (this creates s2 and put candidates in s1 candidate list)
  // this is done in recv_join_ok
}

std::string Rappel::generate_random_string(unsigned int size) const {

  return std::string(size, 'S');
}

/**
 * Compute the set of nodes that subscribe to a given feed from Friend and
 * all other S2. Used for finding a parent to a node adding a new feed.
 */
vector<node_id_t> Rappel::find_feed_subscribers(const feed_id_t& feed_id) {

  vector<node_id_t> nodes;

  map<feed_id_t, Feed*>::iterator it_feed = _feed_set.find(feed_id);
  assert(it_feed != _feed_set.end());
  Feed* _feed = it_feed->second;

  // This is probabilistic if we use the bloom filters for that: there is a
  // (small) risk that a join request to one of the returned feeds leads to an
  // error (resulting in a join_deny)

  for (map<node_id_t, Neighbor*>::iterator it = _peers.begin();
    it != _peers.end(); it++) {

    // does the peer contains the feed (whp) ?
    Neighbor* nbr = it->second;
    if (nbr->get_bloom_filter()->contains(_feed->str())) {

      // arrange in order -- based on proximity
      vector<node_id_t>::iterator it2;
      for (it2 = nodes.begin(); it2 != nodes.end(); it2++) {

        Neighbor* nbr2 = _peers[*it2];
        if (_node->get_nc().get_distance(nbr->get_nc()) <
          _node->get_nc().get_distance(nbr2->get_nc())) {

          break;
        }
      }

      // make sure there are no duplicates
      assert(find(nodes.begin(), nodes.end(), it->first) == nodes.end());
      nodes.insert(it2, it->first);
    }
  }
  
  return nodes;
}

void Rappel::debug_peers(unsigned int seq_num) const {

  printf("DBG_%u_peers %s %u %u\n", seq_num, _node->get_node_id().str().c_str(),
    _node->is_rappel_online(), _peers.size());

  // print s1 peers
  printf("DBG_%u_friends %s %u %u", seq_num, _node->get_node_id().str().c_str(),
    _node->is_rappel_online(), _friends.size());

  if (_friends.size() > 0) {
    printf(" [ ");
    for (vector<node_id_t>::const_iterator it = _friends.begin();
      it != _friends.end(); it++) {

      if (it != _friends.begin()) {
        printf(", ");
      }
      printf("%s", it->str().c_str());
    }
    printf(" ]");
  } else {
    printf(" (empty)");
  }
  printf("\n");

  // print s1 candidates
  printf("DBG_%u_candidates %s %u %u\n", seq_num,
    _node->get_node_id().str().c_str(), _node->is_rappel_online(),
    _candidates.size());

  for (map<node_id_t, CandidatePeer*>::const_iterator it = _candidates.begin();
    it != _candidates.end(); it++) {

    // dereference the iterator to get pointer to the peer
    CandidatePeer* peer = it->second;

    // print out candidate info
    printf("   (%s, %u, %s, %u)\n", peer->get_id().str().c_str(),
      peer->get_num_votes(),
      _timer->get_time_str(peer->get_last_vote_at()).c_str(),
      peer->is_audited());
  }

  // Etienne: print the s1 set utility measure
  // this is bad but i had to remove the const from the method definition
  // for this to work. do not have time to go into c++ details.

  double utility = calculate_friend_set_utility(_friends);
  printf("DBG_%u_friend_set_utility %s %f\n", 
	 seq_num,
	 _node->get_node_id().str().c_str(),
	 utility);
}

void Rappel::debug_feeds(unsigned int seq_num) const {

  //printf("I subscribe to %u feeds.\n", _feed_set.size() - _is_publisher);

  // print feeds
  map<feed_id_t, Feed*>::const_iterator it;
  for (it = _feed_set.begin(); it != _feed_set.end(); it++) {

    Feed* feed = it->second;

    printf("DBG_%u_feed %s %u %s", seq_num, _node->get_node_id().str().c_str(),
    _node->is_rappel_online(), feed->get_feed_id().str().c_str());

    if (feed->get_feed_id() == (feed_id_t) _node->get_node_id()) {
      printf(" [OWN]\n");
    } else {
      printf("\n");
    }

    printf("DBG_%u_parent %s %u %s %s\n", seq_num, _node->get_node_id().str().c_str(),
      _node->is_rappel_online(), feed->get_feed_id().str().c_str(),
      _node->get_nc().to_string().c_str());
    if (feed->has_parent()) {
      printf(" %s %u\n", feed->get_parent().str().c_str(), feed->get_ancestors().size());
    } else {
      printf(" -1 %u\n", feed->get_ancestors().size());
    }

    printf("DBG_%u_children %s %u %s %u", seq_num, _node->get_node_id().str().c_str(),
     _node->is_rappel_online(), feed->get_feed_id().str().c_str(), feed->get_num_children());

    if (feed->get_num_children() > 0) {
      printf(" [ ");
      vector<node_id_t> children = feed->get_children();
      for (unsigned int i = 0; i < children.size(); i++) {
        if (i != 0) {
          printf(", ");
        }
        printf("%s", children[i].str().c_str());
      }
      printf(" ]");
    }
    printf("\n");

/*
    feed->reset_debug_period();
*/
  }
}

/**
 * Get a list of s1 peers with their NC to pass along to a neighbor on a PING
 * reply (of type FRIEND).
 */
vector<node_id_t> Rappel::get_candidate_list() const {

  vector<node_id_t> peers;
  peers = _friends;

  if (_candidates.size() > 0) {

    unsigned int lucky_candidate_num = _rng->rand_uint(0, _candidates.size() - 1);
    map<node_id_t, CandidatePeer*>::const_iterator it = _candidates.begin();
    for (unsigned int counter = 0; counter < lucky_candidate_num; counter++, it++);
    node_id_t lucky_candidate = it->first;

    // Note: the lucky candidate can actually already be a friend -- oh well
    peers.push_back(lucky_candidate);
  }

  return peers;
}

void Rappel::send_friend_req(const node_id_t& target_node_id,
  const node_id_t& node_to_prune, const vector<node_id_t>& next_targets) {

  assert(_currently_auditing == target_node_id);
  assert(know_bloom_filter(target_node_id));
  map<node_id_t, CandidatePeer*>::const_iterator
    it_prot = _protected_candidates.find(target_node_id);
  assert(it_prot != _protected_candidates.end());
  assert(it_prot->second->get_bloom_filter()->get_version() > 0);

  // first, create the timer
  WaitAfterAddFriendReqData* evt_data = new WaitAfterAddFriendReqData;
  assert(evt_data != NULL);
  evt_data->neighbor = target_node_id;
  evt_data->next_targets = next_targets;

  Event event;
  event.node_id = _node->get_node_id();
  event.layer = LYR_APPLICATION;
  event.flag = EVT_RAPPEL_WAIT_AFTER_ADD_FRIEND_REQ;
  event.data = evt_data;
  event.whence = /*_timer->get_time() +*/ globops.rappel_reply_timeout;

  event_id_t event_id = _timer->schedule_event(event);
  assert(_pending_events.find(event_id) == _pending_events.end());
  _pending_events[event_id] = true;

  //second, send a request
  AddFriendReqData* req_data = new AddFriendReqData;
  assert(req_data != NULL);
  req_data->sender_coord = _node->get_nc();
  req_data->wait_for_timer = event_id;

  req_data->candidates = get_candidate_list();
  // no node to prune here
  req_data->node_to_be_pruned = node_to_prune;
  req_data->bloom_filter_version = bloom_filter_version(target_node_id);
  req_data->next_targets = next_targets;

  Message msg;
  msg.layer = LYR_APPLICATION;
  msg.flag = MSG_RAPPEL_ADD_FRIEND_REQ;
  msg.src = _node->get_node_id();
  msg.dst = target_node_id;
  msg.data = req_data;

  safe_send(msg);

#ifndef IGNORE_ETIENNE_STATS
  _curr_cycle_stats.msgs_out_friend_req++;
#endif // IGNORE_ETIENNE_STATS
}

/**
 * Checks if using a replacement candidate for any node in s1 leads to
 * improved utility. This call is linear with the number of 
 * nodes in s1.
 * @param replacement_candidate The replacement candidate
 */
void Rappel::audit_with(const node_id_t& replacement_candidate) {

  assert(_currently_auditing == replacement_candidate);
  if (_protected_candidates.size() > 0) {

    for (map<node_id_t, CandidatePeer*>::iterator it = _protected_candidates.begin();
      it != _protected_candidates.end(); it++) {

      printf("time=%s, node=%s, protected candidate: %s\n", 
        _timer->get_time_str().c_str(), _node->get_node_id().str().c_str(),
        it->first.str().c_str());
    }
    assert(false);
  }

  unsigned int replacement_index = globops.rappel_num_friends;

  if (_friends.size() == globops.rappel_num_friends) {
    // s1 is already saturated

    double base_util = calculate_friend_set_utility(_friends); 
    double curr_max_util = base_util * globops.rappel_friend_set_utility_delta;

    // check if replacement_candidate increases utility
    vector<node_id_t> friends_copy = _friends;
    for (unsigned int i = 0; i < friends_copy.size(); i++) {

      // replace the i_th node with the replacement_candidate
      friends_copy[i] = replacement_candidate;

      // calculate new utility
      double util = calculate_friend_set_utility(friends_copy);

      // uhh, replacing this node, leads to better overall utility
      if (util > curr_max_util) {
        curr_max_util = util;
        replacement_index = i;
      }

      // restore i_th node
      friends_copy[i] = _friends[i];
    }
  }

  if (_friends.size() < globops.rappel_num_friends ||
    replacement_index != globops.rappel_num_friends) { // found a replacement

    //printf("ZZZ=%u Yes, candidate=%u can improve utility. Asking....\n",
    //  _node->get_node_id(), replacement_candidate, _node->get_node_id());

    node_id_t node_to_prune;
    if (_friends.size() == globops.rappel_num_friends) {
      //printf("node to prune. wow: %s\n", _friends[replacement_index].str().c_str());
      node_to_prune = _friends[replacement_index];
    } else {
      node_to_prune = NO_SUCH_NODE;
      //printf("no node to prune: (%s)\n", req_data->node_to_be_pruned.str().c_str());
    }

    assert(get_bloom_filter(replacement_candidate)->get_version() > 0);

    CandidatePeer* cp = new CandidatePeer(replacement_candidate,
      get_nc(replacement_candidate));
    cp->set_bloom_filter(*(get_bloom_filter(replacement_candidate)));

    assert(_protected_candidates.find(replacement_candidate)
      == _protected_candidates.end());
    _protected_candidates[replacement_candidate] = cp;

    // ask replacement_candidate if it is ok to add it to our friend set 
    send_friend_req(replacement_candidate, node_to_prune, vector<node_id_t>());

  } else {

    //printf("ZZZ=%u Yes, candidate=%u does not improve utility.\n",
    //  _node->get_node_id(), replacement_candidate);

    // end of friend audit
    _currently_auditing = NO_SUCH_NODE;
/*
    cout << "13 time=" << _timer->get_time_str() << "; node=" << _node->get_node_id().str()
      << "; currently_auditing=" << _currently_auditing.str() << endl;
*/
    assert(_protected_candidates.size() == 0);
  }
}

/**
 * Improved utility measure function (see paper)
 * JAY'S NOTE (for Etienne): Do not touch _peers directly. Candidate nodes are
 * not "peers", and as such, their information is not stored in the _peers
 * vector.
 */
double Rappel::calculate_friend_set_utility(const vector<node_id_t>& nodes) const {

  double ret = 0.0;

  unsigned int table_size = _filter.get_table_size();
  unsigned int num_nodes = nodes.size();
  assert(num_nodes <= globops.rappel_num_friends);

  // initialize base and bonus scores
  vector<unsigned int> intersection_size(num_nodes, 0);
  vector<unsigned int> union_size(num_nodes, 0);
  vector<unsigned int> bonus(num_nodes, 0);
  //unsigned int my_active_bits = 0;

  for (unsigned int i = 0; i < num_nodes; i++) {
    know_bloom_filter(nodes[i]);
  }

  // this chunk is really only needed to calculate the BONUS
  if (globops.rappel_friend_set_utility_depends_on.find("BONUS", 0) != std::string::npos) {

    for (unsigned int byte = 0; byte < table_size; byte++) {
  
      unsigned char my_byte = _filter.get_byte(byte);
      unsigned char remaining_byte_val = my_byte;
  
      unsigned int bit = 0;
      while (remaining_byte_val) { // at least 1 bit is on
  
        if (remaining_byte_val & 0x1u) { // proceed only if 1 bit is on

          unsigned int absolute_bit = byte * 8 + bit;
          unsigned int nodes_with_active_bit = 0;
          unsigned int last_node_with_active_bit = 0;
  
          for (unsigned int i = 0; i < num_nodes; i++) {
            bool is_active = get_bloom_filter(nodes[i])->contains_bit(absolute_bit);
            intersection_size[i] += (is_active? 1: 0);
            nodes_with_active_bit += (is_active? 1: 0);
            last_node_with_active_bit = (is_active? i: last_node_with_active_bit);
          }
  
  	if (num_nodes > 0) { // valgrind reported error: if num_nodes == 0
            bonus[last_node_with_active_bit] += ((nodes_with_active_bit == 1) ? 1 : 0);
  	}
        }
  
        remaining_byte_val >>= 1;
        bit++;
      }
    }
  }

  for (unsigned int i = 0; i < num_nodes; i++) {

    if (globops.rappel_friend_set_utility_depends_on != "NETWORK_ALONE") {

      const BloomFilter* const filter = get_bloom_filter(nodes[i]);
      if (globops.rappel_friend_set_utility_depends_on.find("BONUS", 0) != std::string::npos) {
        assert(intersection_size[i] == _filter.get_intersection_size(*filter));
      } else {
        intersection_size[i] = _filter.get_intersection_size(*filter);
      }
      union_size[i] = _filter.get_union_size(*filter);
  
      if (globops.rappel_friend_set_utility_depends_on == "INTEREST_ALONE_WITH_BONUS") {

        //ret += static_cast<double>(bonus[i]) +
        ret += static_cast<double>(bonus[i]) / static_cast<double>(_filter.active_bits()) +
               static_cast<double>(intersection_size[i]) / static_cast<double>(union_size[i]);

      } else if (globops.rappel_friend_set_utility_depends_on == "INTEREST_ALONE") {

        ret += static_cast<double>(intersection_size[i]) / static_cast<double>(union_size[i]);

      } else if (globops.rappel_friend_set_utility_depends_on == "BOTH_WITH_INTEREST_BONUS") {

        //ret += static_cast<double>(bonus[i]) +
        double distance_in_secs = _node->get_nc().get_distance(get_nc(nodes[i])) / 1000.0;
        ret += globops.rappel_friend_set_utility_k / distance_in_secs
               * (static_cast<double>(bonus[i]) / static_cast<double>(_filter.active_bits()) +
               static_cast<double>(intersection_size[i]) / static_cast<double>(union_size[i]));

      } else if (globops.rappel_friend_set_utility_depends_on == "BOTH") {

        double distance_in_secs = _node->get_nc().get_distance(get_nc(nodes[i])) / 1000.0;
        ret += globops.rappel_friend_set_utility_k / distance_in_secs
               * static_cast<double>(intersection_size[i]) / static_cast<double>(union_size[i]);

      } else {

        assert(false);
      }

    } else { // NETWORK_ALONE

      ret += (1.00 / _node->get_nc().get_distance(get_nc(nodes[i])));
    }
  }

  return ret;
}

void Rappel::safe_send(Message& msg) {
  assert(_node->is_rappel_online());
  _transport->send(msg);
}

/*
void Rappel::signal_nc_changed() {

  // change publisher's own feed NC
  if (_is_publisher) {
    update_own_feed_nc();

    // flush the ancestors down to the children
    flush_ancestry((feed_id_t) _node->get_node_id());
  }
 
#ifdef DEBUG
  printf("RAPPEL NOTIFIED OF COORD CHANGE for %s -- checking invariant\n",
    _node->get_node_id().str().c_str());
#endif // DEBUG
  vector<node_id_t> sent_new_coords;

  // for all feeds: verify coordinate invariant
  for (map<feed_id_t, Feed*>::iterator it = _feed_set.begin();
    it != _feed_set.end(); it++) {

    feed_id_t feed_id = it->first;
    Feed* feed = it->second;

    // check parent first
    bool closer_to_pub_then_parent = false;
    if (feed->has_parent()) {
       node_id_t parent = feed->get_parent();

      if (parent != feed->get_publisher()) { // if I'm the publisher, no real parent
        assert(know_foreign_nc(parent));

        // inform all peers of coord change -- via a ping, whether it effects
        // the invariant or not (because it affect newly joining nodes who might
        // be caught in a stale state)
        if (find(sent_new_coords.begin(), sent_new_coords.end(), parent)
          == sent_new_coords.end()) {

#ifdef DEBUG
          printf("  SENDING updated COORD info to peer: %s\n", parent.str().c_str());
#endif // DEBUG
          send_ping_requests(parent);
          sent_new_coords.push_back(parent);
        }
      
        if (get_foreign_nc(parent).get_distance(feed->get_publisher_nc()) >
          _node->get_nc().get_distance(feed->get_publisher_nc())) {

          closer_to_pub_then_parent = true;

          // I'm closer to the publisher then my parent
          NotAChildData* msg_data = new NotAChildData;
          msg_data->sender_coord = _node->get_nc();

          msg_data->feed_id = feed->get_feed_id();
          msg_data->publisher_nc = feed->get_publisher_nc();

          Message msg;
          msg.src = _node->get_node_id();
          msg.dst = feed->get_parent();
          msg.layer = LYR_APPLICATION; 
          msg.flag = MSG_RAPPEL_FEED_NO_LONGER_YOUR_CHILD;
          msg.data = msg_data;

          safe_send(msg);

#ifndef IGNORE_ETIENNE_STATS
          _curr_cycle_stats.msgs_out_no_longer_your_child++;
#endif // IGNORE_ETIENNE_STATS

          Role parent_role(ROLE_FEED_PARENT, feed_id);
          remove_peer_from_role(parent, parent_role, false);

	  // rejoin at random ancestor
          node_id_t ancestor = feed->get_random_ancestor();

          map<node_id_t, unsigned int> attempted;
          vector<node_id_t> failed;
          initiate_feed_join_req(feed_id, attempted, failed, ancestor, true);
        }
      }
    }

    // check children
    bool some_child_is_closer_to_pub = false;
    //double closest_child_distance = 1 * HOUR;
    //node_id_t new_parent = NO_SUCH_NODE;

    vector<node_id_t> children = feed->get_children();
    for (vector<node_id_t>::iterator itC = children.begin();
      itC != children.end(); itC++) {
  
      node_id_t child = *itC;
      assert(know_foreign_nc(child));

      // inform all peers of coord change -- via a ping, whether it effects
      // the invariant or not (because it affect newly joining nodes who might
      // be caught in a stale state)
      if (find(sent_new_coords.begin(), sent_new_coords.end(), child)
        == sent_new_coords.end()) {

#ifdef DEBUG
        printf("  SENDING updated COORD info to peer: %s\n", child.str().c_str());
#endif // DEBUG
        send_ping_requests(child);
        sent_new_coords.push_back(child);
      }
      
      if (get_foreign_nc(child).get_distance(feed->get_publisher_nc()) <
        _node->get_nc().get_distance(feed->get_publisher_nc())) {

        // One of my children is now closer to the publisher -- oops

        //child_distance = _node->get_nc().get_distance(get_foreign_nc(child));
        //if (!some_child_is_closer_to_pub || child_distance < closest_child_distance) {
        //  closest_child_distance = child_distance;
        //  new_parent = child;
        //}
        some_child_is_closer_to_pub = true;
        //assert(!closer_to_pub_then_parent);

        Role child_role(ROLE_FEED_CHILD, feed_id);
        remove_peer_from_role(child, child_role, false);

        JoinChangeParentData* msg_data  = new JoinChangeParentData;
        msg_data->sender_coord = _node->get_nc();

        msg_data->feed_id = feed_id;
        msg_data->publisher_nc = feed->get_publisher_nc();
        if (feed->has_parent()) {
          msg_data->new_parent = feed->get_parent(); 
        } else {
          msg_data->new_parent = feed->get_publisher(); 
        }

        Message msg2;
        msg2.src = _node->get_node_id();
        msg2.dst = child;
        msg2.layer = LYR_APPLICATION;
        msg2.flag = MSG_RAPPEL_FEED_CHANGE_PARENT;
        msg2.data = msg_data;

        // delay change parent directive -- wait until the parent child gets the
        // ping first
        _transport->send_with_delay(msg2, globops.rappel_reply_timeout / 2);

#ifndef IGNORE_ETIENNE_STATS
        _curr_cycle_stats.msgs_out_change_parent++;
#endif // IGNORE_ETIENNE_STATS
      }

      //if (some_child_is_farther) {
      //  assert(new_parent != NO_SUCH_NODE);
      //}
    } // end for: all children
  } // end for: all feeds
}
*/
