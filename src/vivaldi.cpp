#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include "rappel_common.h"
#include "timer.h"
#include "transport.h"
#include "data_objects.h"
#include "debug.h"
#include "vivaldi.h"

// initialize object
Vivaldi::Vivaldi(Node* node) {

  assert(node != NULL);
  assert(globops.is_initialized());

  _timer = Timer::get_instance();
  assert(_timer != NULL);
  
  _transport = Transport::get_instance();
  assert(_transport != NULL);

  _rng = RNG::get_instance();
  assert(_rng != NULL);

  _node = node;
/*
  _bootstrap_check_event_id = NO_SUCH_EVENT_ID;
*/
  _periodic_ping_event_id = NO_SUCH_EVENT_ID;
  _wait_for_ping_reply = NO_SUCH_EVENT_ID;
#if COMPILE_FOR == NETWORK
  _periodic_sync_event_id = NO_SUCH_EVENT_ID;
  _wait_for_sync_reply = NO_SUCH_EVENT_ID;
#endif // COMPILE_FOR == NETWORK
  _inactive = false;
  _ping_in_progress = false;
  _sync_in_progress = false;
}

Vivaldi::~Vivaldi() {
  // TODO: unallocate memory
  debug(FINAL_SEQ_NUM); // magic number
}

int Vivaldi::event_callback(Event& event) {

  assert(event.layer == LYR_APPLICATION);

  switch (event.flag) {

/*
  case EVT_VIVALDI_NC_BOOTSTRAP_PERIOD_EXPIRED: {
    return timer_vivaldi_bootstrap_period_expired(event);
  }
*/

  case EVT_VIVALDI_PERIODIC_PING: {
    return timer_periodic_ping(event);
  }

  case EVT_VIVALDI_WAIT_AFTER_PING_REQ: {
    return timer_wait_after_ping_req(event);
  }

#if COMPILE_FOR == NETWORK
  case EVT_VIVALDI_PERIODIC_SYNC: {
    return timer_periodic_sync(event);
  }

  case EVT_VIVALDI_WAIT_AFTER_SYNC_REQ: {
    return timer_wait_after_sync_req(event);
  }
#endif // COMPILE_FOR == NETWORK

  default: {
    assert(false);
  }

  }

  return 0;
}

int Vivaldi::msg_callback(Message& msg) {
  
  assert(msg.layer == LYR_APPLICATION);

  switch (msg.flag) {
  
  // a ping request can be from either a landmark server or a regular rappel node  
  case MSG_VIVALDI_PING_REQ: {
    return receive_ping_req(msg);
  }
    
  case MSG_VIVALDI_PING_REPLY: {
    return receive_ping_reply(msg);
  }

#if COMPILE_FOR == NETWORK
  case MSG_VIVALDI_SYNC_REQ: {
    return receive_sync_req(msg);
  }
    
  case MSG_VIVALDI_SYNC_REPLY: {
    return receive_sync_reply(msg);
  }
#endif // COMPILE_FOR == NETWORK

  default: {
    assert(false);
  }

  }

  return 0;
}

void Vivaldi::debug(unsigned int seq_num) {

#if COMPILE_FOR == NETWORK
  std::vector<std::pair<node_id_t, Clock> > landmarks = _node->get_all_landmarks();

  std::string file_name = std::string("landmarks." + globops.self_name + ".data");
  FILE* file = fopen(file_name.c_str(), "w");

  if (file == NULL) {
    std::cerr << "error: couldn't create current landmark data file: "
      << file_name << std::endl;
    return;
  }

  fprintf(file, "# debug_seq=%u curr_time=%lld\n", seq_num, _timer->get_time());
  fprintf(file, "# %-43s %-10s %-10s %-10s %s\n",
    "landmark_node", "age", "estimated", "min-sample", "coordinate");

  for (std::vector<std::pair<node_id_t, Clock> >::const_iterator
    it = landmarks.begin(); it != landmarks.end(); it++) {

    node_id_t landmark = it->first;
    Clock last_heard_ago = it->second;

    Coordinate other_coord = _node->get_landmark_coordinate(landmark);
    double distance = -1;
    if (other_coord.get_version() > 0) {
      distance = _node->get_nc().get_distance(other_coord);
    }

    fprintf(file, "%-45s %-10s %-10.5f %-10.5f %s\n", landmark.str().c_str(),
      _timer->get_time_str(last_heard_ago).c_str(), distance,
      _node->get_filtered_landmark_measurement(landmark, true),
      other_coord.to_string().c_str());
  }
  fclose(file); // don't be rude
#endif // COMPILE_FOR == NETWORK
}

