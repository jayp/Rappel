#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <string>
#include <cstring>
#include <vector>
#include <map>
#include <set>
#include <utility>
#include <algorithm>
#include "boost/random.hpp"
#include "rappel_common.h"
#include "timer.h"
#include "transport.h"
#include "node.h"
#include "stats.h"

using namespace std;

// global variables
static Node* g_node;
static const char* g_actions_file_loc_tmpl = "%s/actions/%s.data";
static const char* g_publishers_file_loc_tmpl = "%s/publishers.data";
static std::vector<std::string> g_action_cmds;
static std::vector<std::pair<Clock, Clock> > g_sync_sessions;

/** This is a generic class for all driver events. It is not specialized
 * per-event as for RAPPEL (an optimization we can perform later, time
 * permissing)
 */
class DriverEventData: public EventData {
public:
  feed_id_t feed_id;
  size_t update_size;
};

// function prototypes
static int driver_event_callback(Event& event);
static int node_event_callback(Event& event);
static int node_app_msg_callback(Message& msg);
static int node_ctrl_msg_callback(Message& msg);
static void output_stats_files();
static bool is_node_online_at(const Clock& sync_publish_time);

int main(int argc, char** argv) {

  load_config(argc, argv);
  assert(globops.is_initialized());

  if (globops.self_role == "rappel") {
    init_stats_db();
  }

  // initialize timer
  Timer* timer = Timer::get_instance();
  assert(timer != NULL);

  // initialize transport layer
  Transport* transport = Transport::get_instance();
  assert(transport != NULL);
  
  // register driver callback
  timer->register_callback(LYR_DRIVER, driver_event_callback);

  // register ctrl callback
  transport->register_callback(LYR_CONTROL, node_ctrl_msg_callback);

  // register rappel callback(s) on behalf of the rappel node
  timer->register_callback(LYR_APPLICATION, node_event_callback);
  transport->register_callback(LYR_APPLICATION, node_app_msg_callback);

  // buffer string
  char line[MAX_STRING_LENGTH];

  if (globops.self_role == "rappel") { // rappel node

    // read data file for the target node
    char file_loc[MAX_STRING_LENGTH];
    snprintf(file_loc, MAX_STRING_LENGTH, g_actions_file_loc_tmpl,
      globops.experiment.c_str(), globops.self_name.c_str());
    FILE *file = fopen(file_loc, "r");
    assert (file != NULL); 
  
    assert(fgets(line, MAX_STRING_LENGTH, file) != NULL);
  
    char is_publisher;
    assert(sscanf(line, "%c", &is_publisher) == 1);

    printf("DBG: RAPPEL host %s publisher=%c\n", globops.self_node.str().c_str(),
      is_publisher);
  
    // start server
    transport->start_server();
  
    // initialize local node
    g_node = new Node(globops.self_node);
    assert(g_node != NULL);
    g_node->add_landmark(globops.first_landmark);
    g_node->rappel_init(is_publisher == 'y' || is_publisher == 'Y');
   
    // read the rest of the action file
    g_action_cmds.clear();
    while (fgets(line, MAX_STRING_LENGTH, file) != NULL) {

      g_action_cmds.push_back(std::string(line));
    }

    // close the file -- be nice
    fclose(file);
 
  } else if (globops.self_role == "vivaldi") { // vivaldi node

    printf("DBG: VIVALDI host %s\n", globops.self_node.str().c_str());

    // start server
    transport->start_server();

    // initialize local node
    g_node = new Node(globops.self_node);
    assert(g_node != NULL);
    g_node->add_landmark(globops.first_landmark);
    g_node->vivaldi_init();

    // sync node should output its current list of landmarks periodically
    if (globops.self_name == "first_landmark"
      || globops.self_name == "sync_server") {

      // schedule debug events
      StatsEventData* data = new StatsEventData;
      data->seq_num = 1;
      data->start_sync_time = timer->get_sync_time();

      Event evt;
      evt.layer = LYR_DRIVER;
      evt.node_id = globops.self_node;
      evt.flag = EVT_STATS;
      evt.data = data;
      evt.whence = globops.sys_stats_interval;
      timer->schedule_event(evt);
    }

/*
    // development debug: print out the serialization of UDP objects
    Event evt;
    evt.node_id = globops.self_node;
    evt.layer = LYR_DRIVER;
    evt.flag = EVT_DRIVER_DEBUG_MISC;
    evt.data = new EventData; // nothing really, 0-byte class
    evt.whence = 15 * SECONDS;

    timer->schedule_event(evt);
*/
  } else {

    assert(false);
  }

  // start the node
  (IOService::get_instance())->run();

  std::cout << "Done with IOService" << std::endl;

#ifndef IGNORE_ETIENNE_STATS
  if (globops.self_role == "rappel") {
    output_stats_files();
    final_stats_db();
  }
#endif

  return 0;
}

