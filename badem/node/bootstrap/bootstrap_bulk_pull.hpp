#pragma once

#include <badem/node/common.hpp>
#include <badem/node/socket.hpp>

#include <unordered_set>

namespace badem
{
class pull_info
{
public:
	using count_t = badem::bulk_pull::count_t;
	pull_info () = default;
	pull_info (badem::hash_or_account const &, badem::block_hash const &, badem::block_hash const &, count_t = 0, unsigned = 16);
	badem::hash_or_account account_or_head{ 0 };
	badem::block_hash head{ 0 };
	badem::block_hash head_original{ 0 };
	badem::block_hash end{ 0 };
	count_t count{ 0 };
	unsigned attempts{ 0 };
	uint64_t processed{ 0 };
	unsigned retry_limit{ 0 };
};
class bootstrap_client;
class bulk_pull_client final : public std::enable_shared_from_this<badem::bulk_pull_client>
{
public:
	bulk_pull_client (std::shared_ptr<badem::bootstrap_client>, badem::pull_info const &);
	~bulk_pull_client ();
	void request ();
	void receive_block ();
	void throttled_receive_block ();
	void received_type ();
	void received_block (boost::system::error_code const &, size_t, badem::block_type);
	badem::block_hash first ();
	std::shared_ptr<badem::bootstrap_client> connection;
	badem::block_hash expected;
	badem::account known_account;
	badem::pull_info pull;
	uint64_t pull_blocks;
	uint64_t unexpected_count;
	bool network_error{ false };
};
class bulk_pull_account_client final : public std::enable_shared_from_this<badem::bulk_pull_account_client>
{
public:
	bulk_pull_account_client (std::shared_ptr<badem::bootstrap_client>, badem::account const &);
	~bulk_pull_account_client ();
	void request ();
	void receive_pending ();
	std::shared_ptr<badem::bootstrap_client> connection;
	badem::account account;
	uint64_t pull_blocks;
};
class bootstrap_server;
class bulk_pull;
class bulk_pull_server final : public std::enable_shared_from_this<badem::bulk_pull_server>
{
public:
	bulk_pull_server (std::shared_ptr<badem::bootstrap_server> const &, std::unique_ptr<badem::bulk_pull>);
	void set_current_end ();
	std::shared_ptr<badem::block> get_next ();
	void send_next ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void no_block_sent (boost::system::error_code const &, size_t);
	std::shared_ptr<badem::bootstrap_server> connection;
	std::unique_ptr<badem::bulk_pull> request;
	badem::block_hash current;
	bool include_start;
	badem::bulk_pull::count_t max_count;
	badem::bulk_pull::count_t sent_count;
};
class bulk_pull_account;
class bulk_pull_account_server final : public std::enable_shared_from_this<badem::bulk_pull_account_server>
{
public:
	bulk_pull_account_server (std::shared_ptr<badem::bootstrap_server> const &, std::unique_ptr<badem::bulk_pull_account>);
	void set_params ();
	std::pair<std::unique_ptr<badem::pending_key>, std::unique_ptr<badem::pending_info>> get_next ();
	void send_frontier ();
	void send_next_block ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void complete (boost::system::error_code const &, size_t);
	std::shared_ptr<badem::bootstrap_server> connection;
	std::unique_ptr<badem::bulk_pull_account> request;
	std::unordered_set<badem::uint256_union> deduplication;
	badem::pending_key current_key;
	bool pending_address_only;
	bool pending_include_address;
	bool invalid_request;
};
}
