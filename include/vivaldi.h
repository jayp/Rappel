#ifndef _VIVALDI_H
#define _VIVALDI_H

#include "rappel_common.h"
#include "timer.h"
#include "transport.h"
#include "node.h"

class Vivaldi: public Application {
public:
  // constructor and destructor
  Vivaldi(Node* node);
  ~Vivaldi();

  // callbacks
  int event_callback(Event& event);
  int msg_callback(Message& msg);
  void debug(unsigned int seq_num);

  void bring_online();
  void take_offline();
  void deactivate();
  void reactivate();

private:
  Node* _node;
  unsigned int _num_ping_replies;
  std::vector<Clock> _sync_offsets; // .size() = _num_sync_replies
  bool _ping_in_progress;
  bool _sync_in_progress;
/*
  event_id_t _bootstrap_check_event_id;
*/
  event_id_t _periodic_ping_event_id;
  event_id_t _wait_for_ping_reply;
#if COMPILE_FOR == NETWORK
  event_id_t _periodic_sync_event_id;
  event_id_t _wait_for_sync_reply;
#endif // COMPILE_FOR == NETWORK
  bool _inactive;

  // utility singletons
  Timer* _timer;
  Transport* _transport;
  RNG* _rng;

  // internal methods for callbacks
  int timer_vivaldi_bootstrap_period_expired(Event& event);
  int timer_periodic_ping(Event& evt);
  int timer_periodic_sync(Event& evt);
  int timer_wait_after_ping_req(Event& event);
  int timer_wait_after_sync_req(Event& event);
  int receive_ping_req(Message& msg);
  int receive_ping_reply(Message& msg);
  int receive_sync_reply(Message& msg);
  int receive_sync_req(Message& msg);

  // internal helper methods
  void schedule_periodic_ping(bool initial);
  void send_ping();
  void schedule_periodic_sync(bool initial);
  void send_sync_req(bool resend_on_failure);
};

#endif // _VIVALDI_H