int driver_event_callback(Event& event) {

  assert(event.layer == LYR_DRIVER);

  // initialize transport layer
  Transport* transport = Transport::get_instance();
  assert(transport != NULL);
  
  switch (event.flag) {

  case EVT_STATS: {
    // do nothing
    return 0;
  }

  case EVT_DRIVER_DEBUG_MISC: {

    // send ack msg
    Message msg;
    msg.src = globops.self_node;
    msg.dst = node_id_t(std::string("COOK.cs.UIUC.edu:911"));
    msg.layer = LYR_CONTROL;

    CommandMessageData* data = new CommandMessageData;
    data->debug_flag = true;
    data->self_name = globops.self_name;

    msg.flag = MSG_CMD_SIGNAL_BOOTSTRAPPED;
    msg.data = data;
    transport->send(msg);

    /* CommandMessageData* */ data = new CommandMessageData;
    data->debug_flag = true;
    data->self_name = globops.self_name;

    msg.flag = MSG_CMD_SIGNAL_TRIGGER;
    msg.data = data;
    transport->send(msg);

    /* CommandMessageData* */ data = new CommandMessageData;
    data->debug_flag = true;
    data->self_name = "ONCE UPON A TIME THERE WAS XMAS IN THE TOWN";

    msg.flag = MSG_CMD_SIGNAL_TRIGGERED;
    msg.data = data;
    transport->send(msg);

    /* CommandMessageData* */ data = new CommandMessageData;
    data->debug_flag = true;
    data->self_name = globops.self_name;

    msg.flag = MSG_CMD_SIGNAL_STOP;
    msg.data = data;
    transport->send(msg);

    /* CommandMessageData* */ data = new CommandMessageData;
    data->debug_flag = true;
    data->self_name = "ONE\nTWO\t\t\tTHREE\n\nFOUR";

    msg.flag = MSG_CMD_SIGNAL_STOPPING;
    msg.data = data;
    transport->send(msg);

    Message msg2;
    msg2.src = globops.self_node;
    msg2.dst = node_id_t(std::string("COOK.cs.UIUC.edu:911"));
    msg2.layer = LYR_APPLICATION;

    VivaldiSyncReqData* req_data = new VivaldiSyncReqData;
    req_data->debug_flag = true;
    req_data->request_sent_at = 666 * SECONDS;
    req_data->wait_for_timer = 999999;
    req_data->for_publisher_rtt = false;

    msg2.flag = MSG_VIVALDI_SYNC_REQ;
    msg2.data = req_data;
    transport->send(msg2);

    Coordinate coord1, coord2;
    coord1.init();
    coord2.init();
    coord1.update(coord2, 50 * MILLI_SECONDS, true);

    VivaldiSyncReplyData* reply_data = new VivaldiSyncReplyData;
    reply_data->debug_flag = true;
    //reply_data->sender_coord = coord1;
    reply_data->wait_for_timer = req_data->wait_for_timer;
    reply_data->request_sent_at = req_data->request_sent_at;
    reply_data->reply_sent_at = 777 * SECONDS; //_timer->get_time();
    reply_data->for_publisher_rtt = req_data->for_publisher_rtt;

    msg2.flag = MSG_VIVALDI_SYNC_REPLY;
    msg2.data = reply_data;
    transport->send(msg2);

    break;
  }

  case EVT_DRIVER_LOAD_ACTION_CMDS: {

    // initialize timer
    Timer* timer = Timer::get_instance();
    assert(timer != NULL);

    // read publishers file
    std::map<std::string, node_id_t> publishers;

    // read publishers file
    char file_loc[MAX_STRING_LENGTH];
    snprintf(file_loc, MAX_STRING_LENGTH, g_publishers_file_loc_tmpl,
      globops.experiment.c_str());
    FILE *file = fopen(file_loc, "r");
    assert (file != NULL); 

    char line[MAX_STRING_LENGTH];
    while (fgets(line, MAX_STRING_LENGTH, file) != NULL) {

      char key[MAX_STRING_LENGTH];
      char value[MAX_STRING_LENGTH];

      assert(sscanf(line, "%s %s", key, value) == 2);
      publishers[std::string(key)] = node_id_t(std::string(value));
    }

    // close the file -- be nice
    fclose(file);

    Clock curr_sync_time = timer->get_sync_time();
    Clock sync_online_at = -1 * SECOND; // in sync time

    for (unsigned int i = 0; i < g_action_cmds.size(); i++) {

      std::string cmd = g_action_cmds[i];
      const char* line = cmd.c_str();

      unsigned int time;
      unsigned int old_time = 0;
      char op;
      unsigned int chars_read;
  
      assert(sscanf(line, "%u %c %n", &time, &op, &chars_read) == 2);
      assert(time >= old_time);
      old_time = time;
  
      printf("DBG: curr_line=%s [op=%c]\n", line, op);
  
      switch (op) {
  
      case 'U': { // node up (i.e., bring online)
  
        assert(sync_online_at == -1 * SECOND);
        sync_online_at = curr_sync_time + time * SECONDS;
  
        DriverEventData* evt_data = new DriverEventData;
  
        Event evt;
        evt.node_id = event.node_id;
        evt.layer = LYR_DRIVER,
        evt.flag = EVT_DRIVER_BRING_NODE_ONLINE;
        evt.whence = time * SECONDS;
        evt.data = evt_data;
  
        timer->schedule_event(evt);
  
        break;
      }
  
      case 'D': { // node down (take offline)
  
        assert(sync_online_at != -1 * SECOND);
        g_sync_sessions.push_back(make_pair(sync_online_at, curr_sync_time + time * SECONDS));
        sync_online_at = -1 * SECONDS;
  
        DriverEventData* evt_data = new DriverEventData;
  
        Event evt;
        evt.node_id = event.node_id;
        evt.layer = LYR_DRIVER,
        evt.flag = EVT_DRIVER_TAKE_NODE_OFFLINE;
        evt.whence = time * SECONDS;
        evt.data = evt_data;
  
        timer->schedule_event(evt);
  
        break;
      }
  
      case 'A': { // add feed (subscribe)
  
        assert(sync_online_at != -1 * SECOND);
  
        char feed_addr[MAX_STRING_LENGTH];
        assert(sscanf(line + chars_read, "%s", feed_addr) == 1);
  
        std::string s(feed_addr);
        feed_id_t feed_id(s);

        // change feed (publisher) if __TEMPLATE__ based
        unsigned int len = strlen(feed_addr);
        if (feed_addr[0] == '_' && feed_addr[1] == '_') {
          assert(feed_addr[len - 2] == '_' && feed_addr[len - 1] == '_');
          std::string key(&feed_addr[2], len - 4);
          assert(publishers.find(key) != publishers.end());
          feed_id = publishers[key];
        }
  
        DriverEventData* evt_data = new DriverEventData;
        evt_data->feed_id = feed_id;
  
        Event evt;
        evt.node_id = event.node_id;
        evt.layer = LYR_DRIVER,
        evt.flag = EVT_DRIVER_ADD_FEED;
        evt.whence = time * SECONDS;
        evt.data = evt_data;
  
        timer->schedule_event(evt);
  
        break;
      }
  
      case 'R': { // remove feed (unsubscribe)
  
        assert(false); // not yet implemented
        break;
      }
  
      case 'P': { // publish update -- only if publisher
  
        assert(sync_online_at != -1 * SECOND);
  
        unsigned int update_size;
        assert(sscanf(line + chars_read, "%u", &update_size) == 1);
  
        DriverEventData* evt_data = new DriverEventData;
        evt_data->update_size = update_size;
  
        Event evt;
        evt.node_id = event.node_id;
        evt.layer = LYR_DRIVER,
        evt.flag = EVT_DRIVER_PUBLISH;
        evt.whence = time * SECONDS;
        evt.data = evt_data;
  
        timer->schedule_event(evt);
  
        break;
      }
  
      default: {
        assert(false);
      }
  
      }
    }

    if (sync_online_at != -1 * SECOND) { // node was not scheduled to go offline
      g_sync_sessions.push_back(make_pair(sync_online_at, timer->get_sync_time(365 * DAYS)));
      sync_online_at = -1 * SECONDS;
    }
    g_action_cmds.clear();

    // TEMPORARY
/*
    std::cerr << "session __debug__ info " << std::endl;
    for (unsigned int i = 0; i < g_sync_sessions.size(); i++) {
      std::pair<Clock, Clock> session = g_sync_sessions[i];
      std::cerr << "session #" << i + 1 << ": starts=" << timer->get_time_str(session.first) 
        << "; ends=" << timer->get_time_str(session.second) << std::endl;
    }
*/

    break;
  }

  case EVT_DRIVER_BRING_NODE_ONLINE: {
    //DriverEventData* data = dynamic_cast<DriverEventData*>(event.data);
    printf("(driver cback) dbg: EVT_DRIVER_BRING_NODE_ONLINE\n");
    assert(g_node != NULL);
    assert(g_node->is_a_rappel_node());

    g_node->rappel_bring_online();
    break;
  }

  case EVT_DRIVER_TAKE_NODE_OFFLINE: {
    //DriverEventData* data = dynamic_cast<DriverEventData*>(event.data);
    printf("(driver cback) dbg: EVT_DRIVER_TAKE_NODE_OFFLINE\n");
    assert(g_node != NULL);
    assert(g_node->is_a_rappel_node());

    g_node->rappel_take_offline(false);
    break;
  }
				     
  case EVT_DRIVER_ADD_FEED: {
    DriverEventData* data = dynamic_cast<DriverEventData*>(event.data);
    printf("(driver cback) dbg: EVT_DRIVER_ADD_FEED %s\n", data->feed_id.str().c_str());
    assert(g_node != NULL);
    assert(g_node->is_a_rappel_node());

#ifndef IGNORE_ETIENNE_STATS
    assert(feed_stats.find(data->feed_id) == feed_stats.end());
    stats_feed_t feed_stat;
    feed_stats[data->feed_id] = feed_stat;
#endif //!IGNORE_ETIENNE_STATS
    g_node->rappel_add_feed(data->feed_id);
    break;
  }

  case EVT_DRIVER_REMOVE_FEED: {
    //DriverEventData* data = dynamic_cast<DriverEventData*>(event.data);
    assert(false); // not yet implemented -- probably will not be
    break;
  }

  case EVT_DRIVER_PUBLISH: {
    DriverEventData* data = dynamic_cast<DriverEventData*>(event.data);
    printf("(driver cback) dbg: EVT_DRIVER_PUBLISH %u\n", data->update_size);
    assert(g_node != NULL);
    assert(g_node->is_a_rappel_node());

    g_node->rappel_publish_update(data->update_size);
    break;
  }

  default: {
    assert(false);
  }

  }

  // memory leak? hoorah.
  delete(event.data);
  return 0;
}

