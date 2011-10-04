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
#include "node.h"
#include "feed.h"
#include "data_objects.h"

using namespace std;

// initialize object
Feed::Feed(const node_id_t& owner, const feed_id_t& id) {

  _rng = RNG::get_instance();
  assert(_rng != NULL);
  
  _timer = Timer::get_instance();
  assert(_timer != NULL);
  
  _transport = Transport::get_instance();
  assert(_transport != NULL);

  assert(globops.is_initialized());

  _owner = owner;
  _id = id;
  _latest_update_seq = 0;
  _highest_update_seq = 0;

  // parentage
  _parent = NO_SUCH_NODE;
  _grand_parent = NO_SUCH_NODE;
  _seeking_parent = false;
  _path_cost = 0.0;

  _ancestors.push_back((node_id_t) _id);

  // scheule a rejoin
  _rejoin_event_id = NO_SUCH_EVENT_ID;
#if COMPILE_FOR == NETWORK
  _rtt_event_id = NO_SUCH_EVENT_ID;
  schedule_rtt_to_publisher(true);
#endif // COMPILE_FOR == NETWORK
}

// special instance of feed (created for feed publisher)
Feed::Feed(const feed_id_t& id, const Coordinate& publisher_nc) {

  _rng = RNG::get_instance();
  assert(_rng != NULL);
  
  _timer = Timer::get_instance();
  assert(_timer != NULL);
  
  _transport = Transport::get_instance();
  assert(_transport != NULL);

  assert(globops.is_initialized());

  _owner = (node_id_t) id;
  _id = id;
  _latest_update_seq = 0;
  _highest_update_seq = 0;

  // parentage
  _parent = (node_id_t) id;
  _grand_parent = NO_SUCH_NODE;
  _seeking_parent = false;
  _path_cost = 0.0;

  // publisher info (own, in this case)
  //_publisher_nc_set = true;
  _publisher_nc = publisher_nc;

  _rejoin_event_id = NO_SUCH_EVENT_ID;
}

#if COMPILE_FOR == NETWORK
Clock Feed::get_route_rtt() const {

  if (_route_rtts.size() == 0) {
   return -7 * DAYS;
  }
  assert(_route_rtts.size() <= globops.rappel_feed_rtt_samples);

  vector<Clock> delay_msmnts = _route_rtts;    
  sort(delay_msmnts.begin(), delay_msmnts.end());
  Clock median_rtt = *(delay_msmnts.begin() + delay_msmnts.size() / 2);
  return median_rtt;
}

void Feed::add_sample_rtt(const Clock& rtt) {

  assert(_route_rtts.size() <= globops.rappel_feed_rtt_samples);
  if (_route_rtts.size() == globops.rappel_feed_rtt_samples) {
    _route_rtts.erase(_route_rtts.begin());
  }

  _route_rtts.push_back(rtt);
}

unsigned int Feed::send_rtt_to_publisher() {

  // now send the rtt-based sync req
  VivaldiSyncReqData* req_data = new VivaldiSyncReqData;
  assert(req_data != NULL);

  req_data->request_sent_at = _timer->get_time();
  req_data->wait_for_timer = NO_SUCH_EVENT_ID;
  req_data->for_publisher_rtt = true;

  // prepare message
  Message msg;
  msg.src = _owner;
  msg.dst = (node_id_t) _id;
  msg.layer = LYR_APPLICATION;
  msg.flag = MSG_VIVALDI_SYNC_REQ;
  msg.data = req_data;
  
  // send
  _transport->send(msg);

  _rtt_event_id = NO_SUCH_EVENT_ID;
  return schedule_rtt_to_publisher(false);
}

