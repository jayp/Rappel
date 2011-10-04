#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <string>
#include <set>
#include <vector>
#include <map>
#include <algorithm>
#include <iostream>
#include <fstream>
#include "rappel_common.h"
#include "timer.h"
#include "transport.h"
#include "node.h"
//#include "rappel.h"
//#include "vivaldi.h"
#include "stats.h"

// global constants 
static const std::string g_universe_file = "universe.data";
static const std::string g_subscriptions_file = "subscriptions.data";
static const std::string g_updates_file = "updates.data";
static const std::string g_churn_file = "churn.data";
static const unsigned int FIRST_LANDMARK_NODE_ID = 10000000;

// global variables
static stats_system_per_cycle_t g_curr_cycle_stats;
static std::map<node_id_t, Node*> g_network;
//static Node* g_special_node;
static sqlite3_stmt* g_stats_insert_system_stmt;
static sqlite3_stmt* g_stats_insert_update_stmt;
static std::map<node_id_t, std::vector<std::pair<Clock, Clock> > > g_churn_sessions;

enum ApplicationID { RAPPEL, VIVALDI };

/** This is a generic class for all driver events. It is not specialized
 * per-event as for RAPPEL (an optimization we can perform later, time
 * permissing)
 */
class DriverEventData: public EventData {
public:
  node_id_t node_id;
  ApplicationID instance;
  bool is_publisher;
  feed_id_t feed_id;
  size_t update_size;
  node_id_t bootstrap_landmark;
};