int node_ctrl_msg_callback(Message& recv_msg) {

  assert(recv_msg.layer == LYR_CONTROL);
  
  // initialize timer
  Timer* timer = Timer::get_instance();
  assert(timer != NULL);

  // initialize transport layer
  Transport* transport = Transport::get_instance();
  assert(transport != NULL);
  
  switch (recv_msg.flag) {

    case MSG_CMD_SIGNAL_TRIGGER: {

      if (globops.self_role == "vivaldi") {
        std::cerr << "ERROR: recvd TRIGGER for vivaldi node" << std::endl;
        break;
      }

      Clock curr_sync_time = timer->get_sync_time();
      CommandMessageData* req_data = dynamic_cast<CommandMessageData*>(recv_msg.data);
      assert(req_data->sync_action_time > curr_sync_time + 30 * SECONDS); 
        // should be at least 30 seconds in the future

      // send ack msg
      CommandMessageData* data = new CommandMessageData;
      data->self_name = globops.self_name;
      data->sync_action_time = req_data->sync_action_time;

      Message ack_msg;
      ack_msg.src = globops.self_node;
      ack_msg.dst = recv_msg.src;
      ack_msg.layer = LYR_CONTROL;
      ack_msg.flag = MSG_CMD_SIGNAL_TRIGGERED;
      ack_msg.data = data;

      transport->send(ack_msg);

      // action
      Event evt;
      evt.node_id = globops.self_node;
      evt.layer = LYR_DRIVER;
      evt.flag = EVT_DRIVER_LOAD_ACTION_CMDS;
      evt.data = new EventData; // nothing really, 0-byte class
      evt.whence = req_data->sync_action_time - curr_sync_time;

      timer->schedule_event(evt);

      // schedule debug events
      StatsEventData* stats_data = new StatsEventData;
      stats_data->seq_num = 1;
      stats_data->start_sync_time = req_data->sync_action_time;
  
      Event stats_evt;
      stats_evt.layer = LYR_DRIVER;
      stats_evt.node_id = globops.self_node;
      stats_evt.flag = EVT_STATS;
      stats_evt.data = stats_data;
      stats_evt.whence = (req_data->sync_action_time - curr_sync_time) + globops.sys_stats_interval;
      timer->schedule_event(stats_evt);

      Event dbg_evt;
      dbg_evt.layer = LYR_DRIVER;
      dbg_evt.node_id = globops.self_node;
      dbg_evt.flag = EVT_DEBUG;
      dbg_evt.data = new EventData; // whatever, BS
      dbg_evt.whence = 1 * SECOND;
      timer->schedule_event(dbg_evt);

      break;
    }

    case MSG_CMD_SIGNAL_STOP: {

      // send ack msg
      CommandMessageData* data = new CommandMessageData;
      data->self_name = globops.self_name;

      Message ack_msg;
      ack_msg.src = globops.self_node;
      ack_msg.dst = recv_msg.src;
      ack_msg.layer = LYR_CONTROL;
      ack_msg.flag = MSG_CMD_SIGNAL_STOPPING;
      ack_msg.data = data;

      transport->send(ack_msg);

      // action
      transport->stop_server();

      break;
    }

    default: {

      std::cerr << "error: unkown control message received\n";
      break;
    }
  }

  delete(recv_msg.data); // unallocate memory
  return 0;
}