/**
 * Nodes running the Vivaldi server (i.e., landmark nodes) send one periodic ping
 * to a randomly selected landmark node every period.
 */
void Vivaldi::bring_online() {

  _num_ping_replies = 0;
  _ping_in_progress = false;

  // NOTE: July12,07: Why does a vivaldi node always remain in bootstrap mode?
  // i.e., always changes coordinates? Because once node goes out of bootstrap
  // mode it does not send out periodic pings.

  if (!_node->is_a_vivaldi_node()) {
/*
    // first, create the timer
    BootstrapPeriodExpiredEventData* evt_data = new BootstrapPeriodExpiredEventData;

    Event event;
    event.node_id = _node->get_node_id();
    event.layer = LYR_APPLICATION;
    event.flag = EVT_VIVALDI_NC_BOOTSTRAP_PERIOD_EXPIRED;
    event.data = evt_data;
    event.whence = globops.bootstrap_vivaldi_timeout;

    // schedule the check
    _bootstrap_check_event_id = _timer->schedule_event(event);
*/

#if COMPILE_FOR == NETWORK
    // sync the time
    schedule_periodic_sync(true);
#endif // COMPILE_FOR == NETWORK
  }

  // the vivadi component sends a periodic ping to a landmark
#if COMPILE_FOR == NETWORK
  if (globops.self_name != "sync_server") {
#endif // COMPILE_FOR == NETWORK
    schedule_periodic_ping(true);
    // send the first ping now
    send_ping();
#if COMPILE_FOR == NETWORK
  }
#endif // COMPILE_FOR == NETWORK
}

void Vivaldi::take_offline() {

  if (_periodic_ping_event_id != NO_SUCH_EVENT_ID) {
    _timer->cancel_event(_periodic_ping_event_id);
    _periodic_ping_event_id = NO_SUCH_EVENT_ID;
  }

#if COMPILE_FOR == NETWORK
  if (_periodic_sync_event_id != NO_SUCH_EVENT_ID) {
    _timer->cancel_event(_periodic_sync_event_id);
    _periodic_sync_event_id = NO_SUCH_EVENT_ID;
  }
#endif // COMPILE_FOR == NETWORK

  if (_wait_for_ping_reply != NO_SUCH_EVENT_ID) {
    _timer->cancel_event(_wait_for_ping_reply);
    _wait_for_ping_reply = NO_SUCH_EVENT_ID;
  }

/*
  // cancel bootstrap check event id, if pending
  if (_bootstrap_check_event_id != NO_SUCH_EVENT_ID) {
    assert(!_node->is_a_vivaldi_node());
    _timer->cancel_event(_bootstrap_check_event_id);
    _bootstrap_check_event_id = NO_SUCH_EVENT_ID;
  }
*/
}

void Vivaldi::deactivate() {

  assert(!_inactive);

  _inactive = true;

  assert(_periodic_ping_event_id != NO_SUCH_EVENT_ID);
  _timer->cancel_event(_periodic_ping_event_id);
  _periodic_ping_event_id = NO_SUCH_EVENT_ID;

  if (_wait_for_ping_reply != NO_SUCH_EVENT_ID) {
    _timer->cancel_event(_wait_for_ping_reply);
    _wait_for_ping_reply = NO_SUCH_EVENT_ID;
  }
}

void Vivaldi::reactivate() {

  assert(_inactive);

  _inactive = false;

  // the vivadi component sends a periodic ping to a landmark
  schedule_periodic_ping(true);
}

