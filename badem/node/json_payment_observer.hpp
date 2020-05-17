#pragma once

#include <badem/node/node_observers.hpp>
#include <badem/node/wallet.hpp>

#include <string>
#include <vector>

namespace badem
{
class node;

enum class payment_status
{
	not_a_status,
	unknown,
	nothing, // Timeout and nothing was received
	//insufficient, // Timeout and not enough was received
	//over, // More than requested received
	//success_fork, // Amount received but it involved a fork
	success // Amount received
};
class json_payment_observer final : public std::enable_shared_from_this<badem::json_payment_observer>
{
public:
	json_payment_observer (badem::node &, std::function<void(std::string const &)> const &, badem::account const &, badem::amount const &);
	void start (uint64_t);
	void observe ();
	void complete (badem::payment_status);
	std::mutex mutex;
	badem::condition_variable condition;
	badem::node & node;
	badem::account account;
	badem::amount amount;
	std::function<void(std::string const &)> response;
	std::atomic_flag completed;
};
}
