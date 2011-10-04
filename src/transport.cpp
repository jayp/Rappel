#include <cstring>
#include "transport.h"
#include "debug.h"
#if COMPILE_FOR == NETWORK
#include "md5.hpp"
#endif

using namespace std;

Transport* Transport::_instance = NULL; // initialize pointer

#if COMPILE_FOR == NETWORK
struct dns_info_t{
  asio::ip::address_v4 ip_addr;
  Clock resolved_at;
};
static std::map<rappel_address_t, dns_info_t> _dns_resolved;
bool Transport::_stop_in_progress = false;
#endif

#if COMPILE_FOR == SIMULATOR
Transport::Transport() {
#elif COMPILE_FOR == NETWORK
Transport::Transport(asio::io_service& ios) 
  : _tcp_server(ios),  
    _resolver(ios) {
#endif

  _rng = RNG::get_instance();
  assert(_rng != NULL);
  
  _timer = Timer::get_instance();
  assert(_timer != NULL);

  // register event call back
  _timer->register_callback(LYR_TRANSPORT, Transport::transport_event_callback);

#if COMPILE_FOR == SIMULATOR
  _topology = Topology::get_instance();
  assert(_topology != NULL);
  
#elif COMPILE_FOR == NETWORK
  _io_service = IOService::get_instance();
  assert(_io_service != NULL);

  _udp_server = NULL;
  //_udp_send_in_progress = false;
  _tcp_conn_mgr = NULL;
#endif
}

Transport::~Transport() {
}

Transport* Transport::get_instance() {

  if (_instance == NULL) {  
#if COMPILE_FOR == SIMULATOR
    _instance = new Transport(); // create sole instance
#elif COMPILE_FOR == NETWORK
    _stop_in_progress = false;
    _instance = new Transport((IOService::get_instance())->get_ios()); // create sole instance
#endif
    assert(_instance != NULL);
  }

return _instance; // address of sole instance
}

#if COMPILE_FOR == NETWORK
void Transport::start_server() {

  asio::error error;

  bool flexible = false;
  unsigned int port = globops.self_node.get_port();
  if (port == 0) {
    flexible = true;
    port = 38962;
    globops.self_node.set_port(port);
    std::cout << "attempting to start server with new port: " << port << std::endl;
  }

  // START TCP SERVER
  asio::ip::tcp::resolver tcp_resolver(_io_service->get_ios());
  asio::ip::tcp::resolver::query query(asio::ip::tcp::v4(),
    globops.self_node.get_address(),
    boost::lexical_cast<std::string>(globops.self_node.get_port()));
  asio::ip::tcp::resolver_iterator it = tcp_resolver.resolve(query,
    asio::assign_error(error));

  if (error) {
    std::cerr << "Can't resolve local IP for TCP server ("
      << globops.self_node.str() << "): " << error << std::endl;
    assert(false);
  }

  asio::ip::tcp::endpoint local_tcp_endpoint = *it;

  _tcp_server.open(local_tcp_endpoint.protocol());
  _tcp_server.set_option(asio::ip::tcp::acceptor::reuse_address(true));
  _tcp_server.bind(local_tcp_endpoint, asio::assign_error(error));

  if (flexible && error) {

    unsigned int attempts = 0;
    while (++attempts <= 100) {
      port++;
      globops.self_node.set_port(port);
      std::cout << "attempting to start server with new port: " << port << std::endl;

      local_tcp_endpoint.port(port);
      _tcp_server.bind(local_tcp_endpoint, asio::assign_error(error));
      if (!error) {
        break;
      }
    }
  }

  node_id_t local = globops.self_node;

  if (error) {
    std::cerr << "Can't bind to local IP (" << local.str() << "): "
      << error << std::endl;
    assert(false);
  }

  std::cout << "starting server @ " << local.str() << std::endl;

  _tcp_server.listen(0, asio::assign_error(error));

  if (error) {
    std::cerr << "Can't listen to local IP (" << local.str() << "): "
      << error << std::endl;
    assert(false);
  }

  _tcp_conn_mgr = new TCPConnectionManager(_io_service->get_ios());
  assert(_tcp_conn_mgr != NULL);

  // await for new connection
  TCPConnection* new_conn = new TCPConnection(_tcp_server.io_service(),
    _tcp_conn_mgr);
  _tcp_server.async_accept(new_conn->socket(),
    boost::bind(&Transport::handle_tcp_accept, this,
      asio::placeholders::error, new_conn));

  // START UDP SERVER
  asio::ip::udp::resolver::query query2(asio::ip::udp::v4(),
    local.get_address(), boost::lexical_cast<std::string>(local.get_port()));
  asio::ip::udp::resolver_iterator it2 = _resolver.resolve(query2,
    asio::assign_error(error));

  if (error) {
    std::cerr << "Can't resolve local IP for UDP server (" << local.str() << "): "
      << error << std::endl;
    assert(false);
  }

  asio::ip::udp::endpoint local_udp_endpoint = *it2;

  assert(_udp_server == NULL);
  _udp_server = new asio::ip::udp::socket(_io_service->get_ios(),
    local_udp_endpoint);
  assert(_udp_server != NULL);
  await_incoming_udp_message();

  _outgoing_udp_port = globops.self_node.get_port() + 1;
}
#endif

/**
* One of the most important functions: whenever a message is recvd at a node,
* it will be routed to the correct application via the desired callback
* function. Registration should be done in the application's _init() function
*
* @param layer The layer must be a layer "higher" than LYR_TRANSPORT
* @param callback The callback function whenever the application LYR recieves
*                 a send event
*/
void Transport::register_callback(Layer layer, TransportCallbackFunction callback) {

  // make certain this is an application layer
  assert(layer > LYR_TRANSPORT);

  // make sure that the application isn't already registered
  // TODO: think if re-registration should be allowed?
  assert(_callbacks.find(layer) == _callbacks.end());

  _callbacks[layer] = callback;
}

/**
* Send a node to a foreign node
*
* @param msg The msg to be sent from source node to destination node
*/
void Transport::send_with_delay(Message& msg, Clock delay) {

  // make certain this is an application layer
  assert(msg.layer > LYR_TRANSPORT);

  // sanity check for msg
  assert(msg.is_initialized());
  assert(msg.src != msg.dst);

  // schedule a event at the transport layer of the foriegn node
  // TODO: do we need to mark off all the intermediate hops?
  TransportEventData* data = new TransportEventData;
  data->msg = msg;

  Event event;
  event.node_id = msg.src;
  event.layer = LYR_TRANSPORT;
  event.whence = /*_timer->get_time() +*/ delay;
  event.flag = EVT_TRANSPORT_DELAY_SEND;
  event.data = data;

  _timer->schedule_event(event);
}

/**
* Send a node to a foreign node
*
* @param msg The msg to be sent from source node to destination node
*/
void Transport::send(Message& msg) {

  // make certain this is an application layer
  assert(msg.layer > LYR_TRANSPORT);

  // sanity check for msg
  assert(msg.is_initialized());
  assert(msg.src != msg.dst);

#if COMPILE_FOR == NETWORK

  // do not send messages if stop is in progress, unless it is a STOP message
  if (_stop_in_progress
    && msg.flag != MSG_CMD_SIGNAL_STOPPING) {

    delete(msg.data); // unallocate memory
    return;
  }
#endif

#ifdef DEBUG  
  printf("[Time=%s][Transport][Send] src=%s, dst=%s, lyr=%s, flag=%s\n",
    _timer->get_time_str().c_str(), msg.src.str().c_str(), msg.dst.str().c_str(),
    layer_str(msg.layer), msg_flag_str(msg.flag));
#endif // DEBUG

#if COMPILE_FOR == SIMULATOR

  // schedule a event at the transport layer of the foriegn node
  // TODO: do we need to mark off all the intermediate hops?
  TransportEventData* data = new TransportEventData;
  data->msg = msg;

  Clock route_delay = _topology->network_delay(msg.src, msg.dst);
  assert(route_delay > 0 * MICRO_SECOND);

  Event event;
  event.node_id = msg.dst;
  event.layer = LYR_TRANSPORT;
  event.whence = /*_timer->get_time() +*/ route_delay;
  event.flag = EVT_TRANSPORT_RECV;
  event.data = data;

  _timer->schedule_event(event);

#elif COMPILE_FOR == NETWORK

  assert(_tcp_conn_mgr != NULL);
  assert(globops.self_node == msg.src);

  ProtocolFlag protocol = msg.data->protocol();

  if (protocol == PROTO_TCP) {

    _tcp_conn_mgr->send(msg);

  } else if (protocol == PROTO_UDP) {

    udp_send(msg);

  } else {
    // should not happen
    assert(false);
  }
#endif
}

#if COMPILE_FOR == NETWORK
/// Handle completion of a accept operation.
void Transport::handle_tcp_accept(const asio::error& error, TCPConnection* conn) {

  if (!error) {

    // Successfully accepted a new connection. 
    _tcp_conn_mgr->accept_incoming_connection(conn);

  } else {

    // An error occurred. Log it.
    if (error != asio::error::operation_aborted) {
      std::cerr << "Z error is: " << error << std::endl;
    }
    delete conn;
  }

  if (!_stop_in_progress) {

    // Start an accept operation for a new connection.
    TCPConnection* new_conn = new TCPConnection(_tcp_server.io_service(), _tcp_conn_mgr);
    _tcp_server.async_accept(new_conn->socket(),
      boost::bind(&Transport::handle_tcp_accept, this,
        asio::placeholders::error, new_conn));
  }
}

void Transport::await_incoming_udp_message() {

  for (unsigned int i = 0; i < MAX_STRING_LENGTH; i++) {
    _inbound_udp_data[i] = '\0';
  }

 _udp_server->async_receive_from(
   asio::buffer(_inbound_udp_data, MAX_STRING_LENGTH), _incoming_udp_remote_endpoint,
   boost::bind(&Transport::handle_udp_read, this, asio::placeholders::error,
     asio::placeholders::bytes_transferred));
}

void Transport::resolve_only(const node_id_t& dst) {
  Clock resolve_start = _timer->get_time();

  if (_dns_resolved.find(dst.get_address()) != _dns_resolved.end()) {
    dns_info_t info = _dns_resolved[dst.get_address()];
    if (info.resolved_at + 8 * HOURS > resolve_start) { // TODO: variablize
      return;
    }
  }

  asio::ip::udp::resolver::query query(asio::ip::udp::v4(),
    dst.get_address(), boost::lexical_cast<std::string>(dst.get_port()));

  _resolver.async_resolve(query, boost::bind(&Transport::handle_resolve_only,
    this, asio::placeholders::error, asio::placeholders::iterator,
    resolve_start, dst));
}


void Transport::handle_resolve_only(const asio::error& error,
  asio::ip::udp::resolver::iterator it, const Clock& resolve_start,
  const node_id_t& dst) {

  if (error) {
    std::cerr << "Can't resolve remote IP resolve only (" << dst.str()
      << "): " << error << std::endl;
    return;
  }

  // debug info
  Clock curr_time = _timer->get_time();
  Clock duration = curr_time - resolve_start;
  std::cout << "@@ " << _timer->get_time_str(curr_time) << " s: forced resolution of "
    << dst.str() << " took " << _timer->get_time_str(duration) << " secs." 
    << std::endl;

  // create or update _dns_resolved
  asio::ip::udp::endpoint remote_endpoint = *it;
  dns_info_t info;
  info.ip_addr = remote_endpoint.address().to_v4();
  info.resolved_at = curr_time;
  _dns_resolved[dst.get_address()] = info;
}

void Transport::udp_send(Message& msg) {

  Clock resolve_start = _timer->get_time();

  if (_dns_resolved.find(msg.dst.get_address()) != _dns_resolved.end()) {
    dns_info_t info = _dns_resolved[msg.dst.get_address()];
    if (info.resolved_at + 8 * HOURS > resolve_start) { // TODO: variablize
      asio::ip::udp::endpoint remote_endpoint(info.ip_addr, msg.dst.get_port());
      handle_udp_resolve2(asio::error::success, remote_endpoint, -1 * SECONDS, msg);
      return;
    }
  }

  asio::ip::udp::resolver::query query(asio::ip::udp::v4(),
    msg.dst.get_address(),
    boost::lexical_cast<std::string>(msg.dst.get_port()));

  _resolver.async_resolve(query, boost::bind(&Transport::handle_udp_resolve,
    this, asio::placeholders::error, asio::placeholders::iterator,
    resolve_start, msg));
}

void Transport::handle_udp_resolve(const asio::error& error,
  asio::ip::udp::resolver::iterator it, const Clock& resolve_start, Message& msg) {

  if (error) {
    std::cerr << "Can't resolve remote IP for UDP send (" << msg.dst.str()
      << "): " << error << std::endl;
    // NOTE: too cumbersome to use ESCAPE_CLAUSE already
    delete(msg.data); // unallocate memory
    return;
  }

  asio::ip::udp::endpoint remote_endpoint = *it;
  handle_udp_resolve2(asio::error::success, remote_endpoint, resolve_start, msg);
}

void Transport::handle_udp_resolve2(const asio::error& error,
  const asio::ip::udp::endpoint& remote_endpoint, const Clock& resolve_start,
  Message& msg) {

  if (resolve_start != -1 * SECONDS) {

    // debug info
    Clock curr_time = _timer->get_time();
    Clock duration = curr_time - resolve_start;
    std::cout << "@@ " << _timer->get_time_str(curr_time) << " s: udp resolution of "
      << msg.dst.str() << " took " << _timer->get_time_str(duration) << " secs." 
      << std::endl;

    // create or update _dns_resolved
    dns_info_t info;
    info.ip_addr = remote_endpoint.address().to_v4();
    info.resolved_at = curr_time;
    _dns_resolved[msg.dst.get_address()] = info;
  }

  // Serialize the data first so we know how large it is.
  std::ostringstream archive_stream;
  boost::archive::text_oarchive archive(archive_stream);
  try {
    const Message msg_tmp = msg; // this is required for the next line
    archive << msg_tmp;
  } catch (std::exception& e) {

    // Unable to decode data.
    std::cerr << "error in writing udp data to archive (internal): " << e.what() << std::endl;
    assert(false);
  }

  std::string outbound_udp_data = archive_stream.str();

  // format the header
  std::ostringstream header_stream;
  header_stream << std::setw(HEADER_LENGTH)
    << std::hex << outbound_udp_data.size();
  std::string outbound_udp_header = header_stream.str();
  std::string outbound_udp_md5sum = md5(
    outbound_udp_data.c_str()).digest().hex_str_value();

  // declaring these two early -- so we can use ESCAPE_CLAUSE from here on
  //std::vector<asio::const_buffer> buffers;
  std::string *outbound_udp_msg;
  asio::ip::udp::socket* outgoing;

  if (!header_stream || header_stream.str().size() != HEADER_LENGTH) {
    // something went wrong, get out
    std::cerr << "oh no, header length is improper" << std::endl;
    goto ESCAPE_CLAUSE;
  }

  if (outbound_udp_md5sum.size() != MD5_LENGTH) {
    // something went wrong, get out
    std::cerr << "oh no, improper md5 sig size" << std::endl;
    goto ESCAPE_CLAUSE;
  }

  if (msg.data->debug_flag) {

    std::cout << "-------------------------------------------------" << std::endl;
    std::cout << "DEBUG MESSAGE: " << msg_flag_str(msg.flag) << std::endl;
    std::cout << "-------------------------------------------------" << std::endl;
    std::cout << outbound_udp_header << outbound_udp_md5sum
      << outbound_udp_data << std::endl;
    std::cout << "-------------------------------------------------" << std::endl;

    goto ESCAPE_CLAUSE;
  }

  while (true) {
    try {
      outgoing = new asio::ip::udp::socket(_io_service->get_ios(), 
        asio::ip::udp::endpoint(asio::ip::udp::v4(), _outgoing_udp_port));
      break; // on success??
    } catch (const asio::error& error) {
      if (error == asio::error::address_in_use) {
        ++_outgoing_udp_port;
        if (_outgoing_udp_port > 43231) { // just in case
          _outgoing_udp_port = 21343;
        }
      } else {
        std::cerr << "wt? wierd error during udp open for port "
          << _outgoing_udp_port << ": " << error << std::endl;
        goto ESCAPE_CLAUSE; // oops -- some wierd error?
      }
    }
  }

  outbound_udp_msg = new std::string(outbound_udp_header
    + outbound_udp_md5sum + outbound_udp_data);

  outgoing->async_send_to(asio::buffer(*outbound_udp_msg), remote_endpoint,
    boost::bind(&Transport::handle_udp_send, this, outgoing,
      asio::placeholders::error, asio::placeholders::bytes_transferred, msg,
      outbound_udp_msg));

  // no need to unalloacte msg.data -- done in handle_udp_send
  return;
  
ESCAPE_CLAUSE: 
  delete(msg.data); // unallocate memory
}

void Transport::handle_udp_send(asio::ip::udp::socket* outgoing,
  const asio::error& error, size_t bytes_sent, Message& msg,
  std::string* outbound_udp_msg) {

  if (error) {
    std::cerr << "wt? error in udp send: " << error << std::endl;
  }

  delete(msg.data); // unallocate memory
  delete(outgoing); // unallocate socket
  delete(outbound_udp_msg);
}

void Transport::handle_udp_read(const asio::error& error, size_t bytes_recvd) {

/*
  printf("gotten msg:[[%s]]\n", _inbound_udp_data);
  printf("msg str_size: %u; bytes_recvd: %u\n", strlen(_inbound_udp_data),
    bytes_recvd);
*/

  if (!error) {

    // Determine the length of the serialized data.
    std::istringstream is(std::string(&_inbound_udp_data[0], HEADER_LENGTH));
    std::size_t inbound_data_size = 0;

    if (!(is >> std::hex >> inbound_data_size)) {

      // Header doesn't seem to be valid. Inform the caller.
      std::cerr << "error in reading header (internal)" << std::endl;
      goto MALFORMED_UDP_MESSAGE;
    }

    if (HEADER_LENGTH + MD5_LENGTH + inbound_data_size != bytes_recvd) {
      std::cerr << "packet size mismatch" << std::endl;
      goto MALFORMED_UDP_MESSAGE;
    }

    std::string md5sum_read(&_inbound_udp_data[HEADER_LENGTH], MD5_LENGTH);
    std::string archive_data(&_inbound_udp_data[HEADER_LENGTH + MD5_LENGTH],
      inbound_data_size);

    std::string md5sum_calculated
      = md5(archive_data.c_str()).digest().hex_str_value();

    if (md5sum_read != md5sum_calculated) {
      std::cerr << "invalid md5sum" << std::endl;
      goto MALFORMED_UDP_MESSAGE;
    }

    // Extract the data structure from the data just received.
    Message msg_in;
    try {

      std::istringstream archive_stream(archive_data);
      boost::archive::text_iarchive archive(archive_stream);
      archive >> msg_in;

    } catch (std::exception& e) {

      // Unable to decode data.
      std::cerr << "error in reading data (internal): " << e.what() << std::endl;
      goto MALFORMED_UDP_MESSAGE;
    }

    if (msg_in.dst != globops.self_node) {
      std::cerr << "msg recvd for unknown node" << std::endl;
      goto MALFORMED_UDP_MESSAGE;
    }

    TransportEventData* data = new TransportEventData;
    data->msg = msg_in;

    Event event;
    event.node_id = msg_in.dst;
    event.layer = LYR_TRANSPORT;
    //event.whence = (Timer::get_instance())->get_time();
    event.flag = EVT_TRANSPORT_RECV;
    event.data = data;

    (Transport::get_instance())->transport_event_callback(event);

  } else {

    std::cerr << "error in reading UDP data: " << error << std::endl;
  }

MALFORMED_UDP_MESSAGE:
  if (!_stop_in_progress) {
    await_incoming_udp_message();
  }
};

void Transport::stop_server() {

  // stop delivering incoming and sending outgoing messages
  _stop_in_progress = true;

  _cmd_timeout = new asio::deadline_timer(_io_service->get_ios());
  _cmd_timeout->expires_from_now(boost::posix_time::seconds(10) +
    boost::posix_time::microseconds(_rng->rand_uint(0 * SECOND, 20 * SECOND)));

  _cmd_timeout->async_wait(boost::bind(&Transport::delayed_stop, this,
    asio::placeholders::error)); // TODO: delete _cmd_timeout? maybe.
}

void Transport::delayed_stop(const asio::error& error) {

  if (error) {
    std::cerr << "Transport::delayed_stop -> error: " << error << std::endl;
  }

  std::cout << "Transport::delayed_stop " << std::endl;

  // close the tcp and udp server
  _tcp_server.close();
  _udp_server->close();

  // terminate all active tcp connections
  _tcp_conn_mgr->disconnect_all();
    
  // kill all pending events
  _timer->cancel_all_pending_events();

  std::cout << "Done killing everything -- I feels" << std::endl;
}
#endif

int Transport::transport_event_callback(Event& event) {

  // the singleton instance should be initialized by now
  assert(_instance != NULL);

  // make sure the event is scheduled for PING
  assert(event.layer == LYR_TRANSPORT);
  
  switch (event.flag) {

    case EVT_STATS:
    case EVT_FINAL: {
      // nothing to do
      break;
    }

    case EVT_TRANSPORT_DELAY_SEND: {
      
      TransportEventData* data = dynamic_cast<TransportEventData*>(event.data);
      assert(data != NULL);

      _instance->send(data->msg);

      // delete event data object
      delete(data);
      event.data = NULL;

      break;
    }

    case EVT_TRANSPORT_RECV: {

#if COMPILE_FOR == NETWORK
      if (_stop_in_progress) {
        delete(event.data); // unallocate memory
        break;
      }
#endif
      
      TransportEventData* data = dynamic_cast<TransportEventData*>(event.data);
      assert(data != NULL);

      // make sure the event's layer is registered
      assert(_instance->_callbacks.find(data->msg.layer) != _instance->_callbacks.end());
      
#if COMPILE_FOR == NETWORK
      // resolve all incoming (source) nodes right away
      (Transport::get_instance())->resolve_only(data->msg.src);
#endif // COMPILE_FOR == NETWORK

#ifdef DEBUG  
      // this is required as transport_event_callback is a static method
      Timer* timer = Timer::get_instance();
      printf("[Time=%s][Transport][Recv] src=%s, dst=%s, lyr=%s, flag=%s\n",
        timer->get_time_str().c_str(), data->msg.src.str().c_str(),
        data->msg.dst.str().c_str(),
        layer_str(data->msg.layer), msg_flag_str(data->msg.flag));
#endif // DEBUG

      // invoke the application layer's recv callback
      _instance->_callbacks[data->msg.layer](data->msg);

      // delete event data object
      delete(data);
      event.data = NULL;

      break;
    }

    default:
      assert(false);
  }
return 0;
}

#if COMPILE_FOR == NETWORK
void TCPConnectionManager::accept_incoming_connection(TCPConnection* conn) {

  Clock curr_time = _timer->get_time();
  for (std::vector<std::pair<TCPConnection*, Clock> >::iterator
    it = _to_be_deleted.begin(); it != _to_be_deleted.end(); /* it++ */ ) {

    if (curr_time > it->second + 7 * MINUTES) {
     
      TCPConnection* conn = it->first;
      assert(conn->to_be_deleted() == true);
      delete conn;
      it = _to_be_deleted.erase(it);
    } else {
      it++;
    }
    assert(it != _to_be_deleted.end() + 1);
  }

  asio::error error;
  asio::ip::tcp::endpoint remote
   = conn->socket().remote_endpoint(asio::assign_error(error));

  if (error) {
    std::cout << "Can't find remote endpoint connection" << std::endl;;
    assert(false);
  }

  node_id_t remote_node(remote);
  printf("accepting remote connection from: %s\n", remote_node.str().c_str());

  // TODO: handle this unlikely scenario: there is a pending outgoing connection
  // request to a remote host, that is simultaneously trying to establish
  // a connection

  // TIP: remote_node is never going to connect with the same port as its
  // native/server port -- i.e., it will connect from a temporary port.
  // Hence it should never be in the _connections list

  assert(_connections.find(remote_node) == _connections.end());

  _connections[remote_node] = conn;
  conn->map_to(remote_node);
  conn->await_incoming_message();
}

void TCPConnectionManager::disconnect(const node_id_t& remote_node,
  TCPConnection* conn) {

  //node_id_t remote_node = conn->mapped_to();
    
  std::cout << "@ " << _timer->get_time_str() 
    << ": terminating TCPConnectionManager connection to: "
    << remote_node.str() << std::endl;;

/*
  std::cout << "list of current connections (prior to deletion)" << std::endl;
  for (std::map<node_id_t, TCPConnection*>::const_iterator
    it = _connections.begin(); it != _connections.end(); it++) {

    std::cout << "  connection: " << it->first.str() << std::endl;
  }
*/

  // a _socket.close() aborts all pending async read/writes -- this creates
  // another call to disconnect(). this is broken below.
  
  map<node_id_t, TCPConnection*>::iterator it = _connections.find(remote_node);
  if (it != _connections.end()) {

    // remove from _connections list
    _connections.erase(it);

    // close socket and cancel timeout timer
    TCPConnection* conn2 = it->second;
    assert(conn == conn2);
    conn->terminate();

    // keep track of memory to de-allocate (a bit later)
    conn->mark_to_be_deleted();

  } else {

    // remove dst from _in_progress
    it = _connection_establishment_in_progress.find(remote_node);
    if (it != _connection_establishment_in_progress.end()) {

      _connection_establishment_in_progress.erase(it);

      // close socket and cancel timeout timer
      TCPConnection* conn2 = it->second;
      assert(conn == conn2);
      conn->terminate();
  
      // keep track of memory to de-allocate (a bit later)
      conn->mark_to_be_deleted();

      // remove all pending messages (to be sent)
      for (std::vector<Message>::iterator it = _pending.begin();
        it != _pending.end(); /* it++ */) {

        if (it->dst == remote_node) {
          delete it->data;
          it = _pending.erase(it);
        } else {
          it++;
        }
      }
    }
  }

  assert(conn->to_be_deleted() == true);
}

void TCPConnectionManager::disconnect_all() {

  // close all pending events
  for (std::map<node_id_t, TCPConnection*>::iterator it = _connections.begin();
    it != _connections.end(); /* it++ */) {

    // it becomes invalid after erase in disconnect(). hence, it++ becomes
    // invalid. hence, calculating it++ before it becomes invalid.
    std::map<node_id_t, TCPConnection*>::iterator it_now = it;
    it++;

    disconnect(it_now->first, it_now->second);
  }

  // close all the in progress connections
  for (std::map<node_id_t, TCPConnection*>::iterator
    it = _connection_establishment_in_progress.begin();
    it != _connection_establishment_in_progress.end(); /* it++ */) {

    // it becomes invalid after erase in disconnect(). hence, it++ becomes
    // invalid. hence, calculating it++ before it becomes invalid.
    std::map<node_id_t, TCPConnection*>::iterator it_now = it;
    it++;

    disconnect(it_now->first, it_now->second);
  }
}

void TCPConnectionManager::send(Message& msg) {

  // if the message destined to current destination is not active, start a new connection
  if (_connections.find(msg.dst) != _connections.end()) {

    //printf("ConnManager: Send (from %s to %s): using already open connection...\n",
    //  msg.src.str().c_str(), msg.dst.str().c_str());
    _connections[msg.dst]->send(msg);
    return;
  }

  // a connection is being established
  if (_connection_establishment_in_progress.find(msg.dst)
    != _connection_establishment_in_progress.end()) {

    printf("ConnManager: Send (from %s to %s): already establishing a "
      "connection (in progress). queuing packet...\n",
       msg.src.str().c_str(), msg.dst.str().c_str());
    _pending.push_back(msg);
    return;
  }

  // NOTE: We may end up resolving msg.dst twice -- but it's not harm. packets
  // are correctly handled in handle_tcp_resolve
  printf("ConnManager: Send (from %s to %s): establishing a NEW connection...\n",
    msg.src.str().c_str(), msg.dst.str().c_str());

  Clock resolve_start = _timer->get_time();

  if (_dns_resolved.find(msg.dst.get_address()) != _dns_resolved.end()) {
    dns_info_t info = _dns_resolved[msg.dst.get_address()];
    if (info.resolved_at + 8 * HOURS > resolve_start) { // TODO: variablize
      asio::ip::tcp::endpoint remote_endpoint(info.ip_addr, msg.dst.get_port());
      handle_tcp_resolve2(asio::error::success, remote_endpoint, -1 * SECONDS, msg);
      return;
    }
  }

  asio::ip::tcp::resolver::query query(asio::ip::tcp::v4(),
    msg.dst.get_address(),
    boost::lexical_cast<std::string>(msg.dst.get_port()));

  _resolver.async_resolve(query,
    boost::bind(&TCPConnectionManager::handle_tcp_resolve, this,
    asio::placeholders::error, asio::placeholders::iterator, resolve_start,
    msg));
}

void TCPConnectionManager::handle_tcp_resolve(const asio::error& error,
  asio::ip::tcp::resolver::iterator it, const Clock& resolve_start,
  Message& msg) {

  if (error) {
    std::cerr << "Can't resolve remote IP for TCP send (" << msg.dst.str() << "): "
      << error << std::endl;
    delete(msg.data); // unallocate memory
    return;
  }

  // a connection is being established
  if (_connection_establishment_in_progress.find(msg.dst)
    != _connection_establishment_in_progress.end()) {

    printf("ConnManager: handle_tcp_resolve (from %s to %s): already establishing a "
      "connection (in progress). queuing packet...\n",
       msg.src.str().c_str(), msg.dst.str().c_str());
    _pending.push_back(msg);
    return;
  }

  asio::ip::tcp::endpoint remote_endpoint = *it;
  handle_tcp_resolve2(asio::error::success, remote_endpoint, resolve_start, msg);
}

void TCPConnectionManager::handle_tcp_resolve2(const asio::error& error,
  const asio::ip::tcp::endpoint& remote_endpoint, const Clock& resolve_start,
  Message& msg) {

  if (resolve_start != -1 * SECONDS) {

    // debug info
    Clock curr_time = _timer->get_time();
    Clock duration = curr_time - resolve_start;
    std::cout << "@@ " << _timer->get_time_str(curr_time) << " s: tcp resolution of "
      << msg.dst.str() << " took " << _timer->get_time_str(duration) << " secs." 
      << std::endl;

    // create or update _dns_resolved
    dns_info_t info;
    info.ip_addr = remote_endpoint.address().to_v4();
    info.resolved_at = curr_time;
    _dns_resolved[msg.dst.get_address()] = info;
  }

  TCPConnection* new_conn = new TCPConnection(
    (IOService::get_instance())->get_ios(), this);
  new_conn->map_to(msg.dst);

  new_conn->socket().async_connect(remote_endpoint,
    boost::bind(&TCPConnectionManager::handle_connect, this,
      asio::placeholders::error, new_conn));

  _connection_establishment_in_progress[msg.dst] = new_conn;
  _pending.push_back(msg);
}

void TCPConnectionManager::remap(TCPConnection* conn,
  const node_id_t& connecting_node_id, const node_id_t& defined_node_id) {

  bool do_remap = true;

  // make sure connecting_node_id is associated with conn
  assert(_connections.find(connecting_node_id) != _connections.end());
  assert(_connections[connecting_node_id] == conn);

  // delete mapping to old remote node (temporary port)
  _connections.erase(connecting_node_id);

  // problem: there could already be a connection for defined_node_id.
  // this could happen if a node initiates a send() to defined_node_id
  // and simultaenously, defined_node_id initiates a send() to that node.

  if (_connections.find(defined_node_id) != _connections.end()) {

    // previous connection already open --  remove it. and add new one.
    printf("there is a previous connectin open....\n");

    // canonical method in preserving a connection
    if (defined_node_id < globops.self_node) {

      printf("killing old connection\n");

      // remove from _connections list
      TCPConnection* old_conn = _connections[defined_node_id];
      _connections.erase(defined_node_id);
      assert(_connections.find(defined_node_id) == _connections.end());

      // move old_conn's pending messages to the new connection
      conn->set_pending_messages(old_conn->get_pending_messages());
      old_conn->clear_pending_messages();

      // keep track of memory to de-allocate (a bit later)
      old_conn->mark_to_be_deleted();

      // terminate connection
      old_conn->terminate();

    } else {

      // deactivate
      printf("killing _NEW_ (incoming) connection\n");

      // don't close yet -- this is still active: terminate after the read
      conn->deactivate();
      do_remap = false;
    }
  }

  // map to correct remote node
  if (do_remap) {
    std::cout << "remapping connection from: [" << connecting_node_id.str()
      << "] to: ["  << defined_node_id.str() << "]" << std::endl;
    _connections[defined_node_id] = conn;
    conn->map_to(defined_node_id);
  }
}

void TCPConnectionManager::conn_to_be_deleted(TCPConnection* conn) {

  assert(conn != NULL);
  assert(conn->to_be_deleted());
  _to_be_deleted.push_back(make_pair(conn, _timer->get_time()));
}

void TCPConnectionManager::handle_connect(const asio::error& error, TCPConnection* conn) {

  if (!error) {

    std::cout << "handle_connect local endpoint: "
      << conn->socket().local_endpoint() << std::endl;
    std::cout << "handle_connect: remote endpoint: "
      << conn->socket().remote_endpoint() << std::endl;

    // make sure the connection is correctly mapped
    assert(_connection_establishment_in_progress[conn->mapped_to()] == conn);

    // remove dst from _in_progress
    assert(_connection_establishment_in_progress.find(conn->mapped_to()) 
      != _connection_establishment_in_progress.end());
    _connection_establishment_in_progress.erase(conn->mapped_to());

    // add dst to _connections
    if (_connections.find(conn->mapped_to()) == _connections.end()) {
      _connections[conn->mapped_to()] = conn;

      // listen to incoming messages
      conn->await_incoming_message();

      // send all pending messages
      for (std::vector<Message>::iterator it = _pending.begin();
        it != _pending.end(); /* it++ */) {

        if (it->dst == conn->mapped_to()) {
          //printf("found something to send\n");
          conn->send(*it);
          it = _pending.erase(it);
        } else {
          it++;
        }
      }

    } else {
      // async op complication: an incoming connection from conn->mapped_to()
      // was established before an outgoing connection can be established

      assert(!_connections[conn->mapped_to()]->to_be_deleted());

      // send all pending messages
      for (std::vector<Message>::iterator it = _pending.begin();
        it != _pending.end(); /* it++ */) {

        if (it->dst == conn->mapped_to()) {
          //printf("found something to send\n");
          _connections[conn->mapped_to()]->send(*it);
          it = _pending.erase(it);
        } else {
          it++;
        }
      }

      // keep track of memory to de-allocate (a bit later)
      conn->mark_to_be_deleted();
      conn->terminate();
    }
      
  } else {

    std::cout << "connection failed: " << conn->mapped_to().str() << std::endl;

    // connect failed
    disconnect(conn->mapped_to(), conn);
  }
}

/************************************************/
TCPConnection::~TCPConnection() {

  for (vector<Message>::iterator it = _pending.begin(); it != _pending.end();
    it++) {

    // unallocate memory for this broken message
    delete(it->data);
  }
}

void TCPConnection::await_incoming_message() {

  //std::cout << "starting timeout timer...\n";
  //std::cout << "time: " << boost::posix_time::microsec_clock::local_time() << std::endl;
  _timeout.expires_from_now(boost::posix_time::microseconds(globops.transport_conn_timeout));
  _timeout.async_wait(boost::bind(&TCPConnection::timeout, this, asio::placeholders::error));

  async_read(_msg_in,
    boost::bind(&TCPConnection::handle_read, this,
     asio::placeholders::error));
}

void TCPConnection::deactivate() {
  _deactivate = true;
}

void TCPConnection::terminate() {

  _socket.close();
  _timeout.cancel();
}

void TCPConnection::send(Message& msg) {

  // TODO: check that msg.dst is the same as _socket.remote_endpoint()
  _last_communication = _timer->get_time();

  if (_send_in_progress) {

    _pending.push_back(msg);

  } else {

    _send_in_progress = true;
    _msg_out = msg;

    assert(_msg_out.src == globops.self_node);
    assert(_msg_out.dst == _remote_node);
    async_write(_msg_out,
      boost::bind(&TCPConnection::handle_write, this, asio::placeholders::error));
  }
}

void TCPConnection::map_to(const node_id_t& node_id) {
  _remote_node = node_id;
}

node_id_t TCPConnection::mapped_to() const {
  return _remote_node;
}

/*
void TCPConnection::extend_timeout() {

  std::cout << "@ " << _timer->get_time_str() << " extending connection timeout for: "
    << _remote_node.str() << std::endl;
  _timeout.expires_from_now(boost::posix_time::microseconds(globops.transport_conn_timeout));
  _timeout.async_wait(boost::bind(&TCPConnection::timeout, this, asio::placeholders::error));
}
*/
  
void TCPConnection::timeout(const asio::error& error) {

  if (!error) {

    Clock curr_time = _timer->get_time();
    Clock extend_by = _last_communication + globops.transport_conn_timeout - curr_time;
    if (extend_by > 0) {
      _timeout.expires_from_now(boost::posix_time::microseconds(extend_by));
      _timeout.async_wait(boost::bind(&TCPConnection::timeout, this, asio::placeholders::error));
      return;
    }

    std::cout << "TCPConnection::timeout. Last communication at: "
      << _timer->get_time_str(_last_communication) << std::endl;

    if(!_to_be_deleted) {
      _tcp_conn_mgr->disconnect(_remote_node, this);
    }

  } else if (error != asio::error::operation_aborted) {
    std::cerr << "TCPConnection::timeout -> error: " << error << std::endl;
  }
}

void TCPConnection::handle_write(const asio::error& error) {

  assert(_send_in_progress);
  _send_in_progress = false;

  delete(_msg_out.data);

  if (!error) {

    //extend_timeout();
/*
    std::cout << "@ " << _timer->get_time_str() << " done writing, successful"
      << std::endl;
*/

    if (_pending.size() > 0) {
  
      // send first message
      vector<Message>::iterator it = _pending.begin();
  
      _send_in_progress = true;
      _msg_out = *it;
  
      assert(_msg_out.src == globops.self_node);
      assert(_msg_out.dst == _remote_node);
      async_write(_msg_out,
        boost::bind(&TCPConnection::handle_write, this, asio::placeholders::error));
  
      // remove messages 
      _pending.erase(it);
    }
  
    return;
  }
  
  std::cerr << "whooooops. TCP write error @" << _timer->get_time_str() << ":"
    << error << std::endl;
  if(!_to_be_deleted) {
    _tcp_conn_mgr->disconnect(_remote_node, this);
  }
}

/// Asynchronously write a data structure to the socket.
template <typename T, typename Handler>
void TCPConnection::async_write(const T& t, Handler handler) {

  assert(_send_in_progress);

  // Serialize the data first so we know how large it is.
  std::ostringstream archive_stream;
  boost::archive::text_oarchive archive(archive_stream);
  try {
    archive << t;
  } catch (std::exception& e) {

    // Unable to decode data.
    std::cerr << "error in writing tcp data to archive (internal): " << e.what() << std::endl;
    assert(false);
    return;
  }

  _outbound_data = archive_stream.str();

  // Format the header.
  std::ostringstream header_stream;
  header_stream << std::setw(HEADER_LENGTH)
    << std::hex << _outbound_data.size();
  if (!header_stream || header_stream.str().size() != HEADER_LENGTH) {
    // Something went wrong, inform the caller.
    asio::error error(asio::error::invalid_argument);
    _socket.io_service().post(boost::bind(handler, error));
    return;
  }

  _outbound_header = header_stream.str();
  _outbound_md5sum = md5(_outbound_data.c_str()).digest().hex_str_value();
  assert(_outbound_md5sum.size() == MD5_LENGTH);

  // Write the serialized data to the socket. We use "gather-write" to send
  // both the header and the data in a single write operation.
  std::vector<asio::const_buffer> buffers;
  buffers.push_back(asio::buffer(_outbound_header));
  buffers.push_back(asio::buffer(_outbound_md5sum));
  buffers.push_back(asio::buffer(_outbound_data));

  asio::async_write(_socket, buffers, handler);
}

/// Handle completion of a read operation.
void TCPConnection::handle_read(const asio::error& error) {

  if (!error) { 

    if (_pkts_read == 0 && _remote_node != _msg_in.src) {
      _tcp_conn_mgr->remap(this, _remote_node, _msg_in.src);
      _pkts_read++;
    }
   
    if (_msg_in.dst != globops.self_node) {
      goto TCP_MALFORMED_MESSAGE;
    }

    TransportEventData* data = new TransportEventData;
    data->msg = _msg_in;

    Event event;
    event.node_id = _msg_in.dst;
    event.layer = LYR_TRANSPORT;
    //event.whence = (Timer::get_instance())->get_time();
    event.flag = EVT_TRANSPORT_RECV;
    event.data = data;

    (Transport::get_instance())->transport_event_callback(event);
    _last_communication = _timer->get_time();

    if (!_deactivate) {

      //extend_timeout();

      async_read(_msg_in,
        boost::bind(&TCPConnection::handle_read, this,
          asio::placeholders::error));

    } else {

      // keep track of memory to de-allocate (a bit later)
      mark_to_be_deleted();
      terminate();
    }

    return;
  }

TCP_MALFORMED_MESSAGE:
  // An error occurred.
  Clock curr_time = _timer->get_time();
  Clock age = curr_time - _last_communication;
  if (error != asio::error::operation_aborted 
    && age < globops.transport_conn_timeout - globops.rappel_reply_timeout) {
    // too young to be disconnected -- there is some problem
    std::cerr << "whoooops. TCP read error @" << _timer->get_time_str(curr_time)
     << " from " << _remote_node.str() << ": " << error << "(connection age: "
     << _timer->get_time_str(age) << ")" << std::endl;
  }
  if (!_to_be_deleted) {
    _tcp_conn_mgr->disconnect(_remote_node, this);
  }
}

/// Asynchronously read a data structure from the socket.
template <typename T, typename Handler>
void TCPConnection::async_read(T& t, Handler handler) {

  // Issue a read operation to read exactly the number of bytes in a header.
  void (TCPConnection::*f)(const asio::error&, T&, boost::tuple<Handler>)
    = &TCPConnection::handle_read_header<T, Handler>;

  asio::async_read(_socket, asio::buffer(_inbound_header),
      boost::bind(f,
        this, asio::placeholders::error, boost::ref(t),
        boost::make_tuple(handler)));
}

/// Handle a completed read of a message header. The handler is passed using
/// a tuple since boost::bind seems to have trouble binding a function object
/// created using boost::bind as a parameter.
template <typename T, typename Handler>
void TCPConnection::handle_read_header(const asio::error& error, T& t,
  boost::tuple<Handler> handler) {

  if (error) {

    // std::cerr << "error in reading header: " << error << std::endl;
    boost::get<0>(handler)(error);   

  } else {

    // Determine the length of the serialized data.
    std::istringstream is(std::string(_inbound_header, HEADER_LENGTH));
    std::size_t inbound_data_size = 0;

    if (!(is >> std::hex >> inbound_data_size)) {

      // Header doesn't seem to be valid. Inform the caller.
      std::cerr << "error in reading header (internal)" << std::endl;
      asio::error error(asio::error::invalid_argument);
      boost::get<0>(handler)(error);
      return;
    }

    // Start an asynchronous call to receive the data.
    _inbound_data.resize(MD5_LENGTH + inbound_data_size);

    void (TCPConnection::*f)(const asio::error&, T&, boost::tuple<Handler>)
      = &TCPConnection::handle_read_data<T, Handler>;

    asio::async_read(_socket, asio::buffer(_inbound_data),
      boost::bind(f, this, asio::placeholders::error, boost::ref(t), handler));
  }
}

/// Handle a completed read of message data.
template <typename T, typename Handler>
void TCPConnection::handle_read_data(const asio::error& error, T& t,
  boost::tuple<Handler> handler) {

  if (error) {

    std::cerr << "error in reading data: " << error << std::endl;
    boost::get<0>(handler)(error);

  } else {

    std::string md5sum_read(&_inbound_data[0], MD5_LENGTH);
    std::string archive_data(&_inbound_data[MD5_LENGTH],
      _inbound_data.size() - MD5_LENGTH);

    std::string md5sum_calculated
      = md5(archive_data.c_str()).digest().hex_str_value();

    assert(md5sum_read == md5sum_calculated);

    // Extract the data structure from the data just received.
    try {

      std::istringstream archive_stream(archive_data);
      boost::archive::text_iarchive archive(archive_stream);
      archive >> t;

    } catch (std::exception& e) {

      // Unable to decode data.
      std::cerr << "error in reading data (internal): " << e.what() << std::endl;

      asio::error error(asio::error::invalid_argument);
      boost::get<0>(handler)(error);
      return;
    }

    // Inform caller that data has been received ok.
    boost::get<0>(handler)(error);
  }
}
#endif // COMPILE_FOR == NETWORK