// function prototypes
static int driver_event_callback(Event& event);
static int node_event_callback(Event& event);
static int node_msg_callback(Message& msg);
static unsigned int output_update_stats(bool final);
static Clock duration_node_online_at(const node_id_t& node_id, const Clock& time,
  bool with_padding);

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv) {

  load_config(argc, argv);
  assert(globops.is_initialized());
  assert(globops.sim_bootstrap_time > 5 * MINUTES);
  Clock true_sys_termination_time = globops.sys_termination_time;
  globops.sys_termination_time += globops.sim_bootstrap_time;

#ifndef IGNORE_ETIENNE_STATS

  init_stats_db();

  g_curr_cycle_stats.total_nodes = 0;
  g_curr_cycle_stats.node_leaves = 0;
  g_curr_cycle_stats.node_joins = 0; 

#endif // IGNORE_ETIENNE_STATS
  
  // initialize random number generator
  RNG* rng = RNG::get_instance();
  assert(rng != NULL);
  
  // initialize timer
  Timer* timer = Timer::get_instance();
  assert(timer != NULL);

  // initialize transport
  Transport* transport = Transport::get_instance();
  assert(transport != NULL);
  
  // register driver callback
  timer->register_callback(LYR_DRIVER, driver_event_callback);
  
  // register node callbacks
  timer->register_callback(LYR_APPLICATION, node_event_callback);
  transport->register_callback(LYR_APPLICATION, node_msg_callback);

  // step 1 : get churn information
  char line[MAX_STRING_LENGTH]; // node activity description (from traces)
  string file_location_churn = globops.experiment + "/" + g_churn_file;
  FILE *file = fopen (file_location_churn.c_str(), "r");
  assert(file != NULL); 

  map<node_id_t, vector<pair<Clock, bool> > > churn;
   //map<node_id_t, vector<pair<Clock,Clock> > > uptimes;

  unsigned int churn_read_nodes = 0 ;
  unsigned int churn_read_events = 0 ;
  while (fgets(line, MAX_STRING_LENGTH, file) != NULL) {

    // get node id
    unsigned int node_id_read;
    sscanf(line,"node %u", &node_id_read);
    node_id_t node_id(node_id_read);

    std::vector<std::pair<Clock, Clock> > curr_sessions;
    churn_read_nodes++;

    // read events ...    
    unsigned int time;
    Clock last_online_at = -1 * MILLI_SECOND;
    Clock last_offline_at = -1 * MILLI_SECOND;
    char action;
    fgets(line, MAX_STRING_LENGTH, file);
    assert(line != NULL);
    sscanf(line,"%u %c", &time, &action);
        
    while (action != 'F') {

      churn_read_events++;
      bool bring_online = (action == 'U');

      Clock absolute_time = static_cast<Clock>(time * SECOND);
      if (absolute_time >= true_sys_termination_time) {
      // skip event
#ifdef DEBUG
      printf("warn: skipping event, node=%u, action=%c, time=%u (secs)\n",
        node_id_read, action, time);
#endif // DEBUG
        goto SKIP_EVENT;
      }

      if (bring_online) {

        assert(last_online_at == -1 * MILLI_SECOND);
        last_online_at = globops.sim_bootstrap_time + absolute_time
          + rng->rand_ulonglong(1 * SECOND);
        if (last_offline_at != -1 * MILLI_SECOND) {
          assert(last_online_at > last_offline_at);
        }
        
      } else {

        assert(last_online_at != -1 * MILLI_SECOND);
        last_offline_at = globops.sim_bootstrap_time + absolute_time
          + rng->rand_ulonglong(1 * SECOND);
        assert(last_offline_at > last_online_at);
        curr_sessions.push_back(make_pair(last_online_at, last_offline_at));
        last_online_at = -1 * MILLI_SECOND;
      }

SKIP_EVENT:
      fgets(line, MAX_STRING_LENGTH, file);
      assert(line != NULL);
      sscanf(line,"%u %c", &time, &action); // remove \t
    }

    if (last_online_at != -1 * MILLI_SECOND) {
      curr_sessions.push_back(make_pair(last_online_at,
        globops.sys_termination_time - 1 * MICRO_SECOND));
    }

    assert(g_churn_sessions.find(node_id) == g_churn_sessions.end());
    assert(curr_sessions.size() >= 1);
    g_churn_sessions[node_id] = curr_sessions;
  }
  fclose (file);

  //printf("\n\nchurn information: read %u nodes and %u events\n",
  //  churn_read_nodes, churn_read_events);

  // step 2a: create landmarks (for bootstrapping NCs)
  node_id_t bootstrap_landmark(FIRST_LANDMARK_NODE_ID);

  // bring them up -- one by one
  for (unsigned int i = 0; i < globops.sim_vivaldi_landmark_count; i++) {

    node_id_t current_landmark_id(FIRST_LANDMARK_NODE_ID + i);
  
    // creation of the landmark
    DriverEventData* evt_data = new DriverEventData;
    evt_data->node_id = current_landmark_id;
    evt_data->bootstrap_landmark = bootstrap_landmark;
    evt_data->instance = VIVALDI;
    
    Event evt;
    evt.node_id = current_landmark_id;
    evt.layer = LYR_DRIVER;
    evt.flag = EVT_DRIVER_ADD_NODE;
    evt.whence = 1 * SECOND + i * MINUTES;
    evt.data = evt_data;
    
    timer->schedule_event(evt);
  }

  // step 2b : get publishers and subscribers from universe file
  // and create node creation/churn events accordingly
  string file_location = globops.experiment + "/" + g_universe_file;
  file = fopen(file_location.c_str(), "r");
  assert(file != NULL);

  while (fgets(line, MAX_STRING_LENGTH, file) != NULL) {
    unsigned int node_id_read;
    char publisher, subscriber;
    sscanf(line, " %u %c %c", &node_id_read, &publisher, &subscriber);

    node_id_t node_id(node_id_read);

#ifdef DEBUG
    printf("adding node: %s\n", node_id.str().c_str());
#endif // DEBUG

    // assert the node has associated churn information
    assert(g_churn_sessions.find(node_id) != g_churn_sessions.end());

    // create node
    DriverEventData* creation_evt_data = new DriverEventData;
    creation_evt_data->node_id = node_id;
    creation_evt_data->bootstrap_landmark = bootstrap_landmark;
    creation_evt_data->instance = RAPPEL;
    creation_evt_data->is_publisher = (publisher == 'Y' || publisher == 'y');

    Event creation_evt;
    creation_evt.node_id = node_id;
    creation_evt.layer = LYR_DRIVER,
    creation_evt.flag = EVT_DRIVER_ADD_NODE;
    creation_evt.whence = globops.sim_bootstrap_time / 2 + rng->rand_ulonglong(30 * SECONDS);
    assert(creation_evt.whence < globops.sim_bootstrap_time);
    creation_evt.data = creation_evt_data;

    assert(creation_evt.whence < globops.sys_termination_time);
    timer->schedule_event(creation_evt);

    for (vector<pair<Clock, Clock> >::const_iterator
      it = g_churn_sessions[node_id].begin();
      it != g_churn_sessions[node_id].end(); it++) {

      // join event
      DriverEventData* join_evt_data = new DriverEventData;
      join_evt_data->node_id = node_id;

      Event join_evt;
      join_evt.node_id = node_id;
      join_evt.layer = LYR_DRIVER;
      join_evt.flag = EVT_DRIVER_BRING_NODE_ONLINE;
      join_evt.whence = it->first;
      join_evt.data = join_evt_data;
      assert(join_evt.whence < globops.sys_termination_time);
      timer->schedule_event(join_evt);

      // leave event
      DriverEventData* leave_evt_data = new DriverEventData;
      leave_evt_data->node_id = node_id;

      Event leave_evt;
      leave_evt.node_id = node_id;
      leave_evt.layer = LYR_DRIVER;
      leave_evt.flag = EVT_DRIVER_TAKE_NODE_OFFLINE;
      leave_evt.whence = it->second;
      leave_evt.data = leave_evt_data;
      assert(leave_evt.whence < globops.sys_termination_time);
      timer->schedule_event(leave_evt);
    }
  }

  fclose(file);

  // step 3: feed add events
  // TODO find a better model; for the moment nodes subscribe to a feed
  // as soon as they go online

  file_location = globops.experiment + "/" + g_subscriptions_file;
  file = fopen(file_location.c_str(), "r");
  assert(file != NULL);


  while (fgets(line, MAX_STRING_LENGTH, file) != NULL) {
    char* ptr = line;
    unsigned int subscriber_id_read;
    unsigned int num_feeds;
    int chars_read;
    
    sscanf(ptr, " %u %u %n", &subscriber_id_read, &num_feeds, &chars_read);
    ptr += chars_read;
    
    node_id_t subscriber_id(subscriber_id_read);

    // check the node will be up at some point ...
    if (g_churn_sessions.find(subscriber_id) == g_churn_sessions.end()) {
      cerr << "ARGHHH node " << subscriber_id.str() << " is never online." << endl;
      assert(false);
    }
    
    std::vector<std::pair<Clock, Clock> >
      curr_sessions = g_churn_sessions[subscriber_id];
    assert(curr_sessions.size() >= 1);
    Clock subscriber_join_time = curr_sessions[0].first;
    
    unsigned int feeds_read = 0;
    while (feeds_read < num_feeds) {

      unsigned int feed_id_read;

      sscanf(ptr, " %u %n", &feed_id_read, &chars_read);
      ptr += chars_read;
      
      feed_id_t feed_id(feed_id_read);
      assert(subscriber_id != (node_id_t) feed_id);

#ifdef DEBUG
      printf("(driver3) dbg: adding feed for subscriber=%s: %s\n",
        subscriber_id.str().c_str(), feed_id.str().c_str());
#endif // DEBUG

      DriverEventData* evt_data = new DriverEventData;
      evt_data->node_id = subscriber_id;
      evt_data->feed_id = feed_id;

      Event evt;
      evt.node_id = subscriber_id;
      evt.layer = LYR_DRIVER,
      evt.flag = EVT_DRIVER_ADD_FEED;
      evt.whence = subscriber_join_time + 1 * MICRO_SECOND
        + rng->rand_ulonglong(1 * MILLI_SECOND);
      evt.data = evt_data;

      assert(evt.whence < globops.sys_termination_time);
      timer->schedule_event(evt);
   
      feeds_read++;
    }
  }

  // step 4: update events
  // NOTE: updates need to occur only during publisher uptime 
  file_location = globops.experiment + "/" + g_updates_file;
  file = fopen(file_location.c_str(), "r");
  assert(file != NULL);

  while (fgets(line, MAX_STRING_LENGTH, file) != NULL) {
    unsigned int time_in_secs;
    size_t update_title_size, update_body_size;

    unsigned int publisher_id_read;
    sscanf(line, " %u %u %u %u",
      &time_in_secs, 
      &publisher_id_read, 
      &update_title_size, 
      &update_body_size);

    node_id_t publisher_id(publisher_id_read);
    Clock update_time = time_in_secs * SECONDS + globops.sim_bootstrap_time
      + rng->rand_ulonglong(1 * MILLI_SECOND);

    if (duration_node_online_at(publisher_id, update_time, false) < 0 * MICRO_SECONDS) {
      printf("adding update of size %u for feed %s at time %u seconds\n",
        (update_title_size + update_body_size),
        publisher_id.str().c_str(),
        time_in_secs);
      assert(false);
    }

#ifdef DEBUG
    printf("adding update of size %u for feed %s at time %u seconds\n",
      (update_title_size+update_body_size),
      publisher_id.str().c_str(),
      time_in_secs);
#endif // DEBUG

    DriverEventData* evt_data = new DriverEventData;
    evt_data->node_id = publisher_id;
    evt_data->update_size = update_title_size + update_body_size;
    
    Event evt;
    evt.node_id = publisher_id;
    evt.layer = LYR_DRIVER;
    evt.flag = EVT_DRIVER_PUBLISH;
    evt.whence = update_time;
    evt.data = evt_data;

    assert(evt.whence < globops.sys_termination_time);
    timer->schedule_event(evt);
  }

  // step 5: stats event
  StatsEventData* stats_data = new StatsEventData;
  stats_data->seq_num = 1;
  stats_data->start_sync_time = globops.sim_bootstrap_time;

  Event stats_evt;
  stats_evt.layer = LYR_DRIVER;
  stats_evt.node_id = node_id_t(1); // who cares -> NO_SUCH_NODE triggers an assert ...
  stats_evt.flag = EVT_STATS;
  stats_evt.data = stats_data;
  stats_evt.whence = globops.sim_bootstrap_time + globops.sys_stats_interval;

  timer->schedule_event(stats_evt);

  // step 6: final event
  Event final_evt;
  final_evt.node_id = node_id_t(1); // who cares -> NO_SUCH_NODE triggers an assert ...
  final_evt.layer = LYR_DRIVER;
  final_evt.flag = EVT_FINAL;
  final_evt.whence = globops.sys_termination_time + 1 * MICRO_SECOND;
  final_evt.data = new DriverEventData; // whatever
  
  timer->schedule_event(final_evt);

  // step 6: launch simulation
  timer->start_timer();

#ifndef IGNORE_ETIENNE_STATS
  // step 7: close db
  final_stats_db();
#endif

  return 0;
}

