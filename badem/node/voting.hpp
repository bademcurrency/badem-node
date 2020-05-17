#pragma once

#include <badem/lib/config.hpp>
#include <badem/lib/numbers.hpp>
#include <badem/lib/utility.hpp>
#include <badem/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/thread.hpp>

#include <condition_variable>
#include <deque>
#include <mutex>

namespace badem
{
class node;
class vote_generator final
{
public:
	vote_generator (badem::node &);
	void add (badem::block_hash const &);
	void stop ();

private:
	void run ();
	void send (badem::unique_lock<std::mutex> &);
	badem::node & node;
	std::mutex mutex;
	badem::condition_variable condition;
	std::deque<badem::block_hash> hashes;
	badem::network_params network_params;
	bool stopped{ false };
	bool started{ false };
	boost::thread thread;

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_generator & vote_generator, const std::string & name);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_generator & vote_generator, const std::string & name);
class cached_votes final
{
public:
	std::chrono::steady_clock::time_point time;
	badem::block_hash hash;
	std::vector<std::shared_ptr<badem::vote>> votes;
};
class votes_cache final
{
public:
	void add (std::shared_ptr<badem::vote> const &);
	std::vector<std::shared_ptr<badem::vote>> find (badem::block_hash const &);
	void remove (badem::block_hash const &);

private:
	std::mutex cache_mutex;
	boost::multi_index_container<
	badem::cached_votes,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<badem::cached_votes, std::chrono::steady_clock::time_point, &badem::cached_votes::time>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<badem::cached_votes, badem::block_hash, &badem::cached_votes::hash>>>>
	cache;
	badem::network_params network_params;
	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (votes_cache & votes_cache, const std::string & name);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (votes_cache & votes_cache, const std::string & name);
}
