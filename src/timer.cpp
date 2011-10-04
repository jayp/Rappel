#include <cmath>
#include "timer.h"
#include "debug.h"

using namespace std;

static const Clock TIMER_SKEW_LIMIT = 2 * SECONDS;
Timer* Timer::_instance = NULL; // initialize pointer

/**
 * Constructor
 */
Timer::Timer() {

  assert(globops.is_initialized());
  _num_events_executed = 0;
  _current_event_id = NO_SUCH_EVENT_ID;

#if COMPILE_FOR == SIMULATOR
  _time = 0LL;
  _sync_offset = 0 * SECONDS;

#elif COMPILE_FOR == NETWORK

  __io_service = IOService::get_instance();
  _epoch = boost::posix_time::microsec_clock::local_time();
  _last_time = 0 * SECONDS;

  _sync_offset = DEFAULT_SYNC_OFFSET;
#endif
}

/**
 * Destructor
 */
Timer::~Timer() {
}

/**
 * Singleton function
 */
Timer* Timer::get_instance() {

  if (_instance == NULL) {  
  	_instance = new Timer(); // create sole instance
  	assert(_instance != NULL);
  }

return _instance; // address of sole instance
}

/**
* One of the most important functions: whenever an event is ready to be
* executed, the timer will invoke a callback to the registered callback
* function of the appropriate layer. The callback function should be registered
* in the layers's _init() function.
*
* @param layer The layer which is registering the callback function
* @param callback The callback function to be invoked whenever an event is
* ready to be executed
*/
void Timer::register_callback(Layer layer, EventCallbackFunction callback) {

  // make sure that the application isn't already registered
  // TODO: think if re-registration should be allowed?
  assert(_callbacks.find(layer) == _callbacks.end());

  _callbacks[layer] = callback;
}

/**
* Schedule a future event at a foriegn node (can be self-scheduled event)
*
* @param event The event to be scheduled
* @return The event ID
*/
event_id_t Timer::schedule_event(Event& event) {

  // make sure the application callback is registered
  assert(_callbacks.find(event.layer) != _callbacks.end());

  // make sure the event is initialized and it does not happen in the past
  assert(event.is_initialized());

  // must take place in the future, but not too much in the future
  assert(event.whence >= 0 && event.whence <= 31 * DAYS);
   
  return schedule_event_aux(event);
}

/**
 * stuff
 */
event_id_t Timer::schedule_event_aux(Event& event) {

  // transform: relative whence -> absolute whence
  Clock relative_whence = event.whence;
  Clock curr_time = get_time();
  event.whence = curr_time + relative_whence;

  // increment current event id
  _current_event_id++;
  event.id = _current_event_id;

#ifdef DEBUG  
  if (event.layer != LYR_TRANSPORT) {
    printf("[Time=%s][Event][Schedule] event_id=%u, node=%s, lyr=%s, flag=%s, "
      "whence=%s\n",
      get_time_str(curr_time).c_str(), _current_event_id, event.node_id.str().c_str(),
      layer_str(event.layer), event_flag_str(event.flag), 
      get_time_str(event.whence).c_str());
  }
#endif // DEBUG

  // insert event into eventList
  assert(_eventList.find(_current_event_id) == _eventList.end());

#if COMPILE_FOR == SIMULATOR

  TimeOrderedEvent toe;
  toe.id = _current_event_id;
  toe.whence = event.whence;

  _eventQ.push(toe);

  _eventList[_current_event_id] = event;

#elif COMPILE_FOR == NETWORK

  // timer_ptr timer(new asio::deadline_timer(__io_service->get_ios()));
  asio::deadline_timer* timer = new asio::deadline_timer(__io_service->get_ios());

  timer->expires_from_now(boost::posix_time::microseconds(relative_whence));
  timer->async_wait(boost::bind(&Timer::execute_event_aux_network, this,
    timer, _current_event_id, asio::placeholders::error));

  EventInfo event_info;
  event_info.event = event;
  event_info.timer = timer;

  _eventList[_current_event_id] = event_info;

#endif

  return _current_event_id;
}

