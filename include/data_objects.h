#ifndef _DATA_OBJECTS_H
#define _DATA_OBJECTS_H

#include <string>
#include <vector>
#include <cmath>
#include <map>
#include "rappel_common.h"
#include "bloom_filter.h"
#include "coordinate.h"

using namespace std;

/*
 * One Layer per protocol
 */
enum Layer {

  LYR_NONE,

  // transport layer,
  LYR_TRANSPORT,

  // application layer
  LYR_APPLICATION,

  // stupid layer
  LYR_CONTROL,

  // not a layer per se, but used as one.
  LYR_DRIVER,
};

enum EventFlag {

  EVT_NONE,
  EVT_INIT, // not used
  EVT_DEBUG,
  EVT_STATS,
  EVT_FINAL, // final cleanup event

  /******* differentiate the global events *****/
  EVT_GLOBAL_BARRIER,

  /******* LYR_TRANSPORT ***********/
  EVT_TRANSPORT_RECV,
  EVT_TRANSPORT_DELAY_SEND,

  /******* LYR_APPLICATION (VIVALDI) ***********/
  EVT_VIVALDI_START,

  // boostrap
  EVT_VIVALDI_NC_BOOTSTRAP_PERIOD_EXPIRED,

  // pings
  EVT_VIVALDI_PERIODIC_PING,
  EVT_VIVALDI_WAIT_AFTER_PING_REQ,
  EVT_VIVALDI_PERIODIC_SYNC,
  EVT_VIVALDI_WAIT_AFTER_SYNC_REQ,

  EVT_VIVALDI_END,

  /******* LYR_APPLICATION (RAPPEL) ***********/
  EVT_RAPPEL_START,

  // garbage collect
  EVT_RAPPEL_PERIODIC_GARBAGE_COLLECT,

  // construct/improve friend set
  EVT_RAPPEL_PERIODIC_AUDIT,
  EVT_RAPPEL_WAIT_AFTER_BLOOM_REQ,
  EVT_RAPPEL_WAIT_AFTER_ADD_FRIEND_REQ,

  // for maintenance (and pings)
  EVT_RAPPEL_PERIODIC_NODE_CHECK,
  EVT_RAPPEL_WAIT_AFTER_PING_REQ,

  // for joins
  EVT_RAPPEL_NEXT_FEED_JOIN,
  EVT_RAPPEL_PERIODIC_FEED_REJOIN,
  EVT_RAPPEL_WAIT_AFTER_FEED_JOIN_REQ,

  // for updates
  EVT_RAPPEL_PERIODIC_FEED_RTT_REQ,
  EVT_RAPPEL_WAIT_AFTER_FEED_UPDATE_PULL_REQ,

  EVT_RAPPEL_END,

  /******* LYR_DRIVER ***********/
  EVT_DRIVER_DEBUG_MISC,
  EVT_DRIVER_LOAD_ACTION_CMDS, // for network driver
  EVT_DRIVER_ADD_NODE,
  EVT_DRIVER_BRING_NODE_ONLINE,
  EVT_DRIVER_TAKE_NODE_OFFLINE,

  EVT_DRIVER_ADD_FEED,
  EVT_DRIVER_REMOVE_FEED, // not yet implemented

  EVT_DRIVER_PUBLISH,
};

enum ProtocolFlag {
  PROTO_UNDEF,
  PROTO_TCP,
  PROTO_UDP,
};

enum MessageFlag {

  MSG_NONE,

#if COMPILE_FOR == NETWORK
  /********** CMD *****************************/
  MSG_CMD_SIGNAL_START = 1000,

  MSG_CMD_SIGNAL_BOOTSTRAPPED,
  MSG_CMD_SIGNAL_TRIGGER,
  MSG_CMD_SIGNAL_TRIGGERED,
  MSG_CMD_SIGNAL_STOP,
  MSG_CMD_SIGNAL_STOPPING,

