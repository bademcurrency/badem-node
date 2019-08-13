#pragma once

#include <badem/lib/numbers.hpp>
#include <badem/lib/utility.hpp>
#include <badem/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

namespace badem
{
class node;
class transaction;

/** For each gap in account chains, track arrival time and voters */
class gap_information final
{
public:
	std::chrono::steady_clock::time_point arrival;
	badem::block_hash hash;
	std::vector<badem::account> voters;
};

/** Maintains voting and arrival information for gaps (missing source or previous blocks in account chains) */
class gap_cache final
{
public:
	explicit gap_cache (badem::node &);
	void add (badem::transaction const &, badem::block_hash const &, std::chrono::steady_clock::time_point = std::chrono::steady_clock::now ());
	void erase (badem::block_hash const & hash_a);
	void vote (std::shared_ptr<badem::vote>);
	badem::uint128_t bootstrap_threshold (badem::transaction const &);
	size_t size ();
	boost::multi_index_container<
	badem::gap_information,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<gap_information, std::chrono::steady_clock::time_point, &gap_information::arrival>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<gap_information, badem::block_hash, &gap_information::hash>>>>
	blocks;
	size_t const max = 256;
	std::mutex mutex;
	badem::node & node;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (gap_cache & gap_cache, const std::string & name);
}