/**
* Cancel a previously schedule event
* @param event_id The ID of a previously scheduled event
* @return if cancel succeeded
*/
bool Timer::cancel_event(const event_id_t& event_id) {

  // event must exist in event list
  if (_eventList.find(event_id) != _eventList.end()) {

    // fetch the event from eventList, and then delete it
#if COMPILE_FOR == SIMULATOR
    Event event = _eventList[event_id];
#elif COMPILE_FOR == NETWORK
    EventInfo event_info = _eventList[event_id];
    Event event = event_info.event;
    event_info.timer->cancel(); // cancel timer
#endif

#ifdef DEBUG
    printf("[Time=%s][Event][Cancel] id=%u, node=%s, lyr=%s, flag=%s\n",
      get_time_str().c_str(), event_id, event.node_id.str().c_str(),
      layer_str(event.layer), event_flag_str(event.flag));
#endif // DEBUG
    _eventList.erase(event_id);
    assert(_eventList.find(event_id) == _eventList.end());

    // unallocate memory
    delete(event.data);
    return true;
  }

  return false;
}

/**
* Reschedule a previously scheduled event
* @param event_id The ID of a previously scheduled event
* @param whence time of the new event
* @return the new event ID
*/
event_id_t Timer::reschedule_event(event_id_t event_id, const Clock& whence) {

  // must take place in the future, but not too much in the future
  assert(whence >= 0 && whence <= 31 * DAYS);

  assert(_eventList.find(event_id) != _eventList.end());
  
#ifdef DEBUG
  printf("[Time=%s][Event][Cancel] id=%u\n", get_time_str().c_str(),
    event_id);
#endif // DEBUG

  // fetch the event from eventList, and then delete it
#if COMPILE_FOR == SIMULATOR
  Event event = _eventList[event_id];
#elif COMPILE_FOR == NETWORK
  EventInfo event_info = _eventList[event_id];
  Event event = event_info.event;
  event_info.timer->cancel(); // cancel current timer
#endif

  _eventList.erase(event_id);
  assert(_eventList.find(event_id) == _eventList.end());

  event.whence = whence; // new whence

return schedule_event(event); // reschedule
}

#if COMPILE_FOR == SIMULATOR
/**
 * Continually dequeue and execute the next event.
 *
 * @param termination_time The timestamp of the last event to execute
 */
void Timer::start_timer() {

  printf("__SIMULATION STARTS__\n");
  
  unsigned int pct_complete = 0;
  double next_marker = (double) globops.sys_termination_time
    * (pct_complete + 1) / (double) 100;

  while (!_eventQ.empty()) {

    TimeOrderedEvent toe = _eventQ.top();
    _eventQ.pop();
    ++_num_events_executed;

    // adjust the time 
    assert(toe.whence >= _time);
    _time = toe.whence;

    if ((double) _time >= next_marker) {
      pct_complete++;
      printf("SIMULATION %u%% complete [EVENTS: %u executed; %u queued]\n",
        pct_complete, _num_events_executed, _current_event_id);
      next_marker = (double) globops.sys_termination_time
        * (pct_complete + 1) / (double) 100;
    }

    if (execute_event_aux(toe.id) == SMPL_EVENT_FIN) {

      printf("__SIMULATION DONE__\n");
      return;
    }
  }

  return;  
}

#elif COMPILE_FOR == NETWORK 

void Timer::cancel_all_pending_events() {

  for(std::map<event_id_t, EventInfo>::iterator it = _eventList.begin();
    it != _eventList.end(); /* it++*/) {

    // it becomes invalid after erase in cancel_event(). hence, it++ becomes
    // invalid. hence, calculating it++ before it becomes invalid.
    std::map<event_id_t, EventInfo>::iterator it_now = it;
    it++;

    cancel_event(it_now->first);
  }
}

