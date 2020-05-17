#pragma once

#include <badem/node/common.hpp>
#include <badem/node/socket.hpp>

#include <atomic>
#include <queue>

namespace badem
{
class bootstrap_server;
class bootstrap_listener final
{
public:
	bootstrap_listener (uint16_t, badem::node &);
	void start ();
	void stop ();
	void accept_action (boost::system::error_code const &, std::shared_ptr<badem::socket>);
	size_t connection_count ();

	std::mutex mutex;
	std::unordered_map<badem::bootstrap_server *, std::weak_ptr<badem::bootstrap_server>> connections;
	badem::tcp_endpoint endpoint ();
	badem::node & node;
	std::shared_ptr<badem::server_socket> listening_socket;
	bool on;
	std::atomic<size_t> bootstrap_count{ 0 };
	std::atomic<size_t> realtime_count{ 0 };

private:
	uint16_t port;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (bootstrap_listener & bootstrap_listener, const std::string & name);

class message;
enum class bootstrap_server_type
{
	undefined,
	bootstrap,
	realtime,
	realtime_response_server // special type for tcp channel response server
};
class bootstrap_server final : public std::enable_shared_from_this<badem::bootstrap_server>
{
public:
	bootstrap_server (std::shared_ptr<badem::socket>, std::shared_ptr<badem::node>);
	~bootstrap_server ();
	void stop ();
	void receive ();
	void receive_header_action (boost::system::error_code const &, size_t);
	void receive_bulk_pull_action (boost::system::error_code const &, size_t, badem::message_header const &);
	void receive_bulk_pull_account_action (boost::system::error_code const &, size_t, badem::message_header const &);
	void receive_frontier_req_action (boost::system::error_code const &, size_t, badem::message_header const &);
	void receive_keepalive_action (boost::system::error_code const &, size_t, badem::message_header const &);
	void receive_publish_action (boost::system::error_code const &, size_t, badem::message_header const &);
	void receive_confirm_req_action (boost::system::error_code const &, size_t, badem::message_header const &);
	void receive_confirm_ack_action (boost::system::error_code const &, size_t, badem::message_header const &);
	void receive_node_id_handshake_action (boost::system::error_code const &, size_t, badem::message_header const &);
	void add_request (std::unique_ptr<badem::message>);
	void finish_request ();
	void finish_request_async ();
	void timeout ();
	void run_next ();
	bool is_bootstrap_connection ();
	bool is_realtime_connection ();
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	std::shared_ptr<badem::socket> socket;
	std::shared_ptr<badem::node> node;
	std::mutex mutex;
	std::queue<std::unique_ptr<badem::message>> requests;
	std::atomic<bool> stopped{ false };
	std::atomic<badem::bootstrap_server_type> type{ badem::bootstrap_server_type::undefined };
	// Remote enpoint used to remove response channel even after socket closing
	badem::tcp_endpoint remote_endpoint{ boost::asio::ip::address_v6::any (), 0 };
	badem::account remote_node_id{ 0 };
};
}
