#ifndef _CONFIG_H
#define _CONFIG_H

struct Options {

  Options()
    : initialized(false) {

    // by default, Options is not initialized
  }

  bool is_initialized() const {
    return initialized;
  }

  // initialization flag
  bool initialized;

  // current experiment
  std::string experiment;
#if COMPILE_FOR == NETWORK
  std::string self_name;
  std::string self_role;
  node_id_t self_node;
  unsigned int ctrl_port;
#endif // COMPILE_FOR == NETWORK

  // the sysystem wide options
  unsigned int sys_seed;
  Clock sys_termination_time;
  Clock sys_stats_interval;

  // for AS Topology
  std::string as_zhang_topology_date;
  Clock as_delay_min; 
  Clock as_delay_max;
  unsigned int as_cache_size;

  // for Topology (Host Topology)
  Clock isp_delay_min;
  Clock isp_delay_max;

  // for transport
  Clock transport_conn_timeout;
  std::string transport_keep_track_of_traffic;

  // for Network Coordinates
  unsigned int nc_dims;
  double nc_lambda_init;
  double nc_lambda_min;
  double nc_lambda_decr;
  double nc_sys_delta;

  // for vivaldi
  unsigned int vivaldi_num_samples;
  Clock vivaldi_ping_interval;
  unsigned int sim_vivaldi_landmark_count;
  Clock vivaldi_reply_timeout;

  // for time sync
  Clock sync_init_interval;
  Clock sync_post_interval;
  node_id_t sync_node;
  node_id_t first_landmark;

  // for bootstrapping: time sync and network coordinate convergence
  unsigned int bootstrap_vivaldi_pongs_min;
  unsigned int bootstrap_sync_min;
  Clock bootstrap_vivaldi_timeout;
  Clock sim_bootstrap_time; // applicable only to simulator

  // bloom filters
  unsigned int bloom_size;
  unsigned int bloom_hash_funcs;

  // for Rappel
  Clock rappel_garbage_collect_interval;
 
  Clock rappel_reply_timeout;
  Clock rappel_heart_beat_interval;
  Clock rappel_keep_alive_interval;

  unsigned int rappel_num_friends;
  unsigned int rappel_num_fans;
  unsigned int rappel_num_candidates;
  double rappel_friend_set_utility_k;
  double rappel_friend_set_utility_delta;
  std::string rappel_friend_set_utility_depends_on;

  Clock rappel_audit_friend_interval;
  Clock rappel_audit_friend_interval_rapid;

  unsigned int rappel_num_feed_children;
  unsigned int rappel_num_feed_backups;
  unsigned int rappel_feed_updates_cache;

  unsigned int rappel_gradual_feed_joins;

#if COMPILE_FOR == NETWORK
  unsigned int rappel_feed_rtt_samples;
  Clock rappel_feed_rtt_interval;
#endif // COMPILE_FOR == NETWORK

  Clock rappel_feed_rejoin_interval;
  Clock rappel_feed_join_seperation_min;
};

// function
void load_config(int argc, char** argv);
Clock string_to_clock(const std::string& str);

#endif