////////////////////////////////////////

static int driver_event_callback(Event& event) {

  Timer* timer = Timer::get_instance();
  assert(event.layer == LYR_DRIVER);

#ifdef DEBUG
  printf("### driver3 cback with event.flag = %u\n",event.flag);
#endif // DEBUG

  switch (event.flag) {

  case EVT_STATS: {

    // NOTE: node->debug() is called in node_cback

    // flush out as much update stats data as possible
    //unsigned int updates_flushed = 
    output_update_stats(false);
/*
    printf("updates flushed to db: %u\n", updates_flushed);
*/

    ASTopology* as_topology = ASTopology::get_instance();
    //unsigned int as_topology_cleanup = 
    as_topology->cleanup();
/*
    printf("AS ROUTES cached: %u (%u cleaned)\n", 
      as_topology->get_num_routes_cached(), as_topology_cleanup);
*/

    Topology* topology = Topology::get_instance();
    //unsigned int topology_cleanup = 
    topology->cleanup();
/*
    printf("Topology routes used: %u (%u cleaned)\n",
      topology->get_num_routes_used(), topology_cleanup);
*/

#ifndef IGNORE_ETIENNE_STATS

    // save system churn info to stats db
    StatsEventData* data = dynamic_cast<StatsEventData*>(event.data);
    unsigned int seq_num = data->seq_num;

    // prepare -- one time cost
    if (g_stats_insert_system_stmt == NULL) {

      string sql_insert_system("INSERT INTO system_per_cycle(seq_num, sync_time, "
        "total_nodes, node_joins, node_leaves) VALUES (?, ?, ?, ?, ?)");

      if (sqlite3_prepare(stats_db, sql_insert_system.c_str(),
        -1, &g_stats_insert_system_stmt, NULL) != SQLITE_OK) {

        std::cerr << "[[[" << sql_insert_system << "]]]" << std::endl;
        assert(false);
      }
    }

    assert(sqlite3_bind_int(g_stats_insert_system_stmt, 1,
      seq_num) == SQLITE_OK);
    assert(sqlite3_bind_int64(g_stats_insert_system_stmt, 2,
      timer->get_sync_time()) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_system_stmt, 3,
      g_curr_cycle_stats.total_nodes) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_system_stmt, 4,
      g_curr_cycle_stats.node_joins) == SQLITE_OK);
    assert(sqlite3_bind_int(g_stats_insert_system_stmt, 5,
      g_curr_cycle_stats.node_leaves) == SQLITE_OK);

    assert(sqlite3_step(g_stats_insert_system_stmt) == SQLITE_DONE);
    sqlite3_reset(g_stats_insert_system_stmt);

    // reset joins and leaves
    g_curr_cycle_stats.node_joins = 0;
    g_curr_cycle_stats.node_leaves = 0;

#endif // IGNORE_ETIENNE_STATS
    
    return 0; // nothing to delete
  }

  case EVT_FINAL: {

#ifndef IGNORE_ETIENNE_STATS
    output_update_stats(true);
#endif

    return 0; // nothing to delete
  }

  case EVT_DRIVER_ADD_NODE: {
    DriverEventData* data = dynamic_cast<DriverEventData*>(event.data);

#ifdef DEBUG
    printf("(driver3 cback) dbg: EVT_DRIVER_ADD_NODE %s\n", 
      data->node_id.str().c_str());
#endif // DEBUG

    // make sure this node doesn't already exist in the network
    assert(g_network.find(data->node_id) == g_network.end());

    // add node to physical topology
    Topology* topology = Topology::get_instance();
    topology->add_node(data->node_id);

    // insert node in the network
    g_network[data->node_id] = new Node(data->node_id);
    assert(g_network.find(data->node_id) !=  g_network.end());

    // set landarks
    if (data->node_id != data->bootstrap_landmark) {
      g_network[data->node_id]->add_landmark(data->bootstrap_landmark);
    }

    if (data->instance == VIVALDI) {

      g_network[data->node_id]->vivaldi_init();

    } else if (data->instance == RAPPEL) {

#ifndef IGNORE_ETIENNE_STATS

      // create room for churn sessions information for the node
      // we do keep these infos for vivaldi nodes too
/*
      assert(node_stats.find(data->node_id) == node_stats.end());
      stats_node_t node_stat;
      node_stats[data->node_id] = node_stat;
*/
      if (data->is_publisher) {
        assert(feed_stats.find((feed_id_t) data->node_id) == feed_stats.end());
        stats_feed_t feed_stat;
        feed_stats[data->node_id] = feed_stat;
      }

#endif // IGNORE_ETIENNE_STATS

      g_network[data->node_id]->rappel_init(data->is_publisher);

    } else {

      assert(false);
    }

    break;
  }

  case EVT_DRIVER_BRING_NODE_ONLINE: {

    DriverEventData* data = dynamic_cast<DriverEventData*>(event.data);

#ifdef DEBUG
    printf("(driver3 cback) dbg: EVT_DRIVER_BRING_NODE_ONLINE %s\n", 
      data->node_id.str().c_str());
#endif // DEBUG

    // make sure this node exists and offline
    assert(g_network.find(data->node_id) != g_network.end());

#ifndef IGNORE_ETIENNE_STATS

    // keep stats only for vivaldi nodes
    assert(g_network[data->node_id]->is_a_rappel_node());
    assert(g_network[data->node_id]->is_rappel_online() == false);
/*
    // update churn sessions informations
    map<node_id_t, stats_node_t>::iterator
      it_churn_info = node_stats.find(data->node_id);
    assert(it_churn_info != node_stats.end());

    it_churn_info->second.churn_sessions.push_back(make_pair(event.whence,
      globops.sys_termination_time));
*/
    // for driver
    g_curr_cycle_stats.total_nodes++;
    g_curr_cycle_stats.node_joins++;
#endif // IGNORE_ETIENNE_STATS

    // bring online
    g_network[data->node_id]->rappel_bring_online();
    break;
  }

  case EVT_DRIVER_TAKE_NODE_OFFLINE: {

    DriverEventData* data = dynamic_cast<DriverEventData*>(event.data);

#ifdef DEBUG
    printf("(driver3 cback) dbg: EVT_DRIVER_TAKE_NODE_OFFLINE %s\n", 
      data->node_id.str().c_str());
#endif // DEBUG

    // make sure this node exists and online
    assert(g_network.find(data->node_id) != g_network.end());
    assert(g_network[data->node_id]->is_rappel_online());

#ifndef IGNORE_ETIENNE_STATS
    // keep stats only for vivaldi nodes
    if (g_network[data->node_id]->is_a_rappel_node()) {
/*
      // update churn sessions informations
      map<node_id_t, stats_node_t>::iterator
        it_churn_info = node_stats.find(data->node_id);
      assert(it_churn_info != node_stats.end());

      assert(it_churn_info->second.churn_sessions.back().second
        == globops.sys_termination_time);

      it_churn_info->second.churn_sessions.back().second = event.whence;
*/
      // for driver
      g_curr_cycle_stats.total_nodes--;
      g_curr_cycle_stats.node_leaves++;
    }
#endif // IGNORE_ETIENNE_STATS
    // take offline
    g_network[data->node_id]->rappel_take_offline(false);
    break;
  }
    
  case EVT_DRIVER_ADD_FEED: {
    DriverEventData* data = dynamic_cast<DriverEventData*>(event.data);

#ifdef DEBUG
    printf("(driver3 cback) dbg: EVT_DRIVER_ADD_FEED %s to node %s\n",
      data->feed_id.str().c_str(), data->node_id.str().c_str());
#endif // DEBUG

    // make sure this node exists in the network
    assert(g_network.find(data->node_id) != g_network.end());

    // add feed
    g_network[data->node_id]->rappel_add_feed(data->feed_id);
    break;
  }

  case EVT_DRIVER_REMOVE_FEED: {
    //DriverEventData* data = dynamic_cast<DriverEventData*>(event.data);
    assert(false); // not yet implemented -- probably will not be
    break;
  }

  case EVT_DRIVER_PUBLISH: {
    DriverEventData* data = dynamic_cast<DriverEventData*>(event.data);

#ifdef DEBUG
    printf("(driver3 cback) dbg: EVT_DRIVER_PUBLISH %s size=%u\n",
      data->feed_id.str().c_str(), data->update_size);
#endif // DEBUG

    // make sure this node exists in the network
    assert(g_network.find(data->node_id) != g_network.end());

    // publish update
    g_network[data->node_id]->rappel_publish_update(data->update_size);
    break;
  }

  default: {
    assert(false);
  }

  }

  delete(event.data);
  return 0;
}

