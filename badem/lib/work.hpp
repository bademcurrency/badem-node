#pragma once

#include <badem/lib/config.hpp>
#include <badem/lib/numbers.hpp>
#include <badem/lib/utility.hpp>

#include <boost/optional.hpp>
#include <boost/thread/thread.hpp>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <thread>

namespace badem
{
class block;
bool work_validate (badem::block_hash const &, uint64_t, uint64_t * = nullptr);
bool work_validate (badem::block const &, uint64_t * = nullptr);
uint64_t work_value (badem::block_hash const &, uint64_t);
class opencl_work;
class work_item final
{
public:
	badem::uint256_union item;
	std::function<void(boost::optional<uint64_t> const &)> callback;
	uint64_t difficulty;
};
class work_pool final
{
public:
	work_pool (unsigned, std::chrono::nanoseconds = std::chrono::nanoseconds (0), std::function<boost::optional<uint64_t> (badem::uint256_union const &, uint64_t)> = nullptr);
	~work_pool ();
	void loop (uint64_t);
	void stop ();
	void cancel (badem::uint256_union const &);
	void generate (badem::uint256_union const &, std::function<void(boost::optional<uint64_t> const &)>);
	void generate (badem::uint256_union const &, std::function<void(boost::optional<uint64_t> const &)>, uint64_t);
	uint64_t generate (badem::uint256_union const &);
	uint64_t generate (badem::uint256_union const &, uint64_t);
	badem::network_constants network_constants;
	std::atomic<int> ticket;
	bool done;
	std::vector<boost::thread> threads;
	std::list<badem::work_item> pending;
	std::mutex mutex;
	std::condition_variable producer_condition;
	std::chrono::nanoseconds pow_rate_limiter;
	std::function<boost::optional<uint64_t> (badem::uint256_union const &, uint64_t)> opencl;
	badem::observer_set<bool> work_observers;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (work_pool & work_pool, const std::string & name);
}