  MSG_CMD_SIGNAL_END,

#endif // COMPILE_FOR == NETWORK

  /******* LYR_APPLICATION (VIVALDI) ***********/
  MSG_VIVALDI_START = 2000,

  // pings
  MSG_VIVALDI_PING_REQ,
  MSG_VIVALDI_PING_REPLY,
  MSG_VIVALDI_SYNC_REQ,
  MSG_VIVALDI_SYNC_REPLY,

  MSG_VIVALDI_END,

  /******* LYR_APPLICATION (RAPPEL) ***********/
  MSG_RAPPEL_START = 3000,

 // construct/improve friend set
  MSG_RAPPEL_BLOOM_REQ,
  MSG_RAPPEL_BLOOM_REPLY,
  MSG_RAPPEL_ADD_FRIEND_REQ,
  MSG_RAPPEL_REMOVE_FRIEND, // from the perspective of the sender
  MSG_RAPPEL_ADD_FRIEND_REPLY,
  MSG_RAPPEL_REMOVE_FAN, // from the perspective of the sender

  // for pings
  MSG_RAPPEL_PING_REQ,
  MSG_RAPPEL_PING_REPLY,

  // for joins
  MSG_RAPPEL_FEED_JOIN_REQ,
  MSG_RAPPEL_FEED_JOIN_REPLY_DENY,
  MSG_RAPPEL_FEED_JOIN_REPLY_OK,
  MSG_RAPPEL_FEED_JOIN_REPLY_FWD,
  MSG_RAPPEL_FEED_CHANGE_PARENT,
  MSG_RAPPEL_FEED_NO_LONGER_YOUR_CHILD,
  MSG_RAPPEL_FEED_FLUSH_ANCESTRY,

  // for updates
  MSG_RAPPEL_FEED_UPDATE,
  MSG_RAPPEL_FEED_UPDATE_PULL_REQ,

  MSG_RAPPEL_END,
};

enum RappelJoinType {
  INITIAL_JOIN,
  REJOIN_DUE_TO_LOST_PARENT,
  REJOIN_DUE_TO_CHANGE_PARENT,
  PERIODIC_REJOIN,
  REDIRECTED_PERIODIC_REJOIN,
};

enum RoleType {
  ROLE_NONE,
  ROLE_FRIEND,
  ROLE_FEED_PARENT,
  ROLE_FEED_CHILD,
};

/**
 *
 */
class Role {  
public:

  // constructor(s)
  Role() {
    _role_type = ROLE_NONE;
    _feed_id = NO_SUCH_NODE;
  }
  Role(RoleType role_type) {
    assert(role_type == ROLE_FRIEND);
    _role_type = role_type;
    _feed_id = NO_SUCH_NODE;
  }
  Role(RoleType role_type, const feed_id_t& feed_id) {
    _role_type = role_type;
    _feed_id = feed_id;
  }

  // accessors
  RoleType get_role_type() const { 
    assert(_role_type != ROLE_NONE);
    return _role_type; 
  }

  char* get_role_type_str() const {
    assert(_role_type != ROLE_NONE);
    switch (_role_type) {
    case ROLE_FRIEND :
      return "friend" ;
    case ROLE_FEED_PARENT :
      return "parent" ;
    case ROLE_FEED_CHILD :
      return "child" ;
    default :
      assert(false);
      return "ERROR";
    }
  }

  feed_id_t get_feed_id() const {
    assert(_role_type == ROLE_FEED_PARENT || _role_type == ROLE_FEED_CHILD);
    return _feed_id;
  }

  // operator
  bool operator== (const Role& rhs) const {
    if (_role_type == ROLE_FRIEND) {
      return (_role_type == rhs._role_type);
    }
    return (_role_type == rhs._role_type && _feed_id == rhs._feed_id);
  }

private:
  RoleType _role_type;
  feed_id_t _feed_id; // only applies to ROLE_FEED_*

#if COMPILE_FOR == NETWORK
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {

    // serialize local fields
    ar & _role_type;
    ar & _feed_id;
  }
#endif //COMPILE_FOR == NETWORK
};

