#pragma once

#include <badem/node/common.hpp>
#include <badem/node/socket.hpp>

namespace badem
{
class transaction;
class bootstrap_client;
class bulk_push_client final : public std::enable_shared_from_this<badem::bulk_push_client>
{
public:
	explicit bulk_push_client (std::shared_ptr<badem::bootstrap_client> const &);
	~bulk_push_client ();
	void start ();
	void push (badem::transaction const &);
	void push_block (badem::block const &);
	void send_finished ();
	std::shared_ptr<badem::bootstrap_client> connection;
	std::promise<bool> promise;
	std::pair<badem::block_hash, badem::block_hash> current_target;
};
class bootstrap_server;
class bulk_push_server final : public std::enable_shared_from_this<badem::bulk_push_server>
{
public:
	explicit bulk_push_server (std::shared_ptr<badem::bootstrap_server> const &);
	void throttled_receive ();
	void receive ();
	void received_type ();
	void received_block (boost::system::error_code const &, size_t, badem::block_type);
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	std::shared_ptr<badem::bootstrap_server> connection;
};
}