////////////////////// callback implementations
/*
int Vivaldi::timer_vivaldi_bootstrap_period_expired(Event& event) {

  BootstrapPeriodExpiredEventData* evt_data = 
    dynamic_cast<BootstrapPeriodExpiredEventData*>(event.data);
  assert(evt_data != NULL);

  // delete data (no data anyway)
  delete(evt_data);

  // bootstrapping check is over
  _bootstrap_check_event_id = NO_SUCH_EVENT_ID;

  if (_num_ping_replies >= globops.bootstrap_vivaldi_pongs_min
#if COMPILE_FOR == NETWORK
    && _sync_offsets.size() >= globops.bootstrap_sync_min
#endif // COMPILE_FOR == NETWORK
    ) {

    // check that the node has already been bootstrapped
    assert(_node->is_bootstrapped());
    return 0;
  }

  // node should not be bootstrapped
  assert(!_node->is_bootstrapped());

  //_node->signal_vivaldi_bootstrap_complete();
  // first, create the timer
  BootstrapPeriodExpiredEventData* evt_data2 = new BootstrapPeriodExpiredEventData;

  Event event2;
  event2.node_id = _node->get_node_id();
  event2.layer = LYR_APPLICATION;
  event2.flag = EVT_VIVALDI_NC_BOOTSTRAP_PERIOD_EXPIRED;
  event2.data = evt_data2;
  event2.whence = globops.bootstrap_vivaldi_timeout;

  // schedule the check
  _bootstrap_check_event_id = _timer->schedule_event(event2);

  return 0;
}
*/

int Vivaldi::timer_periodic_ping(Event& event) {

  PeriodicVivaldiPingEventData* evt_data
    = dynamic_cast<PeriodicVivaldiPingEventData*>(event.data);
  assert(evt_data != NULL);
  
  // schedule the next periodic ping
  schedule_periodic_ping(false);

  // send a ping for this period
  send_ping();
 
  // free memory
  delete(evt_data);
  return 0;
}

void Vivaldi::send_ping() {

  assert(!_inactive);

  // get out if there is already another ping in progress
  if (_ping_in_progress) {
    return;
  }

  // find lardmark target -- if none known, get out
  node_id_t target = _node->get_random_landmark();
  if (target == NO_SUCH_NODE) {
    //std::cout << "don't know any landmarks" << std::endl;
    return;
  }

  _ping_in_progress = true;

  // first, create the timer for fault handler
  WaitAfterVivaldiPingReqEventData* wait_evt_data
    = new WaitAfterVivaldiPingReqEventData;
  assert(wait_evt_data != NULL);

  wait_evt_data->target = target;

  Event event;
  event.node_id = _node->get_node_id();
  event.layer = LYR_APPLICATION;
  event.flag = EVT_VIVALDI_WAIT_AFTER_PING_REQ;
  event.data = wait_evt_data;
  event.whence = globops.vivaldi_reply_timeout;
  event_id_t event_id = _timer->schedule_event(event);

  assert(_wait_for_ping_reply == NO_SUCH_EVENT_ID);
  _wait_for_ping_reply = event_id;

  // now send the ping
  VivaldiPingReqData* req_data = new VivaldiPingReqData;
  assert(req_data != NULL);

  req_data->request_sent_at = _timer->get_time();
  req_data->wait_for_timer = event_id;
  req_data->is_vivaldi_landmark = _node->is_a_vivaldi_node();

  // prepare PING REQ message
  Message msg;
  msg.src = _node->get_node_id();
  msg.dst = target;
  msg.layer = LYR_APPLICATION;
  msg.flag = MSG_VIVALDI_PING_REQ;
  msg.data = req_data;
  
  // send
  _transport->send(msg);
}

int Vivaldi::timer_wait_after_ping_req(Event& evt) {

  WaitAfterVivaldiPingReqEventData* evt_data
    = dynamic_cast<WaitAfterVivaldiPingReqEventData *>(evt.data);

  _wait_for_ping_reply = NO_SUCH_EVENT_ID; // no scheduled ping
  _ping_in_progress = false; // the previous ping failed

  // send another ping request
  if (_num_ping_replies < globops.bootstrap_vivaldi_pongs_min) {
    send_ping(); 
  }

  // free memory
  delete(evt_data);
  return 0;
}

int Vivaldi::receive_ping_req(Message& recv_msg) {

  VivaldiPingReqData* req_data = dynamic_cast<VivaldiPingReqData*>(recv_msg.data);
  assert(req_data != NULL);

  // do not send anything else than the reply
  VivaldiPingReplyData* reply_data = new VivaldiPingReplyData;
  assert(reply_data != NULL);

  // add node to landmark list, if this is a vivaldi landmark node
  if (req_data->is_vivaldi_landmark) {
    _node->add_landmark(recv_msg.src);
  }

  reply_data->sender_coord = _node->get_nc();
  reply_data->wait_for_timer = req_data->wait_for_timer;
  reply_data->request_sent_at = req_data->request_sent_at;
  reply_data->sample_landmarks = _node->get_sample_landmarks(3); // TODO: variablize

  // send a reply
  Message msg;
  msg.src = recv_msg.dst;
  msg.dst = recv_msg.src;
  msg.layer = LYR_APPLICATION;
  msg.flag = MSG_VIVALDI_PING_REPLY;
  msg.data = reply_data;
  
  // send
  _transport->send(msg);

  // delete req_data
  delete(req_data);
  return 0;
}

