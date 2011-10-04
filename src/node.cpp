#include "rappel_common.h"
#include "timer.h"
#include "transport.h"
#include "node.h"
#include "vivaldi.h"
#include "rappel.h"
#include "debug.h"

// global variables
static sqlite3_stmt* g_stats_insert_coordinates_stmt;

// constructor
Node::Node(const node_id_t& node_id) {
  
  _timer = Timer::get_instance();
  assert(_timer != NULL);

  _transport = Transport::get_instance();
  assert(_transport != NULL);

  _rng = RNG::get_instance();
  assert(_rng != NULL);

  // initialize class variables
  _node_id = node_id;
  _rappel_online_since = 0;
  _is_rappel_online = false;
  _bootstrap_phase_complete = false;

  // initialize coordinates (to randomness)
  _nc.init();

  _vivaldi = NULL;
  _rappel = NULL;
}

Node::~Node() {

  _vivaldi->take_offline(); // vivaldi taken offline only when node destroyed
  delete _vivaldi;
  _vivaldi = NULL;

  if (_rappel) {

    if ( _is_rappel_online) {
      rappel_take_offline(true);
    }

    delete _rappel;
    _rappel = NULL;
  }
}

/**
 * Redirect event to VIVALDI or RAPPEL
 */
int Node::event_callback(Event& event) {

  assert(event.layer == LYR_APPLICATION);

  if (event.flag >= EVT_VIVALDI_START && event.flag <= EVT_VIVALDI_END) {

    assert(_vivaldi != NULL);
    return _vivaldi->event_callback(event);

  } else if (event.flag >= EVT_RAPPEL_START && event.flag <= EVT_RAPPEL_END) {
	  
    // don't deliver any events if rappel is not online
    if (!_is_rappel_online) {
      std::cerr << "warn: @time=" << _timer->get_time_str() << " node="
        << _node_id.str() << " missed event=" << event_flag_str(event.flag)
        << " [node offline]" << std::endl;
      delete(event.data);
      assert(false);
      return 0;
    }

    assert(_rappel != NULL);
    return _rappel->event_callback(event);
  } 

  assert(false);
  return 0;
}

/**
 * Redirect msg to VIVALDI or RAPPEL
 */
int Node::msg_callback(Message& msg) {

  assert(msg.layer == LYR_APPLICATION);

  if (msg.flag >= MSG_VIVALDI_START && msg.flag <= MSG_VIVALDI_END) {

    assert(_vivaldi != NULL);
    return _vivaldi->msg_callback(msg);

  } else if (msg.flag >= MSG_RAPPEL_START && msg.flag <= MSG_RAPPEL_END) {

    // don't deliver any messages if rappel is not online
    if (!_is_rappel_online) {
      delete(msg.data);
      return 0;
    }

    assert(_rappel != NULL);
    return _rappel->msg_callback(msg);
  }

  delete(msg.data);
  std::cerr << "warn: unknown LYR_APPLICATION message" << std::endl;
  return 0;
}

void Node::debug(unsigned int seq_num) {

  if (_rappel != NULL) {
    ((Rappel*) _rappel)->debug(seq_num);
  } else if (_vivaldi != NULL) { // only debug vivaldi, if it is not a rappel node
    ((Vivaldi*) _vivaldi)->debug(seq_num);
  }
}

std::vector<std::pair<node_id_t, Clock> > Node::get_all_landmarks() const {

  Clock curr_time = _timer->get_time();
  std::vector<std::pair<node_id_t, Clock> > all_landmarks;

  for (std::map<node_id_t, Clock>::const_iterator it = _landmarks.begin();
    it != _landmarks.end(); it++) {

    node_id_t landmark = it->first;
    Clock last_heard_at = it->second;

    all_landmarks.push_back(make_pair(landmark, curr_time - last_heard_at));
  }

  return all_landmarks;
}

