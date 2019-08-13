#include <badem/node/json_payment_observer.hpp>
#include <badem/node/payment_observer_processor.hpp>

badem::payment_observer_processor::payment_observer_processor (badem::node_observers::blocks_t & blocks)
{
	blocks.add ([this](badem::election_status const &, badem::account const & account_a, badem::uint128_t const &, bool) {
		observer_action (account_a);
	});
}

void badem::payment_observer_processor::observer_action (badem::account const & account_a)
{
	std::shared_ptr<badem::json_payment_observer> observer;
	{
		std::lock_guard<std::mutex> lock (mutex);
		auto existing (payment_observers.find (account_a));
		if (existing != payment_observers.end ())
		{
			observer = existing->second;
		}
	}
	if (observer != nullptr)
	{
		observer->observe ();
	}
}

void badem::payment_observer_processor::add (badem::account const & account_a, std::shared_ptr<badem::json_payment_observer> payment_observer_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	assert (payment_observers.find (account_a) == payment_observers.end ());
	payment_observers[account_a] = payment_observer_a;
}

void badem::payment_observer_processor::erase (badem::account & account_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	assert (payment_observers.find (account_a) != payment_observers.end ());
	payment_observers.erase (account_a);
}