unsigned int Feed::schedule_rtt_to_publisher(bool initial) {

  PeriodicRTTSampleEventData* evt_data = new PeriodicRTTSampleEventData;
  evt_data->feed_id = _id;

  Event event;
  event.node_id = _owner;
  event.layer = LYR_APPLICATION;
  event.flag = EVT_RAPPEL_PERIODIC_FEED_RTT_REQ;
  event.data = evt_data;
  assert(globops.is_initialized());

  event.whence = initial ?
    _rng->rand_ulonglong(globops.rappel_feed_rtt_interval) :
    globops.rappel_feed_rtt_interval;

  _rtt_event_id = _timer->schedule_event(event);
  return 0;
}

#endif // COMPILE_FOR == NETWORK

void Feed::schedule_rejoin(bool initial_rejoin) {

  assert(_rejoin_event_id == NO_SUCH_EVENT_ID);

  // only non-owned feeds have a schedule rejoin
  if (_owner != (node_id_t) _id) {

    // schedule a periodic rejoin
    PeriodicRejoinEventData* data = new PeriodicRejoinEventData;
    data->feed_id = _id;
  
    Event event;
    event.node_id = _owner;
    event.layer = LYR_APPLICATION;
    event.flag = EVT_RAPPEL_PERIODIC_FEED_REJOIN;
    event.data = data;
    assert(globops.is_initialized());

    if (initial_rejoin) {
      event.whence = _rng->rand_ulonglong(globops.rappel_feed_rejoin_interval);
    } else {

      // rejoin within a 2 minute window
      assert(globops.rappel_feed_rejoin_interval > 1 * MINUTE);
      event.whence = globops.rappel_feed_rejoin_interval - 1 * MINUTE
        + _rng->rand_ulonglong(2 * MINUTE);
    }

    _rejoin_event_id = _timer->schedule_event(event);
  }
}

void Feed::cancel_events() {

  // only non-owned feeds have a schedule rejoin
  if (_owner != (node_id_t) _id) {
    _timer->cancel_event(_rejoin_event_id);
    _rejoin_event_id = NO_SUCH_EVENT_ID;
  }

  // NOTE: very hackish -- a list of pulled entries is kept
  for (set<event_id_t>::const_iterator it = _pull_entry_events.begin();
    it != _pull_entry_events.end(); it++ ) {

    _timer->cancel_event(*it);
  }
  _pull_entry_events.clear();


#if COMPILE_FOR == NETWORK
  if (_rtt_event_id != NO_SUCH_EVENT_ID) {
    _timer->cancel_event(_rtt_event_id);
    _rtt_event_id = NO_SUCH_EVENT_ID;
  }
#endif // COMPILE_FOR == NETWORK
}

void Feed::reschedule_rejoin(bool initial_rejoin) {

  if (_rejoin_event_id != NO_SUCH_EVENT_ID) {
    _timer->cancel_event(_rejoin_event_id);
    _rejoin_event_id = NO_SUCH_EVENT_ID;
  }
  schedule_rejoin(initial_rejoin);
  assert(_rejoin_event_id != NO_SUCH_EVENT_ID);
}

bool Feed::is_parent(const node_id_t& node_id) const {
  return (_parent == node_id);
}

bool Feed::is_child(const node_id_t& node_id) const {

  vector<node_id_t>::const_iterator result = find(_children.begin(), 
    _children.end(), node_id);

  return (result != _children.end());
}