// receive a ping reply
int Vivaldi::receive_ping_reply(Message& recv_msg) {

  VivaldiPingReplyData* reply_data = dynamic_cast<VivaldiPingReplyData*>(recv_msg.data) ;
  assert(reply_data != NULL);

  // delete the wait for timer 
  if (_timer->cancel_event(reply_data->wait_for_timer) == false) {

#ifdef DEBUG
    Clock delay = _timer->get_time() - reply_data->request_sent_at;
    // cancel failed -- the fault handler for this event has already been executed
    if (delay > globops.vivaldi_reply_timeout) {
      std::cerr << "warn: timeout: ping_reply from: " << recv_msg.src.str()
        << "; reply took: " << _timer->get_time_str(delay) << " seconds" << std::endl;
    }
#endif // DEBUG

    delete(reply_data);
    return 0;
  }

  assert(_wait_for_ping_reply == reply_data->wait_for_timer);
  _wait_for_ping_reply = NO_SUCH_EVENT_ID;

  ++_num_ping_replies;
  _ping_in_progress = false; // finished with the ping previously in progress

  _node->add_landmark(recv_msg.src);
  _node->add_landmarks(reply_data->sample_landmarks);

  // update the coordinates
  if (_node->is_a_vivaldi_node() || !_node->is_rappel_online()) {

    //convert latency to milliseconds
    double ow_latency = (_timer->get_time() - reply_data->request_sent_at) 
      / (double) (2 * MILLI_SECONDS);

#ifdef DEBUG
    printf("current sample; ow_latency=%0.3f\n", ow_latency);
#endif // DEBUG

    // add measurement to sample set
    _node->add_landmark_measurement(recv_msg.src, ow_latency,
      reply_data->sender_coord);

    // update coords with filtered sample
    _node->update_local_nc(reply_data->sender_coord,
      _node->get_filtered_landmark_measurement(recv_msg.src, false));
  }

  // signal that the nc has been bootstrapped, if not already
  if (_num_ping_replies >= globops.bootstrap_vivaldi_pongs_min &&
#if COMPILE_FOR == NETWORK
    _sync_offsets.size() >= globops.bootstrap_sync_min &&
#endif // COMPILE_FOR == NETWORK
    !_node->is_a_vivaldi_node() && !_node->is_bootstrapped()) {

    // finish bootstrapping
    _node->signal_vivaldi_bootstrap_complete();

/*
    // cancel bootstrap timeout
    assert(_bootstrap_check_event_id != NO_SUCH_EVENT_ID);
    _timer->cancel_event(_bootstrap_check_event_id);
    _bootstrap_check_event_id = NO_SUCH_EVENT_ID;
*/
  }

  // continuously send the first X pings
  if (_num_ping_replies < globops.bootstrap_vivaldi_pongs_min) {

    // send another ping request
    send_ping();
  }

  // free memory
  delete (reply_data);
  return 0;
}

void Vivaldi::schedule_periodic_ping(bool initial) {

  PeriodicVivaldiPingEventData* evt_data = new PeriodicVivaldiPingEventData;
  assert(evt_data != NULL);

  Event event;
  event.node_id = _node->get_node_id();
  event.layer = LYR_APPLICATION;
  event.flag = EVT_VIVALDI_PERIODIC_PING;
  event.data = evt_data;

  event.whence = initial ?
    _rng->rand_ulonglong(globops.vivaldi_ping_interval) :
    globops.vivaldi_ping_interval;

  _periodic_ping_event_id = _timer->schedule_event(event);
}

#if COMPILE_FOR == NETWORK
int Vivaldi::timer_periodic_sync(Event& event) {

  PeriodicVivaldiSyncEventData* evt_data
    = dynamic_cast<PeriodicVivaldiSyncEventData*>(event.data);
  assert(evt_data != NULL);
  
  // schedule the next periodic ping
  schedule_periodic_sync(false);

  // send a sync for this period
  send_sync_req(false);

  // free memory
  delete(evt_data);
  return 0;
}

