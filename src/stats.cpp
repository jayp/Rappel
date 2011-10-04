#define _STATS_CPP

#include <map>
#include <sqlite3.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "stats.h"
#include "config.h"

using namespace std;

// the actual stats collection variables
std::map<feed_id_t, stats_feed_t> feed_stats;
sqlite3* stats_db;

// global constants
#if COMPILE_FOR == SIMULATOR
static const char* G_DB_FILE_NAME = "stats_v2_%s_%u.db";
static const char* G_TMP_DB_FILE_NAME = "/tmp/stats_v2_%s_%u.db";
#elif COMPILE_FOR == NETWORK
static const char* G_DB_FILE_NAME = "stats_v2_%s.db";
static const char* G_TMP_DB_FILE_NAME = "/tmp/stats_v2_%s.db";
#endif

// global variables
static char g_db_file_name[MAX_STRING_LENGTH];
static char g_tmp_db_file_name[MAX_STRING_LENGTH];

int my_sqlite3_callback(void *not_used, int argc, char **argv, char **col_name) {

  printf("sqlite3 stats callback\n");

  // this shouldn't happen
  assert(false);

  for (int i=0; i < argc; i++){
    printf("%s = %s\n", col_name[i], argv[i] ? argv[i] : "NULL");
  }
  printf("\n");

  return 0;
}

