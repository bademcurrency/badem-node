#pragma once

#include <badem/node/common.hpp>
#include <badem/node/socket.hpp>

namespace badem
{
class transaction;
class bootstrap_client;
class frontier_req_client final : public std::enable_shared_from_this<badem::frontier_req_client>
{
public:
	explicit frontier_req_client (std::shared_ptr<badem::bootstrap_client>);
	~frontier_req_client ();
	void run ();
	void receive_frontier ();
	void received_frontier (boost::system::error_code const &, size_t);
	void unsynced (badem::block_hash const &, badem::block_hash const &);
	void next (badem::transaction const &);
	std::shared_ptr<badem::bootstrap_client> connection;
	badem::account current;
	badem::block_hash frontier;
	unsigned count;
	badem::account landing;
	badem::account faucet;
	std::chrono::steady_clock::time_point start_time;
	std::promise<bool> promise;
	/** A very rough estimate of the cost of `bulk_push`ing missing blocks */
	uint64_t bulk_push_cost;
	std::deque<std::pair<badem::account, badem::block_hash>> accounts;
	static size_t constexpr size_frontier = sizeof (badem::account) + sizeof (badem::block_hash);
};
class bootstrap_server;
class frontier_req;
class frontier_req_server final : public std::enable_shared_from_this<badem::frontier_req_server>
{
public:
	frontier_req_server (std::shared_ptr<badem::bootstrap_server> const &, std::unique_ptr<badem::frontier_req>);
	void send_next ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void no_block_sent (boost::system::error_code const &, size_t);
	void next ();
	std::shared_ptr<badem::bootstrap_server> connection;
	badem::account current;
	badem::block_hash frontier;
	std::unique_ptr<badem::frontier_req> request;
	size_t count;
	std::deque<std::pair<badem::account, badem::block_hash>> accounts;
};
}
