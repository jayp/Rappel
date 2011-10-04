#include <iostream>
#include <fstream>
#include <boost/program_options.hpp>
#include "rappel_common.h"
#include "config.h"

namespace po = boost::program_options;

/**********************************************************************/
// global options: the variable (data holder)
Options globops;

/**********************************************************************/
// typedefs: used to convert data
typedef std::string ClockStr;
/*
class ClockStr {
public:
  ClockStr();
  ClockStr(const char* str) {
  }
  ClockStr(const std::string& str) {
  }
  Clock convert() const {
    return _value;
  }
private:
  Clock _value;
};
*/

/**********************************************************************/
// global options: the defaults

// current experiment
const std::string DEF_EXPERIMENT = "default";
#if COMPILE_FOR == NETWORK
const std::string DEF_SELF_NAME = "first_landmark";
const std::string DEF_SELF_ROLE = "vivaldi";
const std::string DEF_SELF_NODE = "sherpa05.cs.uiuc.edu:1719";
const unsigned int DEF_CTRL_PORT = 38962;
#endif // COMPILE_FOR == NETWORK

// the sysystem wide options
const unsigned int DEF_SYS_SEED = 39;
const ClockStr DEF_SYS_TERMINATION_TIME_STR = "3H";
const ClockStr DEF_SYS_STATS_INTERVAL_STR = "2M";

// for AS Topology
const std::string DEF_AS_ZHANG_TOPOLOGY_DATE = "20060615";
const ClockStr DEF_AS_DELAY_MIN_STR = "8MS";
const ClockStr DEF_AS_DELAY_MAX_STR = "48MS";
const unsigned int DEF_AS_CACHE_SIZE = 100;

// for Topology (Host Topology)
const ClockStr DEF_ISP_DELAY_MIN_STR = "1MS";
const ClockStr DEF_ISP_DELAY_MAX_STR = "5MS";

// for transport
const ClockStr DEF_TRANSPORT_CONN_TIMEOUT_STR = "5M";
const std::string DEF_TRANSPORT_KEEP_TRACK_OF_TRAFFIC = "yes";

// for Network Coordinates
const unsigned int DEF_NC_DIMS = 5;
const double DEF_NC_LAMBDA_INIT = 1.0;
const double DEF_NC_LAMBDA_MIN = 0.025; //0.05;
const double DEF_NC_LAMBDA_DECR = 0.025;
const double DEF_NC_SYS_DELTA = 2.5; // 2.5 millisecs

// for Vivaldi
const unsigned int DEF_VIVALDI_NUM_SAMPLES = 4;
const ClockStr DEF_VIVALDI_PING_INTERVAL_STR = "15S";
const unsigned int DEF_SIM_VIVALDI_LANDMARK_COUNT = 50;
const ClockStr DEF_VIVALDI_REPLY_TIMEOUT_STR = "10S";

// for time sync
const std::string DEF_SYNC_NODE = "sherpa06.cs.uiuc.edu:1719";
const std::string DEF_FIRST_LANDMARK = "sherpa05.cs.uiuc.edu:1719";
const ClockStr DEF_SYNC_INIT_INTERVAL_STR = "20S";
const ClockStr DEF_SYNC_POST_INTERVAL_STR = "1M";

// for Network coordinates convergence: perspective of RAPPEL nodes
const unsigned int DEF_BOOTSTRAP_VIVALDI_PONGS_MIN = 25;
const unsigned int DEF_BOOTSTRAP_SYNC_MIN = 5;
const ClockStr DEF_BOOTSTRAP_VIVALDI_TIMEOUT_STR = "2M";
const ClockStr DEF_SIM_BOOTSTRAP_TIME_STR = "4H";

// bloom filters
const unsigned int DEF_BLOOM_SIZE = 128; // in bytes
const unsigned int DEF_BLOOM_HASH_FUNCS = 3;

// for Rappel
const ClockStr DEF_RAPPEL_GARBAGE_COLLECT_INTERVAL_STR = "15M";
 
const ClockStr DEF_RAPPEL_REPLY_TIMEOUT_STR = "15S";
const ClockStr DEF_RAPPEL_HEART_BEAT_INTERVAL_STR = "30S";
const ClockStr DEF_RAPPEL_KEEP_ALIVE_INTERVAL_STR = "45S";

