#include <badem/secure/blockstore.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/polymorphic_cast.hpp>

badem::block_sideband::block_sideband (badem::block_type type_a, badem::account const & account_a, badem::block_hash const & successor_a, badem::amount const & balance_a, uint64_t height_a, uint64_t timestamp_a) :
type (type_a),
successor (successor_a),
account (account_a),
balance (balance_a),
height (height_a),
timestamp (timestamp_a)
{
}

size_t badem::block_sideband::size (badem::block_type type_a)
{
	size_t result (0);
	result += sizeof (successor);
	if (type_a != badem::block_type::state && type_a != badem::block_type::open)
	{
		result += sizeof (account);
	}
	if (type_a != badem::block_type::open)
	{
		result += sizeof (height);
	}
	if (type_a == badem::block_type::receive || type_a == badem::block_type::change || type_a == badem::block_type::open)
	{
		result += sizeof (balance);
	}
	result += sizeof (timestamp);
	return result;
}

void badem::block_sideband::serialize (badem::stream & stream_a) const
{
	badem::write (stream_a, successor.bytes);
	if (type != badem::block_type::state && type != badem::block_type::open)
	{
		badem::write (stream_a, account.bytes);
	}
	if (type != badem::block_type::open)
	{
		badem::write (stream_a, boost::endian::native_to_big (height));
	}
	if (type == badem::block_type::receive || type == badem::block_type::change || type == badem::block_type::open)
	{
		badem::write (stream_a, balance.bytes);
	}
	badem::write (stream_a, boost::endian::native_to_big (timestamp));
}

bool badem::block_sideband::deserialize (badem::stream & stream_a)
{
	bool result (false);
	try
	{
		badem::read (stream_a, successor.bytes);
		if (type != badem::block_type::state && type != badem::block_type::open)
		{
			badem::read (stream_a, account.bytes);
		}
		if (type != badem::block_type::open)
		{
			badem::read (stream_a, height);
			boost::endian::big_to_native_inplace (height);
		}
		else
		{
			height = 1;
		}
		if (type == badem::block_type::receive || type == badem::block_type::change || type == badem::block_type::open)
		{
			badem::read (stream_a, balance.bytes);
		}
		badem::read (stream_a, timestamp);
		boost::endian::big_to_native_inplace (timestamp);
	}
	catch (std::runtime_error &)
	{
		result = true;
	}

	return result;
}

badem::summation_visitor::summation_visitor (badem::transaction const & transaction_a, badem::block_store const & store_a) :
transaction (transaction_a),
store (store_a)
{
}

void badem::summation_visitor::send_block (badem::send_block const & block_a)
{
	assert (current->type != summation_type::invalid && current != nullptr);
	if (current->type == summation_type::amount)
	{
		sum_set (block_a.hashables.balance.number ());
		current->balance_hash = block_a.hashables.previous;
		current->amount_hash = 0;
	}
	else
	{
		sum_add (block_a.hashables.balance.number ());
		current->balance_hash = 0;
	}
}

void badem::summation_visitor::state_block (badem::state_block const & block_a)
{
	assert (current->type != summation_type::invalid && current != nullptr);
	sum_set (block_a.hashables.balance.number ());
	if (current->type == summation_type::amount)
	{
		current->balance_hash = block_a.hashables.previous;
		current->amount_hash = 0;
	}
	else
	{
		current->balance_hash = 0;
	}
}

void badem::summation_visitor::receive_block (badem::receive_block const & block_a)
{
	assert (current->type != summation_type::invalid && current != nullptr);
	if (current->type == summation_type::amount)
	{
		current->amount_hash = block_a.hashables.source;
	}
	else
	{
		badem::block_info block_info;
		if (!store.block_info_get (transaction, block_a.hash (), block_info))
		{
			sum_add (block_info.balance.number ());
			current->balance_hash = 0;
		}
		else
		{
			current->amount_hash = block_a.hashables.source;
			current->balance_hash = block_a.hashables.previous;
		}
	}
}

void badem::summation_visitor::open_block (badem::open_block const & block_a)
{
	assert (current->type != summation_type::invalid && current != nullptr);
	if (current->type == summation_type::amount)
	{
		if (block_a.hashables.source != network_params.ledger.genesis_account)
		{
			current->amount_hash = block_a.hashables.source;
		}
		else
		{
			sum_set (network_params.ledger.genesis_amount);
			current->amount_hash = 0;
		}
	}
	else
	{
		current->amount_hash = block_a.hashables.source;
		current->balance_hash = 0;
	}
}

void badem::summation_visitor::change_block (badem::change_block const & block_a)
{
	assert (current->type != summation_type::invalid && current != nullptr);
	if (current->type == summation_type::amount)
	{
		sum_set (0);
		current->amount_hash = 0;
	}
	else
	{
		badem::block_info block_info;
		if (!store.block_info_get (transaction, block_a.hash (), block_info))
		{
			sum_add (block_info.balance.number ());
			current->balance_hash = 0;
		}
		else
		{
			current->balance_hash = block_a.hashables.previous;
		}
	}
}

badem::summation_visitor::frame badem::summation_visitor::push (badem::summation_visitor::summation_type type_a, badem::block_hash const & hash_a)
{
	frames.emplace (type_a, type_a == summation_type::balance ? hash_a : 0, type_a == summation_type::amount ? hash_a : 0);
	return frames.top ();
}