std::vector<std::pair<node_id_t, Clock> >
  Node::get_sample_landmarks(unsigned int num_landmarks) const {

  std::vector<std::pair<node_id_t, Clock> > all_landmarks = get_all_landmarks();
  unsigned total_landmarks = all_landmarks.size();
  unsigned min_landmarks = min(total_landmarks, num_landmarks);

  random_shuffle(all_landmarks.begin(), all_landmarks.end());
  std::vector<std::pair<node_id_t, Clock> > sample_landmarks(
    all_landmarks.begin(), all_landmarks.begin() + min_landmarks);

  // remove any old landmark
  for (std::vector<std::pair<node_id_t, Clock> >::iterator
    it = sample_landmarks.begin(); it != sample_landmarks.end(); /* it++ */) {

    node_id_t landmark = it->first;
    Clock last_heard_ago = it->second;

    // TODO: variablize
    if (last_heard_ago > 10 * MINUTES) {
      sample_landmarks.erase(it);
    } else {
      it++;
    }
  }

  return sample_landmarks;
}

Coordinate Node::get_landmark_coordinate(const node_id_t& landmark) const {

  std::map<node_id_t, std::pair<Coordinate, std::vector<double> > >::const_iterator
    it = _landmark_samples.find(landmark);
  assert(it != _landmark_samples.end());

  Coordinate coord = it->second.first;

  return coord;
}

double Node::get_filtered_landmark_measurement(const node_id_t& landmark,
  bool just_probing) const {

  std::map<node_id_t, std::pair<Coordinate, std::vector<double> > >::const_iterator
    it = _landmark_samples.find(landmark);
  assert(it != _landmark_samples.end());

  vector<double> samples = it->second.second;

  if (samples.size() == 0) {
    if (just_probing) {
      return -1;
    }
    assert(false);
  }

  sort(samples.begin(), samples.end());
  double min_sample = samples[0];

  return min_sample;
}

////////// mutators /////////////////////////////////////
node_id_t Node::get_random_landmark() {

  Clock curr_time = _timer->get_time();

  for (std::map<node_id_t, Clock>::iterator it = _landmarks.begin();
    it != _landmarks.end(); /* it++ */) {

    node_id_t landmark = it->first;
    Clock last_heard_ago = curr_time - it->second;

    std::map<node_id_t, Clock>::iterator it_now = it;
    it++;

    // forget inactive landmarks -- except the sync node
    // TODO: variablize
    if (landmark != globops.first_landmark && last_heard_ago > 20 * MINUTES) {

      _landmarks.erase(it_now); // it_now is not invalid
      assert(_landmarks.find(landmark) == _landmarks.end());
    }
  }

  if (_landmarks.size() == 0) {
    return NO_SUCH_NODE;
  }

  unsigned int r = _rng->rand_uint(0, _landmarks.size() - 1);

  unsigned int counter = 0;
  for (std::map<node_id_t, Clock>::const_iterator it = _landmarks.begin();
    it != _landmarks.end(); it++) {

    if (counter++ == r) {
      return it->first;
    }
  }

  // shouldn't happen
  std::cerr << "landmarks size=" << _landmarks.size() << "; r=" << r << std::endl;
  assert(false);
  return NO_SUCH_NODE;
}

void Node::add_landmark(const node_id_t& node) {

  std::vector<std::pair<node_id_t, Clock> > landmark;
  landmark.push_back(make_pair(node, 0 * SECONDS));
  add_landmarks(landmark);
}

void Node::add_landmarks(const std::vector<std::pair<node_id_t, Clock> >& nodes) {

  Clock curr_time = _timer->get_time();

  for (std::vector<std::pair<node_id_t, Clock> >::const_iterator it = nodes.begin();
    it != nodes.end(); it++) {

    node_id_t landmark = it->first;
    Clock last_heard_ago = it->second;

    // do not add the node itself to the landmark list
    if (landmark == _node_id) {
      continue;
    }

    std::map<node_id_t, Clock>::const_iterator it = _landmarks.find(landmark);
    if (it != _landmarks.end()) {
      _landmarks[landmark] = max(_landmarks[landmark], curr_time - last_heard_ago);
    } else {
      //std::cout << "adding new landmark: " << landmark.str() << std::endl;
      _landmarks[landmark] = curr_time - last_heard_ago;
    }

    std::map<node_id_t, std::pair<Coordinate, std::vector<double> > >::const_iterator
      it2 = _landmark_samples.find(landmark);
    if (it2 == _landmark_samples.end()) {
      _landmark_samples[landmark] = make_pair(Coordinate(), vector<double>());
    }
  }
}


