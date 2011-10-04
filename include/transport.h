#ifndef _TRANSPORT_H
#define _TRANSPORT_H
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cstdio>
#include <vector>
#include <cmath>
#include <map>
#include "timer.h"
#include "rappel_common.h"
#include "data_objects.h"
#if COMPILE_FOR == SIMULATOR
#include "topology.h"
#elif COMPILE_FOR == NETWORK
#include <boost/bind.hpp>
//#include <boost/shared_ptr.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/base_object.hpp>
#include <boost/serialization/utility.hpp>
#include <boost/serialization/list.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/map.hpp>
#include "asio.hpp"
#endif

using namespace std;

typedef int (*TransportCallbackFunction)(Message& msg);

#if COMPILE_FOR == NETWORK

/// The size of a fixed length header.
enum { HEADER_LENGTH = 8, MD5_LENGTH = 32 };

class TCPConnection;

class TCPConnectionManager {

public:

  TCPConnectionManager(asio::io_service& io_service)
    :  _resolver(io_service) {

    _timer = Timer::get_instance();
    assert(_timer != NULL);
  }

  void accept_incoming_connection(TCPConnection* conn);
  void disconnect(const node_id_t& remote_node, TCPConnection* conn);
  void disconnect_all();
  void send(Message& msg);
  void remap(TCPConnection* conn, const node_id_t& connecting_node_id,
    const node_id_t& defined_node_id);
  void conn_to_be_deleted(TCPConnection* conn);
  
private:

  void handle_tcp_resolve(const asio::error& error,
    asio::ip::tcp::resolver::iterator it, const Clock& resolve_start,
    Message& msg);
  void handle_tcp_resolve2(const asio::error& error,
    const asio::ip::tcp::endpoint& remote_endpoint, const Clock& resolve_start,
    Message& msg);
  void handle_connect(const asio::error& error,  TCPConnection* conn);

  asio::ip::tcp::resolver _resolver; // tcp resolver
  std::map<node_id_t, TCPConnection*> _connections;
  std::map<node_id_t, TCPConnection*> _connection_establishment_in_progress;
  std::vector<std::pair<TCPConnection*, Clock> > _to_be_deleted;
  std::vector<Message> _pending;

  Timer* _timer;
};

/// The connection class provides serialization primitives on top of a socket.
/**
 * Each message sent using this class consists of:
 * @li An 8-byte header containing the length of the serialized data in
 * hexadecimal.
 * @li The serialized data.
 */
class TCPConnection {

public:
  /// Constructor
  TCPConnection(asio::io_service& io_service, TCPConnectionManager* conn_mgr)
    : _socket(io_service),
      _timeout(io_service),
      _send_in_progress(false),
      _pkts_read(0), 
      _to_be_deleted(false),
      _deactivate(false),
      _tcp_conn_mgr(conn_mgr) {

    assert(_tcp_conn_mgr != NULL);

    _timer = Timer::get_instance();
    assert(_timer != NULL);
  }
  TCPConnection(asio::io_service& io_service, TCPConnectionManager* conn_mgr,
    const asio::ip::tcp::endpoint& local_endpoint)
    : _socket(io_service, local_endpoint),
      _timeout(io_service),
      _send_in_progress(false),
      _pkts_read(0), 
      _to_be_deleted(false),
      _deactivate(false),
      _tcp_conn_mgr(conn_mgr) {

    assert(_tcp_conn_mgr != NULL);

    _timer = Timer::get_instance();
    assert(_timer != NULL);
  }
  ~TCPConnection();

  std::vector<Message> get_pending_messages() const {
    return _pending;
  }

  /**
   * Get the underlying socket. Used for making a connection or for accepting
   * an incoming connection.
   */
  asio::ip::tcp::socket& socket() {
    return _socket;
  }
  void set_pending_messages(const std::vector<Message>& msgs) {
    _pending = msgs;
  }
  void clear_pending_messages() {
    _pending.clear();
  }

  void await_incoming_message();
  void terminate();
  void deactivate();
  void send(Message& msg);
  void map_to(const node_id_t& node_id);
  node_id_t mapped_to() const;
  bool to_be_deleted() const {
    return _to_be_deleted;
  }

  void mark_to_be_deleted() {
    _to_be_deleted = true;
    _tcp_conn_mgr->conn_to_be_deleted(this);
  }

private:
  void extend_timeout();
  void timeout(const asio::error& error);
  void handle_write(const asio::error& error);