int node_event_callback(Event& event) {

  Timer* timer = Timer::get_instance();
  static unsigned int last_stats_seq_num = 0;

  // make sure the event is scheduled for APPLICATION
  assert(event.layer == LYR_APPLICATION);

  if (event.flag <= EVT_GLOBAL_BARRIER) {

    switch (event.flag) {

    case EVT_STATS: {

      StatsEventData* data = dynamic_cast<StatsEventData*>(event.data);
      last_stats_seq_num = data->seq_num;

      for (std::map<node_id_t, Node*>::iterator it = g_network.begin();
        it != g_network.end(); it++) {

        node_id_t node_id = it->first;
        Node* node = it->second;

        // if rappel node was online at the beginning of interval -- or is 
        // online now: flush stats. note that we ignore VIVALDI nodes
        if (node_id < FIRST_LANDMARK_NODE_ID
          && (duration_node_online_at(node_id, timer->get_time()
              - globops.sys_stats_interval + 1 * MICRO_SECOND, false) > 0 * MICRO_SECONDS
            || duration_node_online_at(node_id, timer->get_time(), false)) > 0 * MICRO_SECONDS) {

          node->debug(data->seq_num);
        }
      }
      return 0;
    }

    case EVT_FINAL: {

      // increment stats seq num -- hackish
      last_stats_seq_num++;

      for (std::map<node_id_t, Node*>::iterator it = g_network.begin();
        it != g_network.end(); /* it++ */) {

        node_id_t node_id = it->first;
        Node* node = it->second;

        // if rappel node was online at the beginning of interval -- or is 
        // online now: flush stats. note that we ignore VIVALDI nodes
        if (node_id < FIRST_LANDMARK_NODE_ID
          && (duration_node_online_at(node_id, timer->get_time()
              - globops.sys_stats_interval + 1 * MICRO_SECOND, false) > 0 * MICRO_SECONDS
            || duration_node_online_at(node_id, timer->get_time(), false)) > 0 * MICRO_SECONDS) {

          node->debug(last_stats_seq_num);
        }

        delete node;
	std::map<node_id_t, Node*>::iterator it_now = it;
	it++;
        g_network.erase(it_now);
      }
      return 0;
    }

    default: {
      assert(false);
      return 0;
    }

    }
  }

  // make sure the node is actually in the "network"
  assert(g_network.find(event.node_id) != g_network.end());
  
  // deliver the event -- only is node is online
  return g_network[event.node_id]->event_callback(event);
}