/**************************************************
                  MESSAGE OBJECTS
***************************************************/

/**
 * Object used to pass parameters between events
 */ 
class MessageData {
public:
  bool debug_flag;

  MessageData(): debug_flag(false) {
  }
  virtual ~MessageData() {} // polymorphic
  virtual ProtocolFlag protocol() const {
    return PROTO_UNDEF;	  
  }

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {

    // serialize local fields
  }
#endif //COMPILE_FOR == NETWORK
};

/**
 * base class for all command messages
 */
class CommandMessageData: public MessageData  {
public:
  std::string self_name;
  Clock sync_action_time;

  virtual ProtocolFlag protocol() const {
    return PROTO_UDP; 
  }
  
#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<MessageData>(*this);

    // serialize local fields
    ar & self_name;
    ar & sync_action_time;
  }
#endif //COMPILE_FOR == NETWORK
};

/**
 * base class for all vivaldi messages
 */
class VivaldiMessageData: public MessageData  {
public:
  virtual ProtocolFlag protocol() const {
    return PROTO_UDP; 
  }
  
#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<MessageData>(*this);

    // serialize local fields
  }
#endif //COMPILE_FOR == NETWORK
};

class VivaldiRequestData: public VivaldiMessageData {
public: 
  Clock request_sent_at;
  event_id_t wait_for_timer;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<VivaldiMessageData>(*this);

    // serialize local fields
    ar & request_sent_at;
    ar & wait_for_timer;
  }
#endif //COMPILE_FOR == NETWORK
};

class VivaldiReplyData: public VivaldiMessageData {
public: 
  Clock request_sent_at;
  event_id_t wait_for_timer;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<VivaldiMessageData>(*this);

    // serialize local fields
    ar & request_sent_at;
    ar & wait_for_timer;
  }
#endif //COMPILE_FOR == NETWORK
};

/**
 * base class for all rappel messages
 */
class RappelMessageData: public MessageData  {
public:
  virtual ProtocolFlag protocol() const {
    return PROTO_TCP;
  }

  Coordinate sender_coord;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<MessageData>(*this);

    //printf("inside seriailization of MessageData\n");

    // serialize local fields
    ar & sender_coord;
  }
#endif //COMPILE_FOR == NETWORK
};

class RappelRequestData: public RappelMessageData {
public: 
  event_id_t wait_for_timer;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelMessageData>(*this);

    // serialize local fields
    ar & wait_for_timer;
  }
#endif //COMPILE_FOR == NETWORK
};

class RappelReplyData: public RappelMessageData {
public: 
  event_id_t wait_for_timer;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelMessageData>(*this);

    //printf("inside seriailization of PappelReplyData\n");

    // serialize local fields
    ar & wait_for_timer;
  }
#endif //COMPILE_FOR == NETWORK
};

/////////////////////////////////////////////
///      VIVALDI MESSAGES          /////////

class VivaldiPingReqData: public VivaldiRequestData {
public:
  bool is_vivaldi_landmark;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<VivaldiRequestData>(*this);

    // serialize local fields
    ar & is_vivaldi_landmark;
  }
#endif // COMPILE_FOR == NETWORK
};

class VivaldiPingReplyData: public VivaldiReplyData {
public:
  Coordinate sender_coord;
  std::vector<std::pair<node_id_t, Clock> > sample_landmarks;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<VivaldiReplyData>(*this);

    // serialize local fields
    ar & sender_coord;
    ar & sample_landmarks;
  }
#endif // COMPILE_FOR == NETWORK
};

class VivaldiSyncReqData: public VivaldiRequestData {
public:
  bool for_publisher_rtt;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<VivaldiRequestData>(*this);

    // serialize local fields
    ar & for_publisher_rtt;
  }