void Feed::add_entry(unsigned int seq_num, const std::string& msg,
  const Clock& emission_time ) {

  // add only if missing
  if (_cache.find(seq_num) == _cache.end()) {

    // add entry to cache
    EntryDetails details;
    details.msg = msg;
    details.sync_emission_time = emission_time;

    _cache[seq_num] = details;

    // remove from missing entires
    if (_missing_entries.find(seq_num) != _missing_entries.end()) {
      _missing_entries.erase(seq_num);
    }

    // increment highest update seq, if incoming seq_num is higher
    _highest_update_seq = (seq_num > _highest_update_seq) ?
      seq_num : _highest_update_seq;

    // find the earliest contiguous update in cache
    const int MAGIC_NUM = -100; // definitely non existant
    int earliest_contiguous_update = MAGIC_NUM;

    // increment _latest_update_seq
    if (seq_num > _latest_update_seq) { // to safeguard against a non last-X update (rare)

      bool last_x_updates_in_cache = true;

      unsigned int start_seq = (seq_num >= globops.rappel_feed_updates_cache)
        ? seq_num - globops.rappel_feed_updates_cache + 1 : 1;

      for (unsigned int update_num = start_seq; update_num <= seq_num; update_num++) {

        if (_cache.find(update_num) == _cache.end()) {
          last_x_updates_in_cache = false;
          // NOTE: we don't break here because we want to find the earliest_contiguous_update
          // to handle the special case described below
        } else if ( /* update in cache, and */ earliest_contiguous_update == -1 ) {
          earliest_contiguous_update = update_num;
        }
      }

      if (last_x_updates_in_cache) {
        _latest_update_seq = seq_num;
      }
    }

    // TODO: I believe the following special case scenario is not covered: node gets updates
    // upto x, goes offline. comes back online. the latest known update is x +
    // y, where y > cache_size. so fetches the last cache_size updates. they
    // come. but out of order. the _latest_update_seq is not incremented until
    // x + y + 1 update is published/received.

    // March 14, 2007: Attempt #1 to handle the special case described above
    if (_latest_update_seq < seq_num) {

      bool special_case = true;

      for( unsigned int i = 0; i < globops.rappel_feed_updates_cache; i++) {
        if (_cache.find(earliest_contiguous_update) == _cache.end()) {
          special_case = false;
          break;
        }
      }

      if (special_case) {
        _latest_update_seq = earliest_contiguous_update
          + globops.rappel_feed_updates_cache;
      }
    }

    // on the off chance that the new entry was a missing entry
    while (true) {
      if (_cache.find(_latest_update_seq + 1) != _cache.end()) {
        _latest_update_seq++;
      } else {
        break;
      }
    }

    assert(_latest_update_seq <= _highest_update_seq);
  }
}

unsigned int Feed::request_missing_entries(unsigned int seq_num, const node_id_t& src) {

  assert(src != NO_SUCH_NODE);

  unsigned int entries_pulled = 0;

  // increment highest update seq, if incoming seq_num is higher
  _highest_update_seq = (seq_num > _highest_update_seq) ?
    seq_num : _highest_update_seq;

  // no need to pull from a node that knows less that me (i.e., pulling older than X updates)
  // This is a rare case. This happens. It happened for one of the test cases. An assert
  // was triggered because we tried to pull a request from a node that didn't have that
  // update.
  if (_latest_update_seq > seq_num) {
    return entries_pulled;
  }

  // fetch the last X updates (highest being seq_num)
  unsigned int start_seq = (seq_num >= globops.rappel_feed_updates_cache)
    ? seq_num - globops.rappel_feed_updates_cache + 1 : 1;

  // fetch only the highest X updates (if missing)
  for (unsigned int update_num = start_seq; update_num <= seq_num; update_num++) {

    // pull entry only if missing (and only if its not being fetched currently)
    if (_cache.find(update_num) == _cache.end() &&
      _missing_entries.find(update_num) == _missing_entries.end()) {

      _missing_entries[update_num] = src;
      pull_entry(update_num, src);
      entries_pulled++;
    }
  }

  return entries_pulled;
}

void Feed::mark_pull_as_failed(unsigned int seq_num, const node_id_t& old_src) {

  // add entry if only truly missing
  if (_cache.find(seq_num) == _cache.end()) {

    // mark the entry as no longer being fetched
    assert(_missing_entries.find(seq_num) != _missing_entries.end());
    assert(_missing_entries[seq_num] == old_src);
    _missing_entries.erase(seq_num);
  }

  assert(_missing_entries.find(seq_num) == _missing_entries.end());
}