/**
 * Receive events from the transport layer
 */
static int node_msg_callback(Message& msg) {
  
  // make sure the event is scheduled for RAPPEL 
  assert(msg.layer == LYR_APPLICATION);
  
  // make sure the node is actually in the "network"
  assert(g_network.find(msg.dst) != g_network.end());
  
  // deliver the message -- only is node is online
  return g_network[msg.dst]->msg_callback(msg);

  return 0;
}

#ifndef IGNORE_ETIENNE_STATS

// UGLY : think of something better like maintaining the list of online
// subscribers at the publisher itself

std::vector<node_id_t> get_online_subscribers_for_feed(const feed_id_t& feed_id) {

  std::vector<node_id_t> online_nodes;

  Timer* timer = Timer::get_instance();

  for (map<node_id_t, Node*>::iterator it = g_network.begin();
    it != g_network.end(); it++) {

    if ((node_id_t) feed_id == it->first) {
      continue;
    }

    Node *node = it->second;

    if (node->is_a_rappel_node() &&
      node->rappel_is_subscribed_to(feed_id) &&
      duration_node_online_at(it->first, timer->get_time(), true) > 0 * MICRO_SECONDS) {

      online_nodes.push_back(it->first);
    }
  }

/*
  printf("[DBG] feed=%s has %u subscribers online at time=%s\n",
    feed_id.str().c_str(), online_nodes.size(), timer->get_time_str().c_str());
*/
  return online_nodes;
} 