void Timer::execute_event_aux_network(asio::deadline_timer* timer,
  const event_id_t& event_id, const asio::error& error) {

  if (!error) {
    execute_event_aux(event_id);
  }

  // deallocate memory
  delete(timer);
}

#endif // COMPILE_FOR

sim_evt_status Timer::execute_event_aux(const event_id_t& event_id) {

  Clock init_curr_time = get_time();

#if COMPILE_FOR == SIMULATOR
  // terminate, if need be 
  if (init_curr_time > globops.sys_termination_time + 1 * MICRO_SECOND) {

    printf("THIS SHOULDN'T HAPPEN\n");
    assert(false);
  }
#endif

  // check if the event exists in the event list
  if (_eventList.find(event_id) == _eventList.end()) {
    // the event doesn't exist: must be already canceled
    return SMPL_EVENT_FAILED;
  }

  // fetch event from _eventList, and then delete it
#if COMPILE_FOR == SIMULATOR
  Event event = _eventList[event_id];
#elif COMPILE_FOR == NETWORK
  EventInfo event_info = _eventList[event_id];
  Event event = event_info.event;
#endif
  _eventList.erase(event_id);
  assert(_eventList.find(event_id) == _eventList.end());

#if COMPILE_FOR == SIMULATOR
  assert(event.whence == _time);
#elif COMPILE_FOR == NETWORK
  // make sure the event is being executed at the correct time
  Clock skewed_by = init_curr_time - event.whence;
  if (abs(skewed_by) > TIMER_SKEW_LIMIT) {
    std::cerr << "@ now=" << get_time_str(init_curr_time) << ": event id=" << event_id
      << "; whence=" << get_time_str(event.whence) << "; skewed_by=" <<
      get_time_str(skewed_by) << std::endl;
    //assert(false);
  }
#endif

#ifdef DEBUG
  if (event.layer != LYR_TRANSPORT) {
    printf("[Time=%s][Event][Execute] id=%u, node=%s, lyr=%s, flag=%s\n",
      get_time_str(init_curr_time).c_str(), event_id, event.node_id.str().c_str(),
      layer_str(event.layer), event_flag_str(event.flag));
  }
#endif // DEBUG

  // take care of special events
  if (event.flag == EVT_FINAL) {

    for (map<Layer, EventCallbackFunction>::iterator
      it = _callbacks.begin(); it != _callbacks.end(); it++) {

      Event final_event;
      final_event.layer = it->first;
      final_event.flag = EVT_FINAL;

      it->second(final_event); // HO -- LYR_DRIVER will delete teh
    }

    return SMPL_EVENT_FIN;

  } else if (event.flag == EVT_DEBUG) {

    // adjust whence; account for time to execute debug()
    event.whence = 1 * SECOND;
    schedule_event_aux(event);

    return SMPL_EVENT_SUCCESS;

  } else if (event.flag == EVT_STATS) {

    for (map<Layer, EventCallbackFunction>::iterator
      it = _callbacks.begin(); it != _callbacks.end(); it++) {

      Event debug_event = event;
      debug_event.layer = it->first; // just change the layer

      it->second(debug_event);
    }
    
    // schedule the next debug event
    StatsEventData* data = dynamic_cast<StatsEventData*>(event.data);
    data->seq_num++; // resuse memory -- no need to delete and new

    // adjust whence; account for time to execute debug()
    event.whence = data->start_sync_time + data->seq_num * globops.sys_stats_interval
      - get_sync_time();
    schedule_event_aux(event);
    
    // this is not a "real" event: get out without executing
    return SMPL_EVENT_SUCCESS;
  }
    
  // execute the event
  assert(_callbacks.find(event.layer) != _callbacks.end());
  _callbacks[event.layer](event);
  return SMPL_EVENT_SUCCESS;
}