void Node::add_landmark_measurement(const node_id_t& landmark,
  double ow_latency_in_msecs, const Coordinate& coord) {

  std::map<node_id_t, std::pair<Coordinate, std::vector<double> > >::const_iterator
    it = _landmark_samples.find(landmark);
  assert(it != _landmark_samples.end());

  vector<double> samples = it->second.second;

  if (samples.size() == globops.vivaldi_num_samples) {
    samples.erase(samples.begin());
  }
  
  // save the current sample
  samples.push_back(ow_latency_in_msecs);

  // update the info in landmark_samples
  _landmark_samples[landmark] = make_pair(coord, samples);
}

/**
 * update the nc of the node depending on rtt and sender coordinate
 */
void Node::update_local_nc(const Coordinate& sender_coord,
  double ow_latency_in_msecs) {

  assert(!_is_rappel_online);

#ifdef DEBUG
  printf(" old coordinate (app) => %s\n", _nc.to_string().c_str());
  printf(" old coordinate (sys) => %s\n", _nc.to_string(false).c_str());
  printf(" sender coordinate (app) => %s\n", sender_coord.to_string().c_str());
  printf(" sender latency => %0.3f\n", ow_latency_in_msecs);
#endif // DEBUG

  bool coord_changed = _nc.update(sender_coord, ow_latency_in_msecs, false);

#ifdef DEBUG
  if (coord_changed) {
    printf(" new coordinate (synced) => %s\n", _nc.to_string().c_str());
  } else {
    printf(" no change in coordinate\n");
  }
#endif // DEBUG

  if (coord_changed) {
    if (_rappel != NULL) { // if a rappel node
      flush_coords_to_db();
    }
  }
}

void Node::flush_coords_to_db() {

  if (g_stats_insert_coordinates_stmt == NULL) {

    string sql_insert_coordinates(
      "INSERT INTO coordinates(node_id, sync_time, "
        "version, app_coords_1, app_coords_2, app_coords_3, app_coords_4, app_coords_5"
#if COMPILE_FOR == SIMULATOR
        ") VALUES(?, ?, ?, ?, ?, ?, ?, ?)");
#elif COMPILE_FOR == NETWORK
        ", sync_time) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)");
#endif // COMPILE_FOR
    assert(sqlite3_prepare(stats_db, sql_insert_coordinates.c_str(),
      -1, &g_stats_insert_coordinates_stmt, NULL) == SQLITE_OK);
  }
  
  Clock curr_time = _timer->get_time();

  // get app coords
  std::vector<double> app_coords = _nc.get_app_coords();
      
#if COMPILE_FOR == SIMULATOR
  assert(sqlite3_bind_int(g_stats_insert_coordinates_stmt, 1,
    _node_id.get_address()) == SQLITE_OK);
#elif COMPILE_FOR == NETWORK
  assert(sqlite3_bind_text(g_stats_insert_coordinates_stmt, 1,
    _node_id.str().c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);
#endif
  assert(sqlite3_bind_int64(g_stats_insert_coordinates_stmt, 2,
    _timer->get_sync_time(curr_time)) == SQLITE_OK);
  assert(sqlite3_bind_int(g_stats_insert_coordinates_stmt, 3,
    _nc.get_version()) == SQLITE_OK);
  assert(sqlite3_bind_double(g_stats_insert_coordinates_stmt, 4,
    app_coords[0]) == SQLITE_OK);
  assert(sqlite3_bind_double(g_stats_insert_coordinates_stmt, 5,
    app_coords[1]) == SQLITE_OK);
  assert(sqlite3_bind_double(g_stats_insert_coordinates_stmt, 6,
    app_coords[2]) == SQLITE_OK);
  assert(sqlite3_bind_double(g_stats_insert_coordinates_stmt, 7,
    app_coords[3]) == SQLITE_OK);
  assert(sqlite3_bind_double(g_stats_insert_coordinates_stmt, 8,
    app_coords[4]) == SQLITE_OK);
#if COMPILE_FOR == NETWORK
  assert(sqlite3_bind_int64(g_stats_insert_coordinates_stmt, 2,
    curr_time) == SQLITE_OK);
#endif // COMPILE_FOR == NETWORK

  assert(sqlite3_step(g_stats_insert_coordinates_stmt) == SQLITE_DONE);
  sqlite3_reset(g_stats_insert_coordinates_stmt);
}

