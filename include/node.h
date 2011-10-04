#ifndef _NODE_H
#define _NODE_H

#include <vector>
#include "rappel_common.h"
#include "timer.h"
#include "transport.h"
#include "coordinate.h"

class Application {
public:
  virtual ~Application() { }

  virtual void bring_online() = 0;
  virtual void take_offline() = 0;
  virtual int event_callback(Event& event) = 0;
  virtual int msg_callback(Message& msg) = 0;
  virtual void debug(unsigned int seq_num) = 0;
};

class Node {

public :
  Node(const node_id_t& node_id);
  ~Node();

  int event_callback(Event& event);
  int msg_callback(Message& msg);
  void debug(unsigned int seq_num);

  // accessors
  node_id_t get_node_id() const {
    return _node_id;
  }
  bool is_bootstrapped() const {
    assert(!is_a_vivaldi_node());
    return _bootstrap_phase_complete;
  }
  bool is_a_vivaldi_node() const {
    return (_rappel == NULL); // a vivaldi (exclusive) node has no rappel instance
  }
  bool is_a_rappel_node() const {
    return (_rappel != NULL);
  }
  bool is_rappel_online() const {
    return _is_rappel_online;
  }
  bool rappel_is_publisher() const;
  bool rappel_is_subscribed_to(const feed_id_t& feed_id) const;
  Clock rappel_online_since() const {
    return _rappel_online_since;
  }
  Coordinate get_nc() const {
    return _nc;
  }
  std::vector<std::pair<node_id_t, Clock> >
    get_sample_landmarks(unsigned int num_landmarks) const;
  std::vector<std::pair<node_id_t, Clock> >
    get_all_landmarks() const;
  Coordinate get_landmark_coordinate(const node_id_t& landmark) const;
  double get_filtered_landmark_measurement(const node_id_t& landmark,
    bool just_probing) const;

  // mutators
  // general purpose 
  node_id_t get_random_landmark(); // not const -- as old landmarks are pruned here
  void add_landmark(const node_id_t& node);
  void add_landmarks(const std::vector<std::pair<node_id_t, Clock> >& nodes);
  void add_landmark_measurement(const node_id_t& landmark,
    double ow_latency_in_msecs, const Coordinate& coord);
  void update_local_nc(const Coordinate& sender_coord,
    double ow_latency_in_msecs);
  void signal_vivaldi_bootstrap_complete(); // from vivaldi

  // Vivaldi specific functions
  void vivaldi_init();

  // RAPPEL specific functions
  void rappel_init(bool is_publisher);
  void rappel_bring_online();
  void rappel_take_offline(bool final);
  void rappel_add_feed(const feed_id_t& feed_id);
  void rappel_remove_feed(const feed_id_t& feed_id);
  void rappel_publish_update(size_t num_bytes);
  void rappel_add_feed_rtt_sample(const feed_id_t& feed_id, const Clock& rtt);

private:

  Application* _rappel;
  Application* _vivaldi;

  node_id_t _node_id;
  bool _bootstrap_phase_complete;
  Coordinate _nc; // network coordinate
  bool _is_rappel_online;
  Clock _rappel_online_since;
  std::map<node_id_t, Clock> _landmarks;
  std::map<node_id_t, std::pair<Coordinate, std::vector<double> > >
    _landmark_samples;
  
  // utility singletons
  Timer* _timer;
  Transport* _transport;
  RNG* _rng;


  // internal functions
  void flush_coords_to_db();
};

#endif // _NODE_H
