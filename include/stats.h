#ifndef _STATS_H
#define _STATS_H

#include <vector>
#include <map>
#include <sqlite3.h>
#include "rappel_common.h"
#include "coordinate.h"

struct stats_node_per_cycle_t {

  // friend set values and messages
  unsigned int friend_set_changes; // not actually written to file
  // as it is the same as msgs_in_friend_reply_accepted

  unsigned int msgs_in_friend_req;
  unsigned int msgs_in_friend_reply_accepted; 
  unsigned int msgs_in_friend_reply_rejected; 
  unsigned int msgs_in_friend_deletion; 
  unsigned int msgs_in_fan_deletion; 

  unsigned int msgs_out_friend_req;
  unsigned int msgs_out_friend_reply_accepted; 
  unsigned int msgs_out_friend_reply_rejected; 
  unsigned int msgs_out_friend_deletion; 
  unsigned int msgs_out_fan_deletion; 

  // tree construction messages
  unsigned int msgs_in_join_req;
  unsigned int msgs_in_lost_parent_rejoin_req;
  unsigned int msgs_in_change_parent_rejoin_req;
  unsigned int msgs_in_periodic_rejoin_req;
  unsigned int msgs_in_redirected_periodic_rejoin_req;
    // for v2 -- this value is included in msgs_in_periodic_rejoin_req
  unsigned int msgs_in_join_reply_ok;
  unsigned int msgs_in_join_reply_deny;
  unsigned int msgs_in_join_reply_fwd;

  unsigned int msgs_out_join_req;
  unsigned int msgs_out_lost_parent_rejoin_req;
  unsigned int msgs_out_change_parent_rejoin_req;
  unsigned int msgs_out_periodic_rejoin_req;
  unsigned int msgs_out_join_reply_ok;
  unsigned int msgs_out_join_reply_deny;
  unsigned int msgs_out_join_reply_fwd;

  unsigned int msgs_in_change_parent;
  unsigned int msgs_in_no_longer_your_child;
  unsigned int msgs_in_flush_ancestry;

  unsigned int msgs_out_change_parent;
  unsigned int msgs_out_no_longer_your_child;
  unsigned int msgs_out_flush_ancestry;

  unsigned int msgs_in_bloom_req;
  unsigned int msgs_in_bloom_reply;

  unsigned int msgs_out_bloom_req; 
  unsigned int msgs_out_bloom_reply;

  // ping messages
  unsigned int msgs_in_ping_req;
  unsigned int msgs_in_ping_reply;

  unsigned int msgs_out_ping_req;
  unsigned int msgs_out_ping_reply;

  // dissemination messages
  unsigned int msgs_in_dissemination; 
  unsigned int msgs_in_dissemination_pull_req;

  unsigned int msgs_out_dissemination; 
  unsigned int msgs_out_dissemination_pull_req;
};

////////////////////////////////////////////////////////////

struct stats_node_per_update_t {

  //bool was_online; // was node online
  Clock first_reception_delay; // time of the first reception
  bool first_reception_is_pulled;
  Clock ideal_reception_delay; // direct time to publisher (for stretch)
  Clock last_reception_delay; // time of the last reception
  unsigned int num_receptions_pulled; // num of receptions by pull
  unsigned int num_receptions_pushed; // num of receptions by push
  unsigned int first_reception_height; // ht of the subscriber in tree
};

/**
 * The following is quite convoluted. Good luck deciphering the logic.
 */
struct stats_feed_update_t {

  Clock sync_emission_time; // stats on updates
#if COMPILE_FOR == SIMULATOR
  unsigned int num_subscribers;
  std::map<node_id_t, stats_node_per_update_t> recv_node_stats;
#elif COMPILE_FOR == NETWORK
  stats_node_per_update_t details;
#endif
};

struct stats_feed_t {

  std::map<unsigned int, stats_feed_update_t> updates;  // uint = update_num
};

////////////////////////////////////////////////////////////

struct stats_system_per_cycle_t {

  unsigned int total_nodes; // number of active nodes
  unsigned int node_joins; 
  unsigned int node_leaves; 
};

#ifndef _STATS_CPP
// extern global variables
extern std::map<feed_id_t, stats_feed_t> feed_stats;
extern sqlite3* stats_db;
#endif // _STATS_CPP

// function prototypes
int my_sqlite3_callback(void *not_used, int argc, char **argv, char **col_name);
void init_stats_db();
void final_stats_db();

#endif // _STATS_H