static unsigned int output_update_stats(bool final) {

  Timer* timer = Timer::get_instance();
  unsigned int updates_flushed = 0;
  static std::set<std::pair<feed_id_t, unsigned int> > already_flushed;

  // prepare -- one time cost
  string sql_insert_update("INSERT INTO node_per_update("
    "feed_id, update_num, sync_emission_time, node_id, "

    "first_reception_delay, first_reception_is_pulled, ideal_reception_delay, "
    "last_reception_delay, num_receptions_pulled, num_receptions_pushed, "
    "first_reception_height) "

    "VALUES (?, ?, ?, ?, "
    "?, ?, ?, ?, ?, ?, ?);");

  assert(sqlite3_prepare(stats_db, sql_insert_update.c_str(),
    -1, &g_stats_insert_update_stmt, NULL) == SQLITE_OK);

  // loop for each publisher feed
  for (map<feed_id_t, stats_feed_t>::iterator it = feed_stats.begin();
    it != feed_stats.end(); it++) {

    feed_id_t curr_feed_id = it->first;
    stats_feed_t* curr_feed_stats = &(it->second);
    assert(curr_feed_stats == &feed_stats[it->first]);

    // loop for each update
    for (map<unsigned int, stats_feed_update_t>::iterator
      it_update_stats = curr_feed_stats->updates.begin();  
      it_update_stats != curr_feed_stats->updates.end(); it_update_stats++) {

      unsigned int update_num = it_update_stats->first;
      //printf("[dbg] update=%u out of %u total\n", update_num,
      //  curr_feed_stats->updates.size());
      stats_feed_update_t* curr_update_stats = &(it_update_stats->second);

      if (!final && curr_update_stats->sync_emission_time
        + DEF_CLEANUP_BUFFER > timer->get_time()) {

        //printf("[DBG] breaking out #1\n");
        continue; // can't write out the stats for this update just yet
        // wait a few minutes
      }
 
      if (already_flushed.find(make_pair(curr_feed_id, update_num))
        != already_flushed.end()) {

        if (curr_update_stats->recv_node_stats.size() > 0) {
          printf("[DBG] stats for feed=%s, update=%u already flushed to db, "
            "however %u items still remain in memory\n",
             curr_feed_id.str().c_str(), update_num,
             curr_update_stats->recv_node_stats.size());
          assert(false);
        }
        //printf("[DBG] breaking out #2\n");
        continue;
      }

      // loop for each designated subscriber (i.e., receiver node)
      for (map<node_id_t, stats_node_per_update_t>::iterator
        it_node_stats = curr_update_stats->recv_node_stats.begin();
        it_node_stats != curr_update_stats->recv_node_stats.end();
        /* it_node_stats++ */) {

        node_id_t curr_node_id = it_node_stats->first;
        stats_node_per_update_t curr_node_stats = it_node_stats->second;

        assert(sqlite3_bind_int(g_stats_insert_update_stmt, 1,
          curr_feed_id.get_address()) == SQLITE_OK);
        assert(sqlite3_bind_int(g_stats_insert_update_stmt, 2,
          update_num) == SQLITE_OK);
        assert(sqlite3_bind_int64(g_stats_insert_update_stmt, 3,
          curr_update_stats->sync_emission_time) == SQLITE_OK);
        assert(sqlite3_bind_int(g_stats_insert_update_stmt, 4,
          curr_node_id.get_address()) == SQLITE_OK);

        assert(sqlite3_bind_int64(g_stats_insert_update_stmt, 5,
          curr_node_stats.first_reception_delay) == SQLITE_OK);
        assert(sqlite3_bind_int(g_stats_insert_update_stmt, 6,
          (curr_node_stats.first_reception_is_pulled ? 1 : 0)) == SQLITE_OK);
        assert(sqlite3_bind_int64(g_stats_insert_update_stmt, 7,
          curr_node_stats.ideal_reception_delay) == SQLITE_OK);
        assert(sqlite3_bind_int64(g_stats_insert_update_stmt, 8,
          curr_node_stats.last_reception_delay) == SQLITE_OK);
        assert(sqlite3_bind_int(g_stats_insert_update_stmt, 9,
          curr_node_stats.num_receptions_pulled) == SQLITE_OK);
        assert(sqlite3_bind_int(g_stats_insert_update_stmt, 10,
          curr_node_stats.num_receptions_pushed) == SQLITE_OK);
        assert(sqlite3_bind_int(g_stats_insert_update_stmt, 11,
          curr_node_stats.first_reception_height) == SQLITE_OK);

        assert(sqlite3_step(g_stats_insert_update_stmt) == SQLITE_DONE);
        sqlite3_reset(g_stats_insert_update_stmt);

        // delete the designated subscriber
        map<node_id_t, stats_node_per_update_t>::iterator
          it_node_stats_now = it_node_stats;
        it_node_stats++;
        unsigned int old_size = curr_update_stats->recv_node_stats.size();
        curr_update_stats->recv_node_stats.erase(it_node_stats_now);
        unsigned int new_size = curr_update_stats->recv_node_stats.size();
        assert(old_size == new_size + 1);
      }

      // remember this
      //printf("[DBG] just flushed stats for update=%u to db\n", update_num);
      already_flushed.insert(make_pair(curr_feed_id, update_num));
      updates_flushed++;
      assert(curr_update_stats->recv_node_stats.size() == 0);
    }
  }

  return updates_flushed;
}

static Clock duration_node_online_at(const node_id_t& node_id, const Clock& time,
  bool with_padding) {

/*
  Timer* timer = Timer::get_instance();
  printf("[DBG] duration_node_online_at(): node=%s; time=%s\n",
    node_id.str().c_str(), timer->get_time_str(time).c_str());
*/

  assert(g_churn_sessions.find(node_id) != g_churn_sessions.end());

  for (vector<pair<Clock, Clock> >::const_iterator
    it = g_churn_sessions[node_id].begin();
    it != g_churn_sessions[node_id].end(); it++) {

    Clock session_start = it->first + (with_padding ? SESSION_PAD_TIME : 0);
    Clock session_end = it->second - (with_padding ? SESSION_PAD_TIME : 0);

/*
    printf("[DBG] session start=%s end=%s\n",
      timer->get_time_str(session_start).c_str(),
      timer->get_time_str(session_end).c_str());
*/

    if (time < session_start) {
      return -1 * MICRO_SECONDS;
    }

    if (time >= session_start && time <= session_end) {
      return (time - session_start);
    }
  }

  return -1 * MICRO_SECONDS;
}
#endif // !IGNORE_ETIENNE_STATS
