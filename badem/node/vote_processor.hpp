#pragma once

#include <badem/lib/numbers.hpp>
#include <badem/lib/utility.hpp>
#include <badem/secure/common.hpp>

#include <boost/thread/thread.hpp>

#include <deque>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace badem
{
class node;
class transaction;
namespace transport
{
	class channel;
}

class vote_processor final
{
public:
	explicit vote_processor (badem::node &);
	void vote (std::shared_ptr<badem::vote>, std::shared_ptr<badem::transport::channel>);
	/** Note: node.active.mutex lock is required */
	badem::vote_code vote_blocking (badem::transaction const &, std::shared_ptr<badem::vote>, std::shared_ptr<badem::transport::channel>, bool = false);
	void verify_votes (std::deque<std::pair<std::shared_ptr<badem::vote>, std::shared_ptr<badem::transport::channel>>> &);
	void flush ();
	void calculate_weights ();
	badem::node & node;
	void stop ();

private:
	void process_loop ();
	std::deque<std::pair<std::shared_ptr<badem::vote>, std::shared_ptr<badem::transport::channel>>> votes;
	/** Representatives levels for random early detection */
	std::unordered_set<badem::account> representatives_1;
	std::unordered_set<badem::account> representatives_2;
	std::unordered_set<badem::account> representatives_3;
	std::condition_variable condition;
	std::mutex mutex;
	bool started;
	bool stopped;
	bool active;
	boost::thread thread;

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_processor & vote_processor, const std::string & name);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_processor & vote_processor, const std::string & name);
}