#endif // COMPILE_FOR == NETWORK
};

class VivaldiSyncReplyData: public VivaldiReplyData {
public:
  //Coordinate sender_coord;
  Clock reply_sent_at;
  bool for_publisher_rtt;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<VivaldiReplyData>(*this);

    // serialize local fields
    //ar & sender_coord;
    ar & reply_sent_at;
    ar & for_publisher_rtt;
  }
#endif // COMPILE_FOR == NETWORK
};
/////////////////////////////////////////////
///      RAPPEL MESSAGES           /////////

class BloomReqData: public RappelRequestData {
public:
  bool audit_friend;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelRequestData>(*this);

    // serialize local fields
    ar & audit_friend; 
  }
#endif //COMPILE_FOR == NETWORK
};

class BloomReplyData: public RappelReplyData {
public:
  BloomFilter bloom_filter;
  bool audit_friend;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelReplyData>(*this);

    // serialize local fields
    ar & bloom_filter;
    ar & audit_friend; 
  }
#endif //COMPILE_FOR == NETWORK
};

class AddFriendReqData: public RappelRequestData {
public:
  vector<node_id_t> candidates;
  node_id_t node_to_be_pruned;
  unsigned int bloom_filter_version;
  vector<node_id_t> next_targets;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelRequestData>(*this);

    // serialize local fields
    ar & candidates;
    ar & node_to_be_pruned;
    ar & bloom_filter_version; 
    ar & next_targets;
  }
#endif //COMPILE_FOR == NETWORK
};

class RemoveFriendData: public RappelRequestData {
public:
  vector<node_id_t> candidates;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelRequestData>(*this);

    // serialize local fields
    ar & candidates;
  }
#endif //COMPILE_FOR == NETWORK
};

class AddFriendReplyData: public RappelReplyData {
public:
  vector<node_id_t> candidates;
  node_id_t node_to_be_pruned;
  bool ok_to_add;
  bool filter_has_changed;
  vector<node_id_t> next_targets;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelReplyData>(*this);

    // serialize local fields
    ar & candidates;
    ar & node_to_be_pruned; 
    ar & ok_to_add;
    ar & filter_has_changed;
    ar & next_targets;
  }
#endif //COMPILE_FOR == NETWORK
};

class RemoveFanData: public RappelRequestData {
public:
  vector<node_id_t> candidates;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelRequestData>(*this);

    // serialize local fields
    ar & candidates;
  }
#endif //COMPILE_FOR == NETWORK
};

// -------- Ping request

class PingReqInfo {
public:
  virtual ~PingReqInfo() { }
  Role role;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {

    // serialize local fields
    ar & role;
  }
#endif //COMPILE_FOR == NETWORK
};

class PingReqInfoFromFriend: public PingReqInfo {
public:
  // nothing specific here

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<PingReqInfo>(*this);

    // serialize local fields
  }
#endif //COMPILE_FOR == NETWORK
};

class PingReqInfoFromParent: public PingReqInfo {
public:
  unsigned int last_update_seq;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<PingReqInfo>(*this);

    // serialize local fields
    ar & last_update_seq;
  }
#endif //COMPILE_FOR == NETWORK
};

class PingReqInfoFromChild: public PingReqInfo {
public:
  unsigned int last_update_seq;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<PingReqInfo>(*this);

    // serialize local fields
    ar & last_update_seq;
  }
#endif //COMPILE_FOR == NETWORK
};

class PingReqInfoFromBackup: public PingReqInfo {
public:
  unsigned int last_update_seq;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<PingReqInfo>(*this);

    // serialize local fields
    ar & last_update_seq;
  }
#endif //COMPILE_FOR == NETWORK
};

/**
 * base class for all data piggybacked on ping requests
 */