void init_stats_db() {
  
  assert(globops.is_initialized());

#if COMPILE_FOR == SIMULATOR
  sprintf(g_db_file_name, G_DB_FILE_NAME, globops.experiment.c_str(),
    globops.sys_seed);
  sprintf(g_tmp_db_file_name, G_TMP_DB_FILE_NAME, globops.experiment.c_str(), 
    globops.sys_seed);
#elif COMPILE_FOR == NETWORK
  sprintf(g_db_file_name, G_DB_FILE_NAME, globops.self_name.c_str());
  sprintf(g_tmp_db_file_name, G_TMP_DB_FILE_NAME, 
    globops.self_name.c_str());
#endif

  if (access(g_tmp_db_file_name, F_OK) == 0) {
    std::cerr << "temporary stats_db file exists (" << g_tmp_db_file_name
      << "). removing..." << std::endl;
    // delete the file file
    char rm_cmd[MAX_STRING_LENGTH];
    sprintf(rm_cmd, "rm %s", g_tmp_db_file_name);
    system(rm_cmd);
  }
  assert(access(g_tmp_db_file_name, F_OK) != 0);

  string db_file = globops.experiment + "/" + g_db_file_name;

  if (access(db_file.c_str(), F_OK) == 0) {
    // move the file
    time_t now = time(NULL);
    char mv_cmd[MAX_STRING_LENGTH];
    sprintf(mv_cmd, "mv %s %s.%u", db_file.c_str(), db_file.c_str(), (unsigned int) now);
    printf("old stats db file exists. moving via cmd: %s\n", mv_cmd);
    system(mv_cmd);
  }
  assert(access(db_file.c_str(), F_OK) != 0);

  int rc;
  char *err_msg;

  rc = sqlite3_open(g_tmp_db_file_name, &stats_db);

  if (rc) {
    fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(stats_db));
    sqlite3_close(stats_db);
    exit(1);
  }

  string sql_pragma_cache_size("PRAGMA cache_size = 100000");

  rc = sqlite3_exec(stats_db, sql_pragma_cache_size.c_str(),
    my_sqlite3_callback, 0, &err_msg);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  }

  string sql_pragma_synchronous("PRAGMA synchronous = OFF");

  rc = sqlite3_exec(stats_db, sql_pragma_synchronous.c_str(),
    my_sqlite3_callback, 0, &err_msg);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  }

  string sql_pragma_temp_store("PRAGMA temp_store = MEMORY");

  rc = sqlite3_exec(stats_db, sql_pragma_temp_store.c_str(),
    my_sqlite3_callback, 0, &err_msg);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  }

  string sql_create_node_per_cycle_tbl("CREATE TABLE node_per_cycle("
#if COMPILE_FOR == SIMULATOR
    "node_id INTEGER, "
#elif COMPILE_FOR == NETWORK
    "node_id TEXT, "
#endif
    "seq_num INTEGER, "
    "sync_time INTEGER, "

    // friend message (and stuff)
    "friend_set_utility REAL, "
    "num_friends INTEGER, "
    "num_fans INTEGER, "
    "num_candidates INTEGER, "

    "msgs_in_friend_req INTEGER, "
    "msgs_in_friend_reply_accepted INTEGER, " 
    "msgs_in_friend_reply_rejected INTEGER, " 
    "msgs_in_friend_deletion INTEGER, " 
    "msgs_in_fan_deletion INTEGER, " 

    "msgs_out_friend_req INTEGER, "
    "msgs_out_friend_reply_accepted INTEGER, " 
    "msgs_out_friend_reply_rejected INTEGER, " 
    "msgs_out_friend_deletion INTEGER, " 
    "msgs_out_fan_deletion INTEGER, " 

    // tree construction messages
    "msgs_in_join_req INTEGER, "
    "msgs_in_lost_parent_rejoin_req INTEGER, "
    "msgs_in_change_parent_rejoin_req INTEGER, "
    "msgs_in_periodic_rejoin_req INTEGER, "
    "msgs_in_redirected_periodic_rejoin_req INTEGER, "
    "msgs_in_join_reply_ok INTEGER, "
    "msgs_in_join_reply_deny INTEGER, "
    "msgs_in_join_reply_fwd INTEGER, "

    "msgs_out_join_req INTEGER, "
    "msgs_out_lost_parent_rejoin_req INTEGER, "
    "msgs_out_change_parent_rejoin_req INTEGER, "
    "msgs_out_periodic_rejoin_req INTEGER, "
    "msgs_out_join_reply_ok INTEGER, "
    "msgs_out_join_reply_deny INTEGER, "
    "msgs_out_join_reply_fwd INTEGER, "

    "msgs_in_change_parent INTEGER, "
    "msgs_in_no_longer_your_child INTEGER, "
    "msgs_in_flush_ancestry INTEGER, "

    "msgs_out_change_parent INTEGER, "
    "msgs_out_no_longer_your_child INTEGER, "
    "msgs_out_flush_ancestry INTEGER, "

    "msgs_in_bloom_req INTEGER, "
    "msgs_in_bloom_reply INTEGER, "

    "msgs_out_bloom_req INTEGER, " 
    "msgs_out_bloom_reply INTEGER, "

    // ping messages
    "msgs_in_ping_req INTEGER, "
    "msgs_in_ping_reply INTEGER, "

    "msgs_out_ping_req INTEGER, "
    "msgs_out_ping_reply INTEGER, "

    // dissemination messages
    "msgs_in_dissemination INTEGER, " 
    "msgs_in_dissemination_pull_req INTEGER, "

    "msgs_out_dissemination INTEGER, " 
    "msgs_out_dissemination_pull_req INTEGER"

#if COMPILE_FOR == NETWORK
    ", local_time INTEGER"
#endif // COMPILE_FOR == NETWORK

    ")");

  rc = sqlite3_exec(stats_db, sql_create_node_per_cycle_tbl.c_str(),
    my_sqlite3_callback, 0, &err_msg);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  }

  ///////////////////////////////////////////////
  // friends
  string sql_create_node_friends_per_cycle_tbl("CREATE TABLE node_friends_per_cycle("
#if COMPILE_FOR == SIMULATOR
    "node_id INTEGER, "
#elif COMPILE_FOR == NETWORK
    "node_id TEXT, "
#endif
    "seq_num INTEGER, "
    "sync_time INTEGER, "
#if COMPILE_FOR == SIMULATOR
    "friend_id INTEGER"
#elif COMPILE_FOR == NETWORK
    "friend_id TEXT, "
    "local_time INTEGER"
#endif // COMPILE_FOR == NETWORK
    ")");

  rc = sqlite3_exec(stats_db, sql_create_node_friends_per_cycle_tbl.c_str(),
    my_sqlite3_callback, 0, &err_msg);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  }

  ///////////////////////////////////////////////
  // candidates
  string sql_create_node_candidates_per_cycle_tbl("CREATE TABLE node_candidates_per_cycle("
#if COMPILE_FOR == SIMULATOR
    "node_id INTEGER, "
#elif COMPILE_FOR == NETWORK
    "node_id TEXT, "
#endif
    "seq_num INTEGER, "
    "sync_time INTEGER, "
    "candidate_id INTEGER"
#if COMPILE_FOR == NETWORK
    ", local_time INTEGER"
#endif // COMPILE_FOR == NETWORK
    ")");

  rc = sqlite3_exec(stats_db, sql_create_node_candidates_per_cycle_tbl.c_str(),
    my_sqlite3_callback, 0, &err_msg);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  }

  ///////////////////////////////////////////////
  // fans
  string sql_create_node_fans_per_cycle_tbl("CREATE TABLE node_fans_per_cycle("
#if COMPILE_FOR == SIMULATOR
    "node_id INTEGER, "
#elif COMPILE_FOR == NETWORK
    "node_id TEXT, "
#endif
    "seq_num INTEGER, "
    "sync_time INTEGER, "
#if COMPILE_FOR == SIMULATOR
    "fan_id INTEGER"
#elif COMPILE_FOR == NETWORK
    "fan_id TEXT, "
    "local_time INTEGER"
#endif // COMPILE_FOR == NETWORK
    ")");

  rc = sqlite3_exec(stats_db, sql_create_node_fans_per_cycle_tbl.c_str(),
    my_sqlite3_callback, 0, &err_msg);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  }

  ///////////////////////////////////////////////
  // parents
  string sql_create_node_parent_per_cycle_tbl("CREATE TABLE node_parent_per_cycle("
#if COMPILE_FOR == SIMULATOR
    "node_id INTEGER, "
#elif COMPILE_FOR == NETWORK
    "node_id TEXT, "
#endif
    "seq_num INTEGER, "
    "sync_time INTEGER, "
#if COMPILE_FOR == SIMULATOR
    "feed_id INTEGER, "
    "parent_id INTEGER, "
#elif COMPILE_FOR == NETWORK
    "feed_id TEXT, "
    "parent_id TEXT, "
#endif
    "tree_height INTEGER, "
    "num_children INTEGER, "
    "direct_cost REAL, "
    "path_cost REAL"
#if COMPILE_FOR == NETWORK
    ", local_time INTEGER"
#endif // COMPILE_FOR == NETWORK
    ")");

  rc = sqlite3_exec(stats_db, sql_create_node_parent_per_cycle_tbl.c_str(),
    my_sqlite3_callback, 0, &err_msg);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  }

  ///////////////////////////////////////////////
  // children
  string sql_create_node_children_per_cycle_tbl("CREATE TABLE node_children_per_cycle("
#if COMPILE_FOR == SIMULATOR
    "node_id INTEGER, "
#elif COMPILE_FOR == NETWORK
    "node_id TEXT, "
#endif // COMPILE_FOR
    "seq_num INTEGER, "
    "sync_time INTEGER, "
#if COMPILE_FOR == SIMULATOR
    "feed_id INTEGER, "
    "child_id INTEGER"
#elif COMPILE_FOR == NETWORK
    "feed_id TEXT, "
    "child_id TEXT, "
    "local_time INTEGER"
#endif // COMPILE_FOR == NETWORK
    ")");

   
  rc = sqlite3_exec(stats_db, sql_create_node_children_per_cycle_tbl.c_str(),
    my_sqlite3_callback, 0, &err_msg);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  }

  ///////////////////////////////////////////////
  // updates
  string sql_create_node_per_update_tbl("CREATE TABLE node_per_update("
#if COMPILE_FOR == SIMULATOR
    "feed_id INTEGER, "
#elif COMPILE_FOR == NETWORK
    "feed_id TEXT, "
#endif
    "update_num INTEGER, "
    "sync_emission_time INTEGER, "

#if COMPILE_FOR == SIMULATOR
    "node_id INTEGER, "
#elif COMPILE_FOR == NETWORK
    "node_id TEXT, "
#endif
    "is_online INTEGER, "
    "first_reception_delay INTEGER, "
    "first_reception_is_pulled INTEGER, "
    "ideal_reception_delay INTEGER, "
    "last_reception_delay INTEGER, "
    "num_receptions_pulled INTEGER, "
    "num_receptions_pushed INTEGER, "
    "first_reception_height INTEGER)");

  rc = sqlite3_exec(stats_db, sql_create_node_per_update_tbl.c_str(),
    my_sqlite3_callback, 0, &err_msg);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  }

  ///////////////////////////////////////////////
  // coordinates
  string sql_create_coordinates_tbl("CREATE TABLE coordinates("
#if COMPILE_FOR == SIMULATOR
    "node_id INTEGER, "
#elif COMPILE_FOR == NETWORK
    "node_id TEXT, "
#endif
    "sync_time INTEGER, "
    "version INTEGER, "
    "app_coords_1 REAL, "
    "app_coords_2 REAL, "
    "app_coords_3 REAL, "
    "app_coords_4 REAL, "
    "app_coords_5 REAL"
#if COMPILE_FOR == NETWORK
    ", local_time INTEGER"
#endif // COMPILE_FOR == NETWORK
    ")");

  rc = sqlite3_exec(stats_db, sql_create_coordinates_tbl.c_str(),
    my_sqlite3_callback, 0, &err_msg);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  }