void Vivaldi::send_sync_req(bool resend_on_failure) {

  //assert(!_inactive); # we'll be sending these forever -- to adjust for any time changes
  if (_sync_in_progress) {
    return;
  }

  // this node shouldn't be the sync node itself
  assert(globops.sync_node != _node->get_node_id());

  _sync_in_progress = true;

  // first, create the timer for fault handler
  WaitAfterVivaldiSyncReqEventData* wait_evt_data
    = new WaitAfterVivaldiSyncReqEventData;
  assert(wait_evt_data != NULL);
  wait_evt_data->resend_on_failure = resend_on_failure;

  Event event;
  event.node_id = _node->get_node_id();
  event.layer = LYR_APPLICATION;
  event.flag = EVT_VIVALDI_WAIT_AFTER_SYNC_REQ;
  event.data = wait_evt_data;
  event.whence = globops.vivaldi_reply_timeout;
  event_id_t event_id = _timer->schedule_event(event);

  assert(_wait_for_sync_reply == NO_SUCH_EVENT_ID);
  _wait_for_sync_reply = event_id;

  // now send the ping
  VivaldiSyncReqData* req_data = new VivaldiSyncReqData;
  assert(req_data != NULL);

  req_data->request_sent_at = _timer->get_time();
  req_data->wait_for_timer = event_id;
  req_data->for_publisher_rtt = false;

  // prepare PING REQ message
  Message msg;
  msg.src = _node->get_node_id();
  msg.dst = globops.sync_node;
  msg.layer = LYR_APPLICATION;
  msg.flag = MSG_VIVALDI_SYNC_REQ;
  msg.data = req_data;
  
  // send
  _transport->send(msg);
}

int Vivaldi::timer_wait_after_sync_req(Event& evt) {

  WaitAfterVivaldiSyncReqEventData* evt_data
    = dynamic_cast<WaitAfterVivaldiSyncReqEventData *>(evt.data);

  _wait_for_sync_reply = NO_SUCH_EVENT_ID; // no scheduled ping
  _sync_in_progress = false; // the previous ping failed

  if (evt_data->resend_on_failure) {
    send_sync_req(true);
  }

  // free memory
  delete(evt_data);
  return 0;
}

// answer a ping request
// answer a sync request
int Vivaldi::receive_sync_req(Message& recv_msg) {

  VivaldiSyncReqData* req_data = dynamic_cast<VivaldiSyncReqData*>(recv_msg.data);
  assert(req_data != NULL);

  // do not send anything else than the reply
  VivaldiSyncReplyData* reply_data = new VivaldiSyncReplyData;
  assert(reply_data != NULL);

  //reply_data->sender_coord = _node->get_nc();
  reply_data->wait_for_timer = req_data->wait_for_timer;
  reply_data->request_sent_at = req_data->request_sent_at;
  reply_data->reply_sent_at = _timer->get_time();
  if (req_data->for_publisher_rtt) {
    assert(_node->rappel_is_publisher());
  }
  reply_data->for_publisher_rtt = req_data->for_publisher_rtt;

  // send a reply
  Message msg;
  msg.src = recv_msg.dst;
  msg.dst = recv_msg.src;
  msg.layer = LYR_APPLICATION;
  msg.flag = MSG_VIVALDI_SYNC_REPLY;
  msg.data = reply_data;
  
  // send
  _transport->send(msg);

  // delete req_data
  delete(req_data);
  return 0;
}