unsigned int Feed::send_entry_to_children(unsigned int seq_num) {

  assert(seq_num <= _highest_update_seq);
  assert(_cache.find(seq_num) != _cache.end());

  // send the message to children (in the same universe)
  for (unsigned int i = 0; i < _children.size(); i++) {
    send_entry(seq_num, _children[i], false);
  }

  return _children.size();
}

void Feed::send_entry(unsigned int seq_num, const node_id_t& node_id, bool pulled) {

  //assert(seq_num <= _latest_update_seq);
  assert(_cache.find(seq_num) != _cache.end());

  // send an update notification
  FeedUpdateData* send_data = new FeedUpdateData;
  send_data->feed_id = _id;
  send_data->publisher_nc = _publisher_nc;
  send_data->seq_num = seq_num;
  send_data->msg = _cache[seq_num].msg;
  send_data->sync_emission_time = _cache[seq_num].sync_emission_time;
  send_data->pulled = pulled;

  Message msg;
  msg.layer = LYR_APPLICATION;
  msg.flag = MSG_RAPPEL_FEED_UPDATE;
  msg.src = _owner;
  msg.dst = node_id;
  msg.data = send_data;

  _transport->send(msg);
}

void Feed::pull_entry(unsigned seq_num, const node_id_t& src) {

  assert(seq_num > _latest_update_seq);
  assert(_cache.find(seq_num) == _cache.end());

  // pull an update
  FeedUpdatePullData* req_data = new FeedUpdatePullData;
  req_data->feed_id = _id;
  req_data->seq_num = seq_num;

  Message msg;
  msg.layer = LYR_APPLICATION;
  msg.flag = MSG_RAPPEL_FEED_UPDATE_PULL_REQ;
  msg.src = _owner;
  msg.dst = src; 
  msg.data = req_data;

  _transport->send(msg);
  WaitAfterUpdatePullData* evt_data = new WaitAfterUpdatePullData;
  evt_data->feed_id = _id;
  evt_data->seq_num = seq_num;
  evt_data->neighbor = src;

  Event event;
  event.node_id = _owner;
  event.layer = LYR_APPLICATION;
  event.flag = EVT_RAPPEL_WAIT_AFTER_FEED_UPDATE_PULL_REQ;
  event.data = evt_data;
  assert(globops.is_initialized());
  event.whence = globops.rappel_reply_timeout;

  event_id_t event_id = _timer->schedule_event(event);
  _pull_entry_events.insert(event_id);
}
  
void Feed::set_parent(const node_id_t& parent) {
  assert(parent != NO_SUCH_NODE);
  bool initial_rejoin = (_parent == NO_SUCH_NODE);
  _parent = parent;
  reschedule_rejoin(initial_rejoin);
  assert(_rejoin_event_id != NO_SUCH_EVENT_ID);
}

void Feed::set_grand_parent(const node_id_t& grand_parent) {
  assert(grand_parent != NO_SUCH_NODE);
  _grand_parent = grand_parent;
}

void Feed::set_ancestors(const vector<node_id_t>& ancestors) {
  assert(ancestors.size() > 0);
  assert(ancestors[0] == get_publisher());
  assert(ancestors[ancestors.size() - 1] == get_parent());
  assert(ancestors.size() < 100); // some very high number

  // just to make sure there are no cycles
  for (unsigned int i = 0; i < ancestors.size(); i++) {
    assert (ancestors[i] != _owner);
  }

  _ancestors = ancestors;
}

void Feed::clear_ancestors() {
  _ancestors.clear();
}

void Feed::unset_parent() {
  assert(_parent != NO_SUCH_NODE);
  _parent = NO_SUCH_NODE;
  _grand_parent = NO_SUCH_NODE;
}

void Feed::add_child(const node_id_t& child) {

  assert(_children.size() <= globops.rappel_num_feed_children);
  assert(is_child(child) == false);
  
  _children.push_back(child);
}

void Feed::remove_child(const node_id_t& child) {

  assert(is_child(child));

  vector<node_id_t>::iterator result = find(_children.begin(), 
    _children.end(), child);

  _children.erase(result);
}