#if COMPILE_FOR == SIMULATOR
  ///////////////////////////////////////////////
  // system
  string sql_create_system_per_cycle_tbl("CREATE TABLE system_per_cycle("
    "seq_num INTEGER, "
    "sync_time INTEGER, "
    "total_nodes INTEGER, "
    "node_joins INTEGER, "
    "node_leaves INTEGER)");

  rc = sqlite3_exec(stats_db, sql_create_system_per_cycle_tbl.c_str(),
    my_sqlite3_callback, 0, &err_msg);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  }
#endif // COMPLILE_FOR == SIMULATOR

  //////////////////////////////////////////////////
  // global defs

  string sql_create_global_defs_tbl("CREATE TABLE global_defs("
    "interval_len INTEGER)");

  rc = sqlite3_exec(stats_db, sql_create_global_defs_tbl.c_str(),
    my_sqlite3_callback, 0, &err_msg);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  }

  // prepare -- one time cost
  string sql_insert_global_defs("INSERT INTO global_defs("
    "interval_len) "
    "VALUES(?);");

  sqlite3_stmt* stats_insert_global_defs_stmt;
  assert(sqlite3_prepare(stats_db, sql_insert_global_defs.c_str(),
    -1, &stats_insert_global_defs_stmt, NULL) == SQLITE_OK);

  assert(sqlite3_bind_int64(stats_insert_global_defs_stmt, 1,
    globops.sys_stats_interval) == SQLITE_OK);

  assert(sqlite3_step(stats_insert_global_defs_stmt) == SQLITE_DONE);
  sqlite3_reset(stats_insert_global_defs_stmt);
}

void final_stats_db() {

  // finalize prepared statements
  //sqlite3_finalize(stats_insert_friend_stmt);

  // close db
  sqlite3_close(stats_db);

  string slash("/");
  string db_file = globops.experiment + slash + g_db_file_name;

  // move tmp file to "permanent" location
  char mv_cmd[MAX_STRING_LENGTH];
  sprintf(mv_cmd, "mv %s %s", g_tmp_db_file_name, db_file.c_str());
  printf("old stats db file exists. moving via cmd: %s\n", mv_cmd);
  system(mv_cmd);

  // this file should no longer exist
  assert(access(db_file.c_str(), F_OK) == 0);
}