class PingReqData: public RappelRequestData {
public:
  ~PingReqData() { // to avoid memory leak
    for (unsigned int i = 0; i < requests.size(); i++) {
      //std::cout << "deleting request info: " << i << std::endl;
      delete requests[i];
    }
  }
  unsigned int bloom_filter_version;
  vector<node_id_t> candidates;
  vector<PingReqInfo*> requests;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelRequestData>(*this);

    // register classes
    ar.register_type(static_cast<PingReqInfoFromFriend*>(NULL));
    ar.register_type(static_cast<PingReqInfoFromParent*>(NULL));
    ar.register_type(static_cast<PingReqInfoFromChild*>(NULL));
    ar.register_type(static_cast<PingReqInfoFromBackup*>(NULL));

    // serialize local fields
    ar & bloom_filter_version;
    ar & candidates;
    ar & requests;
  }
#endif //COMPILE_FOR == NETWORK
};

// -------- Ping reply

class PingReplyInfo {
public:
  virtual ~PingReplyInfo() { }
  Role role;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {

    // serialize local fields
    ar & role;
  }
#endif //COMPILE_FOR == NETWORK
};

class PingReplyInfoFromFriend: public PingReplyInfo {
public:
  // nothing specific here

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<PingReplyInfo>(*this);

    // serialize local fields
  }
#endif //COMPILE_FOR == NETWORK
};

class PingReplyInfoFromParent: public PingReplyInfo {
public:
  unsigned int last_update_seq;
  Coordinate publisher_nc;
  //vector<node_id_it> ancestors;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<PingReplyInfo>(*this);

    // serialize local fields
    ar & last_update_seq;
    ar & publisher_nc;
    // ar & ancestors;
  }
#endif //COMPILE_FOR == NETWORK
};

class PingReplyInfoFromChild: public PingReplyInfo {
public:
  unsigned int last_update_seq;
  Coordinate publisher_nc;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<PingReplyInfo>(*this);

    // serialize local fields
    ar & last_update_seq;
    ar & publisher_nc;
  }
#endif //COMPILE_FOR == NETWORK
};

class PingReplyInfoFromBackup: public PingReplyInfo {
public:
  unsigned int last_update_seq;
  Coordinate publisher_nc;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<PingReplyInfo>(*this);

    // serialize local fields
    ar & last_update_seq;
    ar & publisher_nc;
  }
#endif //COMPILE_FOR == NETWORK
};

/**
 * base class for data piggybacked on ping replies
 */
class PingReplyData: public RappelReplyData {
public:
  ~PingReplyData() { // to avoid memory leak
    for (unsigned int i = 0; i < replies.size(); i++) {
      delete replies[i];
    }
  }
  unsigned bloom_filter_version;
  vector<node_id_t> candidates;
  vector<PingReplyInfo*> replies;
  //vector<PingReqInfo*> piggybacked_requests; // for one-way relationships

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelReplyData>(*this);

    //printf("inside seriailization of PingReplyData\n");

    // register classes
    ar.register_type(static_cast<PingReplyInfoFromFriend*>(NULL));
    ar.register_type(static_cast<PingReplyInfoFromParent*>(NULL));
    ar.register_type(static_cast<PingReplyInfoFromChild*>(NULL));
    ar.register_type(static_cast<PingReplyInfoFromBackup*>(NULL));
    //ar.register_type(static_cast<PingReqInfoFromFriend*>(NULL)); // maybe one-way
    //ar.register_type(static_cast<PingReqInfoFromBackup*>(NULL)); // maybe one-way

    // serialize local fields
    ar & bloom_filter_version;
    ar & candidates;
    ar & replies;
    //ar & piggybacked_requests;
  }
#endif //COMPILE_FOR == NETWORK
};

// -------- Construction of dissemination trees

/**
 * data pig. on a join demand to a node
 */
class JoinReqData: public RappelRequestData {
public:
  vector<node_id_t> candidates;
  feed_id_t feed_id;
  RappelJoinType request_type;
  // node along with pair of num tries of attempted nodes
  map<node_id_t, unsigned int> attempted;
  vector<node_id_t> failed;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelRequestData>(*this);

