#pragma once

#include <badem/lib/blocks.hpp>
#include <badem/node/voting.hpp>
#include <badem/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <memory>
#include <unordered_set>

namespace badem
{
class node;
class transaction;
class write_transaction;
class write_database_queue;

class rolled_hash
{
public:
	std::chrono::steady_clock::time_point time;
	badem::block_hash hash;
};
/**
 * Processing blocks is a potentially long IO operation.
 * This class isolates block insertion from other operations like servicing network operations
 */
class block_processor final
{
public:
	explicit block_processor (badem::node &, badem::write_database_queue &);
	~block_processor ();
	void stop ();
	void flush ();
	size_t size ();
	bool full ();
	bool half_full ();
	void add (badem::unchecked_info const &);
	void add (std::shared_ptr<badem::block>, uint64_t = 0);
	void force (std::shared_ptr<badem::block>);
	void wait_write ();
	bool should_log (bool);
	bool have_blocks ();
	void process_blocks ();
	badem::process_return process_one (badem::write_transaction const &, badem::unchecked_info, const bool = false);
	badem::process_return process_one (badem::write_transaction const &, std::shared_ptr<badem::block>, const bool = false);
	badem::vote_generator generator;
	// Delay required for average network propagartion before requesting confirmation
	static std::chrono::milliseconds constexpr confirmation_request_delay{ 1500 };

private:
	void queue_unchecked (badem::write_transaction const &, badem::block_hash const &);
	void verify_state_blocks (badem::unique_lock<std::mutex> &, size_t = std::numeric_limits<size_t>::max ());
	void process_batch (badem::unique_lock<std::mutex> &);
	void process_live (badem::block_hash const &, std::shared_ptr<badem::block>, const bool = false);
	void requeue_invalid (badem::block_hash const &, badem::unchecked_info const &);
	bool stopped;
	bool active;
	bool awaiting_write{ false };
	std::chrono::steady_clock::time_point next_log;
	std::deque<badem::unchecked_info> state_blocks;
	std::deque<badem::unchecked_info> blocks;
	std::deque<std::shared_ptr<badem::block>> forced;
	badem::block_hash filter_item (badem::block_hash const &, badem::signature const &);
	std::unordered_set<badem::block_hash> blocks_filter;
	boost::multi_index_container<
	badem::rolled_hash,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<badem::rolled_hash, std::chrono::steady_clock::time_point, &badem::rolled_hash::time>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<badem::rolled_hash, badem::block_hash, &badem::rolled_hash::hash>>>>
	rolled_back;
	static size_t const rolled_back_max = 1024;
	badem::condition_variable condition;
	badem::node & node;
	badem::write_database_queue & write_database_queue;
	std::mutex mutex;

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_processor & block_processor, const std::string & name);
};
}