int node_event_callback(Event& event) {
	
  assert(event.layer == LYR_APPLICATION);
  assert(g_node != NULL);

  if (event.flag <= EVT_GLOBAL_BARRIER) {

    switch (event.flag) {

    case EVT_STATS: {

      StatsEventData* data = dynamic_cast<StatsEventData*>(event.data);
      unsigned int seq_num = data->seq_num;
      g_node->debug(seq_num);
      return 0;
    }

    default: {

      assert(false);
      return 0;
    }

    }
  }

  return g_node->event_callback(event);
}

int node_app_msg_callback(Message& msg) {

  assert(msg.layer == LYR_APPLICATION);
  assert(g_node != NULL);
  return g_node->msg_callback(msg);
}

#ifndef IGNORE_ETIENNE_STATS
static void output_stats_files() {

  sqlite3_stmt* stats_insert_update_stmt;

  // prepare -- one time cost
  string sql_insert_update("INSERT INTO node_per_update("
    "feed_id, update_num, sync_emission_time, node_id, "

    "is_online, first_reception_delay, first_reception_is_pulled, "
    "ideal_reception_delay, last_reception_delay, num_receptions_pulled, "
    "num_receptions_pushed, first_reception_height) "

    "VALUES(?, ?, ?, ?, "
    "?, ?, ?, ?, ?, ?, ?, ?);");

  assert(sqlite3_prepare(stats_db, sql_insert_update.c_str(),
    -1, &stats_insert_update_stmt, NULL) == SQLITE_OK);

  node_id_t curr_node_id = g_node->get_node_id();

  for (map<feed_id_t, stats_feed_t>::const_iterator it = feed_stats.begin();
    it != feed_stats.end(); it++) {

    feed_id_t curr_feed_id = it->first;
    stats_feed_t curr_feed_stats = it->second;

    for (map<unsigned int, stats_feed_update_t>::const_iterator
      it_update_stats = curr_feed_stats.updates.begin();  
      it_update_stats != curr_feed_stats.updates.end(); it_update_stats++) {

      unsigned int update_num = it_update_stats->first;
      stats_feed_update_t update_stats = it_update_stats->second;

      assert(sqlite3_bind_text(stats_insert_update_stmt, 1,
        curr_feed_id.str().c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);
      assert(sqlite3_bind_int(stats_insert_update_stmt, 2,
        update_num) == SQLITE_OK);
      assert(sqlite3_bind_int64(stats_insert_update_stmt, 3,
        update_stats.sync_emission_time) == SQLITE_OK);
      assert(sqlite3_bind_text(stats_insert_update_stmt, 4,
        curr_node_id.str().c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);

      assert(sqlite3_bind_int(stats_insert_update_stmt, 5,
        (is_node_online_at(update_stats.sync_emission_time) ? 1: 0)) == SQLITE_OK);
      assert(sqlite3_bind_int64(stats_insert_update_stmt, 6,
        update_stats.details.first_reception_delay) == SQLITE_OK);
      assert(sqlite3_bind_int(stats_insert_update_stmt, 7,
        (update_stats.details.first_reception_is_pulled ? 1 : 0)) == SQLITE_OK);
      assert(sqlite3_bind_int64(stats_insert_update_stmt, 8,
        update_stats.details.ideal_reception_delay) == SQLITE_OK);
      assert(sqlite3_bind_int64(stats_insert_update_stmt, 9,
        update_stats.details.last_reception_delay) == SQLITE_OK);
      assert(sqlite3_bind_int(stats_insert_update_stmt, 10,
        update_stats.details.num_receptions_pulled) == SQLITE_OK);
      assert(sqlite3_bind_int(stats_insert_update_stmt, 11,
        update_stats.details.num_receptions_pushed) == SQLITE_OK);
      assert(sqlite3_bind_int(stats_insert_update_stmt, 12,
        update_stats.details.first_reception_height) == SQLITE_OK);

      assert(sqlite3_step(stats_insert_update_stmt) == SQLITE_DONE);
      sqlite3_reset(stats_insert_update_stmt);
    }
  }
}

static bool is_node_online_at(const Clock& sync_publish_time) {

  for (unsigned int i = 0; i < g_sync_sessions.size(); i++) {

    std::pair<Clock, Clock> session = g_sync_sessions[i];
    Clock session_start = session.first + SESSION_PAD_TIME;
    Clock session_end = session.second - SESSION_PAD_TIME;

    if (sync_publish_time < session_start) {
      return false;
    }

    if (sync_publish_time >= session_start && sync_publish_time <= session_end) {
      return true;
    }
  }

  return false;
}
#endif // !IGNORE_ETIENNE_STATS