    // serialize local fields
    ar & candidates;
    ar & feed_id;
    ar & request_type;
    ar & attempted;
    ar & failed;
  }
#endif //COMPILE_FOR == NETWORK
};

/**
 * data pig. on an authorization to join
 */
class JoinOkData: public RappelReplyData {
public:
  vector<node_id_t> candidates;
  feed_id_t feed_id;
  RappelJoinType request_type;
  double path_cost;
  vector<node_id_t> ancestors;
  Coordinate publisher_nc;
  unsigned int last_update_seq;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelReplyData>(*this);

    // serialize local fields
    ar & candidates;
    ar & feed_id;
    ar & request_type;
    ar & path_cost;
    ar & ancestors;
    ar & publisher_nc;
    ar & last_update_seq;
  }
#endif //COMPILE_FOR == NETWORK
};

/**
 * data pig. on a denial to join
 */
class JoinDenyData: public RappelReplyData {
public:
  vector<node_id_t> candidates;
  feed_id_t feed_id;
  RappelJoinType request_type;
  bool publisher_nc_attached;
  Coordinate publisher_nc;
  // node along with pair of num tries of attempted nodes
  map<node_id_t, unsigned int> attempted;
  vector<node_id_t> failed;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelReplyData>(*this);

    // serialize local fields
    ar & candidates;
    ar & feed_id;
    ar & request_type;
    ar & publisher_nc_attached;
    ar & publisher_nc;
    ar & attempted;
    ar & failed;
  }
#endif //COMPILE_FOR == NETWORK
};

/**
 * data pig. on a forward answer to join
 */
class JoinFwdData: public RappelReplyData {
public:
  vector<node_id_t> candidates;
  feed_id_t feed_id;
  RappelJoinType request_type;
  Coordinate publisher_nc;
  node_id_t fwd_node; 
  // node along with pair of num tries of attempted nodes
  map<node_id_t, unsigned int> attempted;
  vector<node_id_t> failed;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelReplyData>(*this);

    // serialize local fields
    ar & candidates;
    ar & feed_id;
    ar & request_type;
    ar & publisher_nc;
    ar & fwd_node;
    ar & attempted;
    ar & failed;
  }
#endif //COMPILE_FOR == NETWORK
};

/** 
 * This is a special message -- it is not a "request" (i.e., expecting a reply)
 * or a reply. It is simply a "directive" message.
 */
class JoinChangeParentData: public RappelMessageData {
public:
  feed_id_t feed_id;
  Coordinate publisher_nc;
  node_id_t new_parent;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelMessageData>(*this);

    // serialize local fields
    ar & feed_id;
    ar & publisher_nc;
    ar & new_parent;
  }
#endif //COMPILE_FOR == NETWORK
};

class NotAChildData: public RappelMessageData {
public:
  feed_id_t feed_id;
  Coordinate publisher_nc;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelMessageData>(*this);

    // serialize local fields
    ar & feed_id; 
    ar & publisher_nc;
  }
#endif //COMPILE_FOR == NETWORK
};

class FlushAncestryData: public RappelMessageData {
public:
  feed_id_t feed_id;
  Coordinate publisher_nc;
  vector<node_id_t> ancestors;
  double path_cost;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelMessageData>(*this);

    // serialize local fields
    ar & feed_id; 
    ar & publisher_nc;
    ar & ancestors;
    ar & path_cost; 
  }
#endif //COMPILE_FOR == NETWORK
};

// -------- Updates dissemination and acks

/**
 * data pig. on a feed update message (the update itself and its number)
 */
class FeedUpdateData: public RappelMessageData {
public:
  feed_id_t feed_id;
  Coordinate publisher_nc; // update publisher_nc 
  unsigned int seq_num;
  std::string msg;
  Clock sync_emission_time;
  bool pulled;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelMessageData>(*this);