// receive a ping reply
int Vivaldi::receive_sync_reply(Message& recv_msg) {

  VivaldiSyncReplyData* reply_data = dynamic_cast<VivaldiSyncReplyData*>(recv_msg.data) ;
  assert(reply_data != NULL);

  // delete the wait for timer 
  if (reply_data->wait_for_timer != NO_SUCH_EVENT_ID
    && _timer->cancel_event(reply_data->wait_for_timer) == false) {

    Clock delay = _timer->get_time() - reply_data->request_sent_at;

#ifdef DEBUG
    // cancel failed -- the fault handler for this event has already been executed
    if (delay > globops.vivaldi_reply_timeout) {
      std::cerr << "warn: timeout: sync_reply from: " << recv_msg.src.str()
        << "; reply took: " << _timer->get_time_str(delay) << " seconds" << std::endl;
    }
#endif // DEBUG

    return 0;
  }

  Clock curr_time = _timer->get_time();

  if (reply_data->for_publisher_rtt) { 

    // TODO: this should be true, as long as remove_feed() is not implemented
    assert(_node->rappel_is_subscribed_to((feed_id_t) recv_msg.src));
    _node->rappel_add_feed_rtt_sample((feed_id_t) recv_msg.src, curr_time
      - reply_data->request_sent_at);
	  
  } else {

    assert(_wait_for_sync_reply == reply_data->wait_for_timer);
    _wait_for_sync_reply = NO_SUCH_EVENT_ID;

    _sync_in_progress = false;
    Clock local_midpoint = reply_data->request_sent_at
      + (curr_time - reply_data->request_sent_at) / 2;
    Clock curr_offset = reply_data->reply_sent_at - local_midpoint;

    std::cout << "SYNC_RESPONSE: request_sent_at="
      << _timer->get_time_str(reply_data->request_sent_at)
      << " reply_recvd_at=" << _timer->get_time_str(curr_time)
      << " median=" << _timer->get_time_str(local_midpoint) << std::endl << "\t"
      << " reply_sent_at=" << _timer->get_time_str(reply_data->reply_sent_at)
      << " offset=" << _timer->get_time_str(curr_offset) << std::endl;

    assert(_sync_offsets.size() <= globops.bootstrap_sync_min);
    if (_sync_offsets.size() == globops.bootstrap_sync_min) {
      _sync_offsets.erase(_sync_offsets.begin());
    }

    // save the current sample offset
    _sync_offsets.push_back(curr_offset);

    // if sample offset is inconsistent with the sync offset, send another sync msg
    Clock curr_sync_offset = _timer->get_sync_offset();
    Clock diff = curr_sync_offset - curr_offset;
    if (curr_sync_offset != DEFAULT_SYNC_OFFSET
      && (diff > DEFAULT_SYNC_OFFSET_CRITICAL_DRIFT
        || diff < -1 * DEFAULT_SYNC_OFFSET_CRITICAL_DRIFT)) {
      // drifting
      std::cerr << "@time=" << _timer->get_time_str(curr_time)
        << ": offset drift critical, sending another sync probe" << std::endl;
      send_sync_req(true);
    }

    // set the time offset
    if (_sync_offsets.size() == globops.bootstrap_sync_min) {
  
      vector<Clock> offsets = _sync_offsets;
      sort(offsets.begin(), offsets.end());
      Clock median_offset = *(offsets.begin() + offsets.size() / 2);
      
      std::cout << "THE NEW MEDIAN TIME SYNC OFFSET IS: " << _timer->get_time_str(median_offset)
        << std::endl;
      _timer->set_sync_offset(median_offset);
    }
    
    // signal that the nc has been bootstrapped, if not already
    if (_num_ping_replies >= globops.bootstrap_vivaldi_pongs_min &&
      _sync_offsets.size() >= globops.bootstrap_sync_min &&
      !_node->is_a_vivaldi_node() && !_node->is_bootstrapped()) {
  
      // finish bootstrapping
      _node->signal_vivaldi_bootstrap_complete();
  
/*
      // cancel bootstrap timeout
      assert(_bootstrap_check_event_id != NO_SUCH_EVENT_ID);
      _timer->cancel_event(_bootstrap_check_event_id);
      _bootstrap_check_event_id = NO_SUCH_EVENT_ID;
*/
    }
  }
  
  // free memory
  delete (reply_data);
  return 0;
}

void Vivaldi::schedule_periodic_sync(bool initial) {

  PeriodicVivaldiSyncEventData* evt_data = new PeriodicVivaldiSyncEventData;
  assert(evt_data != NULL);

  Event event;
  event.node_id = _node->get_node_id();
  event.layer = LYR_APPLICATION;
  event.flag = EVT_VIVALDI_PERIODIC_SYNC;
  event.data = evt_data;

  event.whence = initial ?
    _rng->rand_ulonglong(globops.sync_post_interval) :
    _node->is_bootstrapped() ?
      globops.sync_post_interval : globops.sync_init_interval;

  _periodic_sync_event_id = _timer->schedule_event(event);
}
#endif // COMPILE_FOR == NETWORK