const unsigned int DEF_RAPPEL_NUM_FRIENDS = 6;
const unsigned int DEF_RAPPEL_NUM_FRIENDS_INCOMING = 2 * DEF_RAPPEL_NUM_FRIENDS;
const unsigned int DEF_RAPPEL_NUM_FRIEND_CANDIDATES = 120;
const double DEF_RAPPEL_FRIEND_SET_UTILITY_K = 1;
const double DEF_RAPPEL_FRIEND_SET_UTILITY_DELTA = 1.01;
const std::string DEF_RAPPEL_FRIEND_SET_UTILITY_DEPENDS_ON = "BOTH_WITH_INTEREST_BONUS";

const ClockStr DEF_RAPPEL_AUDIT_FRIEND_INTERVAL_STR = "30S";
const ClockStr DEF_RAPPEL_AUDIT_FRIEND_INTERVAL_RAPID_STR = "1S";

const unsigned int DEF_RAPPEL_NUM_FEED_CHILDREN = 5;
const unsigned int DEF_RAPPEL_NUM_FEED_BACKUPS = 2;
const unsigned int DEF_RAPPEL_FEED_UPDATES_CACHE = 10;

const ClockStr DEF_RAPPEL_FEED_RTT_INTERVAL_STR = "1M";
const ClockStr DEF_RAPPEL_FEED_REJOIN_INTERVAL_STR = "10M";

const unsigned int DEF_RAPPEL_FEED_RTT_SAMPLES = 5;
const ClockStr DEF_RAPPEL_FEED_JOIN_SEPERATION_MIN_STR = "20S";
const unsigned int DEF_RAPPEL_GRADUAL_FEED_JOINS = 12;