    // serialize local fields
    ar & feed_id; 
    ar & publisher_nc;
    ar & seq_num;
    ar & msg;
    ar & sync_emission_time;
    ar & pulled;
  }
#endif //COMPILE_FOR == NETWORK
};

/**
 * data pig. on a request for updates to a node
 */
class FeedUpdatePullData: public RappelMessageData {
public:
  feed_id_t feed_id;
  unsigned int seq_num;

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    // serialize base class information
    ar & boost::serialization::base_object<RappelMessageData>(*this);

    // serialize local fields
    ar & feed_id; 
    ar & seq_num;
  }
#endif //COMPILE_FOR == NETWORK
};

/**
* Message object
*/
class Message {

public: 
  Message()
    : src(NO_SUCH_NODE),
    dst(NO_SUCH_NODE),
    layer(LYR_NONE),
    flag(MSG_NONE),
    data(NULL) {
  }

  bool is_initialized() const {
    assert(src != NO_SUCH_NODE);
    assert(dst != NO_SUCH_NODE);
    assert(layer != LYR_NONE);
    assert(flag != MSG_NONE);
    assert(data != NULL);
#if COMPILE_FOR == NETWORK
    assert(src.get_address() != NO_SUCH_NODE.get_address());
    assert(dst.get_address() != NO_SUCH_NODE.get_address());
#endif // COMPILE_FOR == NETWORK
    return true;
  }

  node_id_t src;
  node_id_t dst;
  Layer layer;
  MessageFlag flag;
  MessageData* data; 

#if COMPILE_FOR == NETWORK
private:
  friend class boost::serialization::access;
  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {

    // register classes
    ar.register_type(static_cast<MessageData*>(NULL));
    ar.register_type(static_cast<CommandMessageData*>(NULL));
    ar.register_type(static_cast<VivaldiMessageData*>(NULL));
    ar.register_type(static_cast<VivaldiRequestData*>(NULL));
    ar.register_type(static_cast<VivaldiReplyData*>(NULL));
    ar.register_type(static_cast<VivaldiPingReqData*>(NULL));
    ar.register_type(static_cast<VivaldiPingReplyData*>(NULL));
    ar.register_type(static_cast<VivaldiSyncReqData*>(NULL));
    ar.register_type(static_cast<VivaldiSyncReplyData*>(NULL));
    ar.register_type(static_cast<PingReqData*>(NULL));
    ar.register_type(static_cast<RappelMessageData*>(NULL));
    ar.register_type(static_cast<RappelRequestData*>(NULL));
    ar.register_type(static_cast<RappelReplyData*>(NULL));
    ar.register_type(static_cast<BloomReqData*>(NULL));
    ar.register_type(static_cast<BloomReplyData*>(NULL));
    ar.register_type(static_cast<AddFriendReqData*>(NULL));
    ar.register_type(static_cast<RemoveFriendData*>(NULL));
    ar.register_type(static_cast<AddFriendReplyData*>(NULL));
    ar.register_type(static_cast<RemoveFanData*>(NULL));
    ar.register_type(static_cast<PingReplyData*>(NULL));
    ar.register_type(static_cast<JoinReqData*>(NULL));
    ar.register_type(static_cast<JoinOkData*>(NULL));
    ar.register_type(static_cast<JoinDenyData*>(NULL));
    ar.register_type(static_cast<JoinFwdData*>(NULL));
    ar.register_type(static_cast<JoinChangeParentData*>(NULL));
    ar.register_type(static_cast<NotAChildData*>(NULL));
    ar.register_type(static_cast<FlushAncestryData*>(NULL));
    ar.register_type(static_cast<FeedUpdateData*>(NULL));
    ar.register_type(static_cast<FeedUpdatePullData*>(NULL));

    ar & src;
    ar & dst;
    ar & layer;
    ar & flag;
    ar & data;
  }
#endif //COMPILE_FOR == NETWORK
};