void Node::signal_vivaldi_bootstrap_complete() { // from vivaldi

  assert(_rappel != NULL);
  assert(!_bootstrap_phase_complete);

  // indicate that we are bootstrapped
  _bootstrap_phase_complete = true;

  // force a sync -- at bootstrap complete time
  // NOTE: this is req'd for nodes who never move out of the origin
  // as their movement resides within the origin +/- stability range
  _nc.sync();
  flush_coords_to_db();

#if COMPILE_FOR == NETWORK
  // send message to ctrl server
  Message msg;

  CommandMessageData* data = new CommandMessageData;
  data->self_name = globops.self_name;

  // swap src and dst as we send this message out directly via udp_send()
  msg.src = _node_id;
  msg.dst = node_id_t(_node_id.get_address(), globops.ctrl_port);
  msg.layer = LYR_CONTROL;
  msg.flag = MSG_CMD_SIGNAL_BOOTSTRAPPED;
  msg.data = data;

  _transport->send(msg);
#endif // COMPILE_FOR == NETWORK
}

////////////// Vivaldi specific
void Node::vivaldi_init() {

  assert(_vivaldi == NULL);
  assert(_rappel == NULL);

  // initialize vivaldi
  _vivaldi = new Vivaldi(this);
  assert(_vivaldi != NULL);

  _vivaldi->bring_online();
}

////////////// RAPPEL specific
bool Node::rappel_is_publisher() const {

  Rappel* rappel = dynamic_cast<Rappel*>(_rappel);
  return (_rappel != NULL && rappel->is_publisher());
}

bool Node::rappel_is_subscribed_to(const feed_id_t& feed_id) const {

  Rappel* rappel = dynamic_cast<Rappel*>(_rappel);
  return (_rappel != NULL && rappel->is_subscribed_to(feed_id));
}

void Node::rappel_init(bool is_publisher) {

  assert(_vivaldi == NULL);
  assert(_rappel == NULL);
  assert(_is_rappel_online == false);

  // initialize vivaldi
  _vivaldi = new Vivaldi(this);
  assert(_vivaldi != NULL);

  _rappel = new Rappel(this, is_publisher);
  assert(_rappel != NULL);

  // a rappel node also runs vivaldi. once vivaldi bootstraps the node's
  // NC, a  rappel node may be brought online
  _vivaldi->bring_online(); // NOTE: must be called after new Rappel
}

void Node::rappel_bring_online() {

  assert(_is_rappel_online == false);
  assert(_rappel != NULL);
  assert(_bootstrap_phase_complete);

  // in the beginning
  _is_rappel_online = true;
  _rappel_online_since = _timer->get_time();

  ((Vivaldi*) _vivaldi)->deactivate();
  _rappel->bring_online(); 
}

void Node::rappel_take_offline(bool final) {

  assert(_rappel != NULL);
  assert(_is_rappel_online == true);

  _rappel->take_offline();
  _is_rappel_online = false;

  if (!final) {
    ((Vivaldi*) _vivaldi)->reactivate();
  }
}

void Node::rappel_add_feed(const feed_id_t& feed_id) {

  assert(_rappel != NULL);

  Rappel* rappel = dynamic_cast<Rappel*>(_rappel);
  assert(rappel != NULL);
  rappel->add_feed(feed_id);
}

void Node::rappel_remove_feed(const feed_id_t& feed_id) {

  assert(_rappel != NULL);

  Rappel* rappel = dynamic_cast<Rappel*>(_rappel);
  assert(rappel != NULL);
  rappel->add_feed(feed_id);
}

void Node::rappel_publish_update(size_t num_bytes) {

  assert(_rappel != NULL);

  Rappel* rappel = dynamic_cast<Rappel*>(_rappel);
  assert(rappel != NULL);
  assert(rappel->is_publisher());
  rappel->publish_update(num_bytes);
}

#if COMPILE_FOR == NETWORK
void Node::rappel_add_feed_rtt_sample(const feed_id_t& feed_id, const Clock& rtt) {

  Rappel* rappel = dynamic_cast<Rappel*>(_rappel);
  assert(_rappel != NULL);
  return rappel->add_feed_rtt_sample(feed_id, rtt);
}
#endif // COMPILE_FOR == NETWORK