/**********************************************************************/
// global options: read from command line and config file
void load_config(int argc, char** argv) {

  std::string read_self_node;
  ClockStr read_sys_termination_time;
  ClockStr read_sys_stats_interval;
  ClockStr read_as_delay_min;
  ClockStr read_as_delay_max;
  ClockStr read_isp_delay_min;
  ClockStr read_isp_delay_max;
  ClockStr read_transport_conn_timeout;
  ClockStr read_vivaldi_ping_interval;
  ClockStr read_vivaldi_reply_timeout;
  ClockStr read_sync_init_interval;
  ClockStr read_sync_post_interval;
  std::string read_sync_node;
  std::string read_first_landmark;
  ClockStr read_sim_bootstrap_time;
  ClockStr read_bootstrap_vivaldi_timeout;
  ClockStr read_rappel_garbage_collect_interval;
  ClockStr read_rappel_reply_timeout;
  ClockStr read_rappel_heart_beat_interval;
  ClockStr read_rappel_keep_alive_interval;
  ClockStr read_rappel_audit_friend_interval;
  ClockStr read_rappel_audit_friend_interval_rapid;
  ClockStr read_rappel_feed_rtt_interval;
  ClockStr read_rappel_feed_rejoin_interval;
  ClockStr read_rappel_feed_join_seperation_min;

  try {

    // JAY's note: this is really wierd syntax -- due to abuse of operator() overloading
    
    // Declare a group of options that will be 
    // allowed only on command line
    po::options_description cmd_line("Allowed options");
    cmd_line.add_options()
      ("version,v",
        "print current version")

      ("help",
        "produce this help message")

      ("experiment",
        po::value<std::string>(&(globops.experiment))->default_value(DEF_EXPERIMENT),
        "experiment name")

#if COMPILE_FOR == NETWORK
      ("sys_seed",
        po::value<unsigned int>(&(globops.sys_seed))->default_value(DEF_SYS_SEED),
        "seed for the random number generator")

      ("self_name",
        po::value<std::string>(&(globops.self_name))->default_value(DEF_SELF_NAME),
        "the name of this node")

      ("self_role",
        po::value<std::string>(&(globops.self_role))->default_value(DEF_SELF_ROLE),
        "the role of this node")

      ("self_node",
        po::value<std::string>(&read_self_node)->default_value(DEF_SELF_NODE),
        "the ip address of this node")

      ("ctrl_port",
        po::value<unsigned int>(&(globops.ctrl_port))->default_value(DEF_CTRL_PORT),
        "ctrl port (to send BOOTSTRAP ACK msg)")
#endif // COMPILE_FOR == NETWORK
      ;
     
    // Declare a group of options that will be 
    // allowed both on command line and in
    // config file
    po::options_description config("Configuration");
    config.add_options()

#if COMPILE_FOR == SIMULATOR
      ("sys_seed",
        po::value<unsigned int>(&(globops.sys_seed))->default_value(DEF_SYS_SEED),
        "seed for the random number generator")
#endif // COMPILE_FOR == SIMULATOR

      ("sys_termination_time",
        po::value<ClockStr>(&read_sys_termination_time)->default_value(DEF_SYS_TERMINATION_TIME_STR),
        "simulation termination time")

      ("sys_stats_interval",
        po::value<ClockStr>(&read_sys_stats_interval)->default_value(DEF_SYS_STATS_INTERVAL_STR),
        "periodic stats interval")

      ("as_zhang_topology_date",
        po::value<std::string>(&(globops.as_zhang_topology_date))->default_value(DEF_AS_ZHANG_TOPOLOGY_DATE),
        "Zhang's AS topology data set")

      ("as_delay_min",
        po::value<ClockStr>(&read_as_delay_min)->default_value(DEF_AS_DELAY_MIN_STR),
        "minimum delay between two AS networks")

      ("as_delay_max",
        po::value<ClockStr>(&read_as_delay_max)->default_value(DEF_AS_DELAY_MAX_STR),
        "maximum delay between two AS networks")

      ("as_cache_size",
        po::value<unsigned int>(&(globops.as_cache_size))->default_value(DEF_AS_CACHE_SIZE),
        "number of AS routes to store in cache")

      ("isp_delay_min",
        po::value<ClockStr>(&read_isp_delay_min)->default_value(DEF_ISP_DELAY_MIN_STR),
        "minimum delay between an ISP and a end-user")

      ("isp_delay_max",
        po::value<ClockStr>(&read_isp_delay_max)->default_value(DEF_ISP_DELAY_MAX_STR),
        "maximum delay between an ISP and a end-user")

      ("transport_conn_timeout",
        po::value<ClockStr>(&read_transport_conn_timeout)->default_value(DEF_TRANSPORT_CONN_TIMEOUT_STR),
        "connection timeout for tcp")

      ("transport_keep_track_of_traffic",
        po::value<ClockStr>(&(globops.transport_keep_track_of_traffic))->default_value(DEF_TRANSPORT_KEEP_TRACK_OF_TRAFFIC),
        "keep track of traffic")

      ("nc_dims",
        po::value<unsigned int>(&(globops.nc_dims))->default_value(DEF_NC_DIMS),
        "network coordinate dimensions")

      ("nc_lambda_init",
        po::value<double>(&(globops.nc_lambda_init))->default_value(DEF_NC_LAMBDA_INIT),
        "initial value for lambda")

      ("nc_lambda_min",
        po::value<double>(&(globops.nc_lambda_min))->default_value(DEF_NC_LAMBDA_MIN),
        "minimum value for lambda")

      ("nc_lambda_decr",
        po::value<double>(&(globops.nc_lambda_init))->default_value(DEF_NC_LAMBDA_DECR),
        "decrement of lambda with each measurement")

      ("nc_sys_delta",
        po::value<double>(&(globops.nc_sys_delta))->default_value(DEF_NC_SYS_DELTA),
        "delta change in sys-level coords affect application-level coords")

      ("vivaldi_num_samples",
        po::value<unsigned int>(&(globops.vivaldi_num_samples))->default_value(DEF_VIVALDI_NUM_SAMPLES),
        "number of measurement samples to keep per vivaldi landmark")

      ("sim_vivaldi_landmark_count",
        po::value<unsigned int>(&(globops.sim_vivaldi_landmark_count))->default_value(DEF_SIM_VIVALDI_LANDMARK_COUNT),
        "number of landmark nodes (for NC boostrapping)")

      ("vivaldi_ping_interval",
        po::value<ClockStr>(&read_vivaldi_ping_interval)->default_value(DEF_VIVALDI_PING_INTERVAL_STR),
        "time between ping requests for a landmark node")

      ("vivaldi_reply_timeout",
        po::value<ClockStr>(&read_vivaldi_reply_timeout)->default_value(DEF_VIVALDI_REPLY_TIMEOUT_STR),
        "vivaldi ping timeout")

      ("sync_node",
        po::value<std::string>(&read_sync_node)->default_value(DEF_SYNC_NODE),
        "time between sync requests to the sync node")

      ("first_landmark",
        po::value<std::string>(&read_first_landmark)->default_value(DEF_FIRST_LANDMARK),
        "first landmark")

      ("sync_init_interval",
        po::value<ClockStr>(&read_sync_init_interval)->default_value(DEF_SYNC_INIT_INTERVAL_STR),
        "time between sync requests to the sync node (init)")

      ("sync_post_interval",
        po::value<ClockStr>(&read_sync_post_interval)->default_value(DEF_SYNC_POST_INTERVAL_STR),
        "time between sync requests to the sync node (post init)")

      ("sim_bootstrap_time",
        po::value<ClockStr>(&read_sim_bootstrap_time)->default_value(DEF_SIM_BOOTSTRAP_TIME_STR),
        "time until which only landmark nodes are active")

      ("bootstrap_vivaldi_pongs_min",
        po::value<unsigned int>(&(globops.bootstrap_vivaldi_pongs_min))->default_value(DEF_BOOTSTRAP_VIVALDI_PONGS_MIN),
        "minimum pongs from landmark nodes to bootstrap Vivaldi")

      ("bootstrap_sync_min",
        po::value<unsigned int>(&(globops.bootstrap_sync_min))->default_value(DEF_BOOTSTRAP_SYNC_MIN),
        "minimum sync replies from sync node to bootstrap Vivaldi")

      ("bootstrap_vivaldi_timeout",
        po::value<ClockStr>(&read_bootstrap_vivaldi_timeout)->default_value(DEF_BOOTSTRAP_VIVALDI_TIMEOUT_STR),
        "used to limit the bootstrap time (if landmark node reponses are slow)")

      ("bloom_size",
        po::value<unsigned int>(&(globops.bloom_size))->default_value(DEF_BLOOM_SIZE),
        "size of bloom filters (in bytes)")

      ("bloom_hash_funcs",
        po::value<unsigned int>(&(globops.bloom_hash_funcs))->default_value(DEF_BLOOM_HASH_FUNCS),
        "number of hash functions used to compute bloom filter")

      ("rappel_garbage_collect_interval",
        po::value<ClockStr>(&read_rappel_garbage_collect_interval)->default_value(DEF_RAPPEL_GARBAGE_COLLECT_INTERVAL_STR),
        "periodically check node state and/or clean up mess")

      ("rappel_reply_timeout",
        po::value<ClockStr>(&read_rappel_reply_timeout)->default_value(DEF_RAPPEL_REPLY_TIMEOUT_STR),
        "response timeout")

      ("rappel_heart_beat_interval",
        po::value<ClockStr>(&read_rappel_heart_beat_interval)->default_value(DEF_RAPPEL_HEART_BEAT_INTERVAL_STR),
        "interval to send a ping request to peers")

      ("rappel_keep_alive_interval",
        po::value<ClockStr>(&read_rappel_keep_alive_interval)->default_value(DEF_RAPPEL_KEEP_ALIVE_INTERVAL_STR),
        "interval from which each peer must be heard from")

      ("rappel_num_friends",
        po::value<unsigned int>(&(globops.rappel_num_friends))->default_value(DEF_RAPPEL_NUM_FRIENDS),
        "number of friends")

      ("rappel_num_fans",
        po::value<unsigned int>(&(globops.rappel_num_fans))->default_value(DEF_RAPPEL_NUM_FRIENDS_INCOMING),
        "number of fans")

      ("rappel_num_candidates",
        po::value<unsigned int>(&(globops.rappel_num_candidates))->default_value(DEF_RAPPEL_NUM_FRIEND_CANDIDATES),
        "number of candidates")

      ("rappel_friend_set_utility_k",
        po::value<double>(&(globops.rappel_friend_set_utility_k))->default_value(DEF_RAPPEL_FRIEND_SET_UTILITY_K),
        "interest/network proximity caliberator")

      ("rappel_friend_set_utility_delta",
        po::value<double>(&(globops.rappel_friend_set_utility_delta))->default_value(DEF_RAPPEL_FRIEND_SET_UTILITY_DELTA),
        "minimum utility improvement for a candidate to be included in friend set")

      ("rappel_friend_set_utility_depends_on",
        po::value<std::string>(&(globops.rappel_friend_set_utility_depends_on))->default_value(DEF_RAPPEL_FRIEND_SET_UTILITY_DEPENDS_ON),
        "how is friend set utility calculated")

      ("rappel_audit_friend_interval",
        po::value<ClockStr>(&read_rappel_audit_friend_interval)->default_value(DEF_RAPPEL_AUDIT_FRIEND_INTERVAL_STR),
        "periodically test candidates for inclusion into friend set")

      ("rappel_audit_friend_interval_rapid",
        po::value<ClockStr>(&read_rappel_audit_friend_interval_rapid)->default_value(DEF_RAPPEL_AUDIT_FRIEND_INTERVAL_RAPID_STR),
        "periodically test candidates for inclusion into friend set (in rapid mode)")

      ("rappel_num_feed_children",
        po::value<unsigned int>(&(globops.rappel_num_feed_children))->default_value(DEF_RAPPEL_NUM_FEED_CHILDREN),
        "number of children per feed")

      ("rappel_num_feed_backups",
        po::value<unsigned int>(&(globops.rappel_num_feed_backups))->default_value(DEF_RAPPEL_NUM_FEED_BACKUPS),
        "number of backups per feed")

      ("rappel_feed_updates_cache",
        po::value<unsigned int>(&(globops.rappel_feed_updates_cache))->default_value(DEF_RAPPEL_FEED_UPDATES_CACHE),
        "number of last updates to keep in cache")

      ("rappel_gradual_feed_joins",
        po::value<unsigned int>(&(globops.rappel_gradual_feed_joins))->default_value(DEF_RAPPEL_GRADUAL_FEED_JOINS),
        "number of initial feeds to join gradually")

#if COMPILE_FOR == NETWORK
      ("rappel_feed_rtt_samples",
        po::value<unsigned int>(&globops.rappel_feed_rtt_samples)->default_value(DEF_RAPPEL_FEED_RTT_SAMPLES),
        "number of rtt measurements to publisher")

      ("rappel_feed_rtt_interval",
        po::value<ClockStr>(&read_rappel_feed_rtt_interval)->default_value(DEF_RAPPEL_FEED_RTT_INTERVAL_STR),
        "periodically measure rtt to publisher")
#endif // COMPILE_FOR == NETWORK

      ("rappel_feed_rejoin_interval",
        po::value<ClockStr>(&read_rappel_feed_rejoin_interval)->default_value(DEF_RAPPEL_FEED_REJOIN_INTERVAL_STR),
        "periodically rejoin the feed at a random ancestor")

      ("rappel_feed_join_seperation_min",
        po::value<ClockStr>(&read_rappel_feed_join_seperation_min)->default_value(DEF_RAPPEL_FEED_JOIN_SEPERATION_MIN_STR),
        "minimum difference between feed joins (for the first few feeds)")
    ;
  
    po::options_description config_file_options;
    config_file_options.add(config);
  
    po::options_description cmdline_options;
    cmdline_options.add(cmd_line);
  
    po::positional_options_description p;
    p.add("experiment", -1);
    
    po::variables_map vm;
    store(po::command_line_parser(argc, argv).
          options(cmdline_options).positional(p).run(), vm);
    notify(vm);

    if (vm.count("help")) {
        std::cout << cmd_line << std::endl;
        exit(0);
    }
  
    if (vm.count("version")) {
        std::cout << "RAPPEL SIMULATOR, version 1.0\n";
        exit(0);
    }

    if (vm.count("experiment")) {

      std::cout << "Experiment: " << globops.experiment << std::endl;

      std::string config_file = globops.experiment + "/settings.cfg";
      ifstream ifs(config_file.c_str());
      store(parse_config_file(ifs, config_file_options), vm);

      notify(vm);
    }

  } catch(exception& e) {
    std::cout << e.what() << std::endl;
    exit(1);
  }

  // convert ClockStr to Clock
#if COMPILE_FOR == NETWORK
  globops.self_node = node_id_t(read_self_node);
#endif // COMPILE_FOR == NETWORK
  globops.sys_termination_time = string_to_clock(read_sys_termination_time);
  globops.sys_stats_interval = string_to_clock(read_sys_stats_interval);
  globops.as_delay_min = string_to_clock(read_as_delay_min);
  globops.as_delay_max = string_to_clock(read_as_delay_max);
  globops.isp_delay_min = string_to_clock(read_isp_delay_min);
  globops.isp_delay_max = string_to_clock(read_isp_delay_max);
  globops.transport_conn_timeout = string_to_clock(read_transport_conn_timeout);
  globops.vivaldi_ping_interval = string_to_clock(read_vivaldi_ping_interval);
  globops.vivaldi_reply_timeout = string_to_clock(read_vivaldi_reply_timeout);
  globops.sync_init_interval = string_to_clock(read_sync_init_interval);
  globops.sync_post_interval = string_to_clock(read_sync_post_interval);
  globops.sync_node = node_id_t(read_sync_node);
  globops.first_landmark = node_id_t(read_first_landmark);
  globops.sim_bootstrap_time = string_to_clock(read_sim_bootstrap_time);
  globops.bootstrap_vivaldi_timeout = string_to_clock(read_bootstrap_vivaldi_timeout);
  globops.rappel_garbage_collect_interval = string_to_clock(read_rappel_garbage_collect_interval);
  globops.rappel_reply_timeout = string_to_clock(read_rappel_reply_timeout);
  globops.rappel_heart_beat_interval = string_to_clock(read_rappel_heart_beat_interval);
  globops.rappel_keep_alive_interval = string_to_clock(read_rappel_keep_alive_interval);
  globops.rappel_audit_friend_interval = string_to_clock(read_rappel_audit_friend_interval);
  globops.rappel_audit_friend_interval_rapid = string_to_clock(read_rappel_audit_friend_interval_rapid);
#if COMPILE_FOR == NETWORK
  globops.rappel_feed_rtt_interval = string_to_clock(read_rappel_feed_rtt_interval);
#endif // COMPILE_FOR == NETWORK
  globops.rappel_feed_rejoin_interval = string_to_clock(read_rappel_feed_rejoin_interval);
  globops.rappel_feed_join_seperation_min = string_to_clock(read_rappel_feed_join_seperation_min);

  // check for correctness of config values // TODO: more?
  assert(globops.as_delay_max > globops.as_delay_min);
  assert(globops.isp_delay_max > globops.isp_delay_min);
  assert(globops.rappel_keep_alive_interval > globops.rappel_heart_beat_interval);

  if (globops.rappel_friend_set_utility_depends_on != "BOTH_WITH_INTEREST_BONUS"
   && globops.rappel_friend_set_utility_depends_on != "BOTH"
   && globops.rappel_friend_set_utility_depends_on != "INTEREST_ALONE_WITH_BONUS"
   && globops.rappel_friend_set_utility_depends_on != "INTEREST_ALONE"
   && globops.rappel_friend_set_utility_depends_on != "NETWORK_ALONE") {

    std::cerr << "invalid value for rappel_friend_set_utility_depends_on="
      << globops.rappel_friend_set_utility_depends_on << std::endl;
    assert(false);
  }
  
  globops.initialized = true;
}

Clock string_to_clock(const std::string& str) {

  const char* c_str = str.c_str();
  unsigned int value;
  char unit[MAX_STRING_LENGTH];

  assert(sscanf(c_str, "%u%s", &value, unit) == 2);

  if (strcmp(unit, "MS") == 0) {
    return value * MILLI_SECONDS;

  } else if (strcmp(unit, "S") == 0) {
    return value * SECONDS;

  } else if (strcmp(unit, "M") == 0) {
    return value * MINUTES;

  } else if (strcmp(unit, "H") == 0) {
    return value * HOURS;

  } else if (strcmp(unit, "D") == 0) {
    return value * DAYS;

  }

  // unknown format
  assert(false);
  return value;
}