/**************************************************
                   EVENT OBJECTS
***************************************************/

/**
* Object used to pass parameters between events
*/
class EventData {
public:
  virtual ~EventData() {} // polymorphic
};

class StatsEventData: public EventData {
public:
  unsigned int seq_num;
  Clock start_sync_time;
};

class TransportEventData: public EventData {
public:
  Message msg;
};

/**
 * Base class for all rappel events data
 */
class RappelEventData: public EventData {
public:
};

class VivaldiEventData: public EventData {
public:
};

// -------- Vivaldi-specific

class PeriodicVivaldiPingEventData: public VivaldiEventData {
public:
};

class WaitAfterVivaldiPingReqEventData: public EventData {
public:
  node_id_t target;
};

class PeriodicVivaldiSyncEventData: public VivaldiEventData {
public:
};

class WaitAfterVivaldiSyncReqEventData: public EventData {
public:
  bool resend_on_failure;
};

// boostrap
class BootstrapPeriodExpiredEventData: public VivaldiEventData {
public:
};

// -------- Maintenenance (improve s1)
/**
 * Timer for improving s1 nbrs
 */
class PeriodicAuditFriendEventData: public RappelEventData {
public:
};

class WaitAfterBloomReqData: public RappelEventData {
public:
  node_id_t neighbor;
  bool audit_friend;
};

class WaitAfterAddFriendReqData: public RappelEventData {
public:
  node_id_t neighbor; // not used?
  vector<node_id_t> next_targets;
};

// -------- Maintenance (ping requests)

class PeriodicGarbageCollectEventData: public RappelEventData {
public:
  // nothing in particular
};

/**
 * Timer for proactive maintenance
 */
class PeriodicNeighborCheckEventData: public RappelEventData {
public:
  node_id_t neighbor;
};

/**
 * Timer to wait for ping reply
 */
class WaitAfterPingReqData: public RappelEventData {
public:
  node_id_t neighbor;
  vector<Role> roles;
};

// -------- Joining 
/**
 * Timer for RTT Sample
 */
class PeriodicRTTSampleEventData: public RappelEventData {
public:
  feed_id_t feed_id;
};

/**
 * Timer for delayed joins (next joing)
 */
class NextFeedJoinEventData: public RappelEventData {
public:
  // no data required
};

/**
 * Timer for proactive rejoins
 */
class PeriodicRejoinEventData: public RappelEventData {
public:
  feed_id_t feed_id;
};

/**
 * Timer to wait for an answer for a JOIN request
 */
class WaitAfterJoinReqData: public RappelEventData {
public:
  node_id_t neighbor;
  feed_id_t feed_id;
  RappelJoinType request_type;
  // node, num tries
  // node along with pair of num tries of attempted nodes
  map<node_id_t, unsigned int> attempted;
  vector<node_id_t> failed;
};

class WaitAfterUpdatePullData: public RappelEventData {
public:
  node_id_t neighbor;
  feed_id_t feed_id;
  unsigned int seq_num;
};

/**
* Functionality that encapsulates and event
*
* @param node_id foreign node (maybe be the scheduling node itself)
* @param layer the layer where the event will take place
* @param whence when the event will take place (must be in the FUTURE only)
* @param flag app defined flag (will be passed to event call back)
* @param event_data layer specific data
*/
class Event {

public:

  // constructor
  Event()
    : node_id(NO_SUCH_NODE),
    layer(LYR_NONE),
    flag(EVT_NONE),
    data(NULL) {
  }

  // accessors
  bool is_initialized() const {
    return (node_id != NO_SUCH_NODE &&
      layer != LYR_NONE &&
      flag != EVT_NONE &&
      data != NULL);
  }

public: //hack
  event_id_t id;
  Clock whence;
  node_id_t node_id;
  Layer layer;
  EventFlag flag;
  EventData* data;
};

#endif // _DATA_OBJECTS_H
