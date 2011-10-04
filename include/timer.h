#ifndef _TIMER_H
#define _TIMER_H

#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <cmath>
#include <string>
#include <vector>
#include <queue>
#include <map>
#include <utility>
#include "rappel_common.h"
#include "data_objects.h"
#if COMPILE_FOR == NETWORK
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include "asio.hpp"
#endif

#if COMPILE_FOR == NETWORK
const Clock DEFAULT_SYNC_OFFSET = -7 * DAYS;
const Clock DEFAULT_SYNC_OFFSET_CRITICAL_DRIFT = 10 * MILLI_SECONDS;
const Clock DEFAULT_SYNC_OFFSET_MAX_DRIFT = 10 * SECONDS;
typedef boost::shared_ptr<asio::deadline_timer> timer_ptr;

struct EventInfo {
  Event event;
  asio::deadline_timer* timer;
};
#endif

/**
 *
 */
enum sim_evt_status {
  SMPL_EVENT_SUCCESS = 0x2919,
  SMPL_EVENT_FAILED,
  SMPL_EVENT_FIN,
};

/**
 *
 */
class TimeOrderedEvent {

public:

  // operator
  bool operator>(const TimeOrderedEvent& rhs) const {
    return (whence > rhs.whence);
  }

public: //hack
  event_id_t id;
  Clock whence;
};

typedef int (*EventCallbackFunction)(Event& event);

/**
 *
 */
class Timer {

public:
  Timer();
  ~Timer();

  // singleton
  static Timer* get_instance();

  // accessors
#if COMPILE_FOR == SIMULATOR
  Clock get_time() const {
    return _time;
#elif COMPILE_FOR == NETWORK
  Clock get_time() { // not an accessor
    boost::posix_time::time_duration diff
      = boost::posix_time::microsec_clock::local_time() - _epoch;
    Clock now = diff.total_microseconds();
    if (now < _last_time) {
      std::cerr << "Time skew detected. now=" << get_time_str(now)
        << "; last_time=" << get_time_str(_last_time) << std::endl;
      now = _last_time;
    }
    _last_time = now;
    return now;
#endif // COMPILE_FOR
  }
  std::string get_time_str(const Clock& time) const {
    char buffer[MAX_STRING_LENGTH];
    Clock time_in_secs = time / SECOND;
    Clock time_in_millisecs = (time % SECOND) / MILLI_SECOND;
    Clock time_in_microsecs = (time % MILLI_SECOND) / MICRO_SECOND;
    sprintf(buffer, "%03lld.%03lld%03lld",  time_in_secs, time_in_millisecs,
      time_in_microsecs);
    return std::string(buffer);
  }
#if COMPILE_FOR == SIMULATOR
  std::string get_time_str() const {
#elif COMPILE_FOR == NETWORK
  std::string get_time_str() {
#endif // COMPILE_FOR
    return get_time_str(get_time());
  }
  Clock get_sync_offset() const {
    return _sync_offset;
  }
  Clock get_sync_time(const Clock& time) const {
    return time + _sync_offset;
  }
#if COMPILE_FOR == SIMULATOR
  Clock get_sync_time() const {
#elif COMPILE_FOR == NETWORK
  Clock get_sync_time() {
#endif // COMPILE_FOR
    return get_sync_time(get_time());
  }

  // mutators
  void register_callback(Layer layer, EventCallbackFunction callback);
  void set_sync_offset(const Clock& sync_offset) {
#if COMPILE_FOR == SIMULATOR
    assert(false);
#elif COMPILE_FOR == NETWORK
    Clock offset_diff = sync_offset - _initial_sync_offset;
    if (_sync_offset == DEFAULT_SYNC_OFFSET) {
      _initial_sync_offset = sync_offset;
    // Below: some problem with gcc  -- doing llabs manually
    //} else if (llabs(offset_diff) > DEFAULT_SYNC_OFFSET_MAX_DRIFT) {
    } else if (offset_diff < -1 * DEFAULT_SYNC_OFFSET_MAX_DRIFT
      || offset_diff > DEFAULT_SYNC_OFFSET_MAX_DRIFT) {
      std::cerr << "oh noes, time offset drifted too far" << std::endl;
      assert(false);
    }
    _sync_offset = sync_offset;
#endif // COMPILE_FOR
  }
  event_id_t schedule_event(Event& event);
  bool cancel_event(const event_id_t& event_id);
  event_id_t reschedule_event(event_id_t event_id, const Clock& whence);
#if COMPILE_FOR == SIMULATOR
  void start_timer();
#elif COMPILE_FOR == NETWORK
  void cancel_all_pending_events();
#endif // COMPILE_FOR

private:
  // variables
  static Timer* _instance;

  std::map<Layer, EventCallbackFunction> _callbacks;

  Clock _sync_offset;
  Clock _initial_sync_offset;
  event_id_t _current_event_id;
  unsigned int _num_events_executed;

#if COMPILE_FOR == SIMULATOR
  Clock _time;
  std::priority_queue< TimeOrderedEvent, 
    std::vector<TimeOrderedEvent>, 
    std::greater<TimeOrderedEvent> > _eventQ;
  std::map<event_id_t, Event> _eventList;

#elif COMPILE_FOR == NETWORK
  Clock _last_time;
  boost::posix_time::ptime _epoch;
  IOService* __io_service;
  std::map<event_id_t, EventInfo> _eventList;

#endif

  // private functions
  event_id_t schedule_event_aux(Event& event);
  sim_evt_status execute_event_aux(const event_id_t& event_id);

#if COMPILE_FOR == NETWORK
  void execute_event_aux_network(asio::deadline_timer* timer,
    const event_id_t& event_id, const asio::error& error);
#endif
};

#endif // _TIMER_H