void badem::summation_visitor::sum_add (badem::uint128_t addend_a)
{
	current->sum += addend_a;
	result = current->sum;
}

void badem::summation_visitor::sum_set (badem::uint128_t value_a)
{
	current->sum = value_a;
	result = current->sum;
}

badem::uint128_t badem::summation_visitor::compute_internal (badem::summation_visitor::summation_type type_a, badem::block_hash const & hash_a)
{
	push (type_a, hash_a);

	/*
	 Invocation loop representing balance and amount computations calling each other.
	 This is usually better done by recursion or something like boost::coroutine2, but
	 segmented stacks are not supported on all platforms so we do it manually to avoid
	 stack overflow (the mutual calls are not tail-recursive so we cannot rely on the
	 compiler optimizing that into a loop, though a future alternative is to do a
	 CPS-style implementation to enforce tail calls.)
	*/
	while (!frames.empty ())
	{
		current = &frames.top ();
		assert (current->type != summation_type::invalid && current != nullptr);

		if (current->type == summation_type::balance)
		{
			if (current->awaiting_result)
			{
				sum_add (current->incoming_result);
				current->awaiting_result = false;
			}

			while (!current->awaiting_result && (!current->balance_hash.is_zero () || !current->amount_hash.is_zero ()))
			{
				if (!current->amount_hash.is_zero ())
				{
					// Compute amount
					current->awaiting_result = true;
					push (summation_type::amount, current->amount_hash);
					current->amount_hash = 0;
				}
				else
				{
					auto block (store.block_get (transaction, current->balance_hash));
					assert (block != nullptr);
					block->visit (*this);
				}
			}

			epilogue ();
		}
		else if (current->type == summation_type::amount)
		{
			if (current->awaiting_result)
			{
				sum_set (current->sum < current->incoming_result ? current->incoming_result - current->sum : current->sum - current->incoming_result);
				current->awaiting_result = false;
			}

			while (!current->awaiting_result && (!current->amount_hash.is_zero () || !current->balance_hash.is_zero ()))
			{
				if (!current->amount_hash.is_zero ())
				{
					auto block (store.block_get (transaction, current->amount_hash));
					if (block != nullptr)
					{
						block->visit (*this);
					}
					else
					{
						if (current->amount_hash == network_params.ledger.genesis_account)
						{
							sum_set (std::numeric_limits<badem::uint128_t>::max ());
							current->amount_hash = 0;
						}
						else
						{
							assert (false);
							sum_set (0);
							current->amount_hash = 0;
						}
					}
				}
				else
				{
					// Compute balance
					current->awaiting_result = true;
					push (summation_type::balance, current->balance_hash);
					current->balance_hash = 0;
				}
			}

			epilogue ();
		}
	}

	return result;
}

void badem::summation_visitor::epilogue ()
{
	if (!current->awaiting_result)
	{
		frames.pop ();
		if (!frames.empty ())
		{
			frames.top ().incoming_result = current->sum;
		}
	}
}

badem::uint128_t badem::summation_visitor::compute_amount (badem::block_hash const & block_hash)
{
	return compute_internal (summation_type::amount, block_hash);
}

badem::uint128_t badem::summation_visitor::compute_balance (badem::block_hash const & block_hash)
{
	return compute_internal (summation_type::balance, block_hash);
}

badem::representative_visitor::representative_visitor (badem::transaction const & transaction_a, badem::block_store & store_a) :
transaction (transaction_a),
store (store_a),
result (0)
{
}

void badem::representative_visitor::compute (badem::block_hash const & hash_a)
{
	current = hash_a;
	while (result.is_zero ())
	{
		auto block (store.block_get (transaction, current));
		assert (block != nullptr);
		block->visit (*this);
	}
}

void badem::representative_visitor::send_block (badem::send_block const & block_a)
{
	current = block_a.previous ();
}

void badem::representative_visitor::receive_block (badem::receive_block const & block_a)
{
	current = block_a.previous ();
}

void badem::representative_visitor::open_block (badem::open_block const & block_a)
{
	result = block_a.hash ();
}

void badem::representative_visitor::change_block (badem::change_block const & block_a)
{
	result = block_a.hash ();
}

void badem::representative_visitor::state_block (badem::state_block const & block_a)
{
	result = block_a.hash ();
}

badem::read_transaction::read_transaction (std::unique_ptr<badem::read_transaction_impl> read_transaction_impl) :
impl (std::move (read_transaction_impl))
{
}

void * badem::read_transaction::get_handle () const
{
	return impl->get_handle ();
}

void badem::read_transaction::reset () const
{
	impl->reset ();
}

void badem::read_transaction::renew () const
{
	impl->renew ();
}

void badem::read_transaction::refresh () const
{
	reset ();
	renew ();
}

badem::write_transaction::write_transaction (std::unique_ptr<badem::write_transaction_impl> write_transaction_impl) :
impl (std::move (write_transaction_impl))
{
}

void * badem::write_transaction::get_handle () const
{
	return impl->get_handle ();
}

void badem::write_transaction::commit () const
{
	impl->commit ();
}

void badem::write_transaction::renew ()
{
	impl->renew ();
}