  template <typename T, typename Handler>
  void async_write(const T& t, Handler handler);

  void handle_read(const asio::error& error); 

  template <typename T, typename Handler>
  void async_read(T& t, Handler handler);

  template <typename T, typename Handler>
  void handle_read_header(const asio::error& error, T& t,
    boost::tuple<Handler> handler); 

  template <typename T, typename Handler>
  void handle_read_data(const asio::error& error, T& t,
    boost::tuple<Handler> handler);

  /// The remote node and the underlying socket to it
  node_id_t _remote_node;
  asio::ip::tcp::socket _socket;

  /// Holds an inbound header and data
  Message _msg_in;
  char _inbound_header[HEADER_LENGTH];
  std::vector<char> _inbound_data;

  /// Holds the outbound data and all
  Message _msg_out;
  std::vector<Message> _pending;
  std::string _outbound_header;
  std::string _outbound_md5sum;
  std::string _outbound_data;

  /// Timer to timeout stale TCP connection
  asio::deadline_timer _timeout;

  /// State
  Clock _last_communication;
  bool _send_in_progress;
  unsigned int _pkts_read;
  bool _to_be_deleted;
  bool _deactivate;

  TCPConnectionManager* _tcp_conn_mgr;
  Timer* _timer;
};
#endif // COMPILE_FOR == NETWORK

class Transport {

public:
#if COMPILE_FOR == SIMULATOR
  Transport();
#elif COMPILE_FOR == NETWORK
  Transport(asio::io_service& ios);
#endif
  ~Transport(); 

  // singleton
  static Transport* get_instance();

  // mutators
  void register_callback(Layer layer, TransportCallbackFunction callback);
#if COMPILE_FOR == NETWORK
  void start_server();
  void stop_server();
#endif
  void send_with_delay(Message& msg, Clock delay);
  void send(Message& msg);

  // Etienne: hack to help debug information
#if COMPILE_FOR == SIMULATOR
  Clock get_route_delay(node_id_t src,node_id_t dst) const {
    return _topology->network_delay(src,dst);
  }

  Clock get_route_delay_native(node_id_t src,node_id_t dst) const {
    return _topology->network_delay_native(src,dst);
  }
#endif

  // static
  static int transport_event_callback(Event& event);

private:
  // variables

  // system-wide singleton
  RNG* _rng;
  Timer* _timer;
#if COMPILE_FOR == SIMULATOR
  Topology* _topology;
#elif COMPILE_FOR == NETWORK
  IOService* _io_service;
#endif

  // self-referenced singleton
  static Transport* _instance;

  // data
  map<Layer, TransportCallbackFunction> _callbacks;

#if COMPILE_FOR == NETWORK

  // These two handle all the TCP connections
  asio::ip::tcp::acceptor _tcp_server;
  TCPConnectionManager* _tcp_conn_mgr;

  // UDP stuff
  asio::ip::udp::resolver _resolver; // udp resolver
  asio::ip::udp::socket* _udp_server;
  unsigned short _outgoing_udp_port;
  asio::ip::udp::endpoint _incoming_udp_remote_endpoint;
  char _inbound_udp_data[MAX_STRING_LENGTH];

  // used to safely exit the program
  static bool _stop_in_progress;
  asio::deadline_timer* _cmd_timeout;

  // functions
  void handle_tcp_accept(const asio::error& error, TCPConnection* conn);
  void resolve_only(const node_id_t& dst);
  void handle_resolve_only(const asio::error& error,
    asio::ip::udp::resolver::iterator it, const Clock& resolve_start,
    const node_id_t& dst);
  void udp_send(Message& msg);
  void await_incoming_udp_message();
  void handle_udp_resolve(const asio::error& error,
    asio::ip::udp::resolver::iterator it, const Clock& resolve_start,
    Message& msg);
  void handle_udp_resolve2(const asio::error& error,
  const asio::ip::udp::endpoint& remote_endpoint, const Clock& resolve_start,
    Message& msg);
  void handle_udp_send(asio::ip::udp::socket* outgoing,
    const asio::error& error, size_t bytes_recvd, Message& msg,
    std::string* outbound_udp_msg);
  void handle_udp_read(const asio::error& error, size_t bytes_recvd);
  void delayed_stop(const asio::error& error);
#endif

};

#endif // _TRANSPORT_H
