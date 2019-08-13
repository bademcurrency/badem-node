#pragma once

#include <badem/node/node_observers.hpp>

namespace badem
{
class json_payment_observer;

class payment_observer_processor final
{
public:
	explicit payment_observer_processor (badem::node_observers::blocks_t & blocks);
	void observer_action (badem::account const & account_a);
	void add (badem::account const & account_a, std::shared_ptr<badem::json_payment_observer> payment_observer_a);
	void erase (badem::account & account_a);

private:
	std::mutex mutex;
	std::unordered_map<badem::account, std::shared_ptr<badem::json_payment_observer>> payment_observers;
};
}
