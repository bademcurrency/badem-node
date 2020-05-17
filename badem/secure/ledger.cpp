#include <badem/lib/rep_weights.hpp>
#include <badem/lib/stats.hpp>
#include <badem/lib/utility.hpp>
#include <badem/lib/work.hpp>
#include <badem/secure/blockstore.hpp>
#include <badem/secure/ledger.hpp>

namespace
{
/**
 * Roll back the visited block
 */
class rollback_visitor : public badem::block_visitor
{
public:
	rollback_visitor (badem::write_transaction const & transaction_a, badem::ledger & ledger_a, std::vector<std::shared_ptr<badem::block>> & list_a) :
	transaction (transaction_a),
	ledger (ledger_a),
	list (list_a)
	{
	}
	virtual ~rollback_visitor () = default;
	void send_block (badem::send_block const & block_a) override
	{
		auto hash (block_a.hash ());
		badem::pending_info pending;
		badem::pending_key key (block_a.hashables.destination, hash);
		while (!error && ledger.store.pending_get (transaction, key, pending))
		{
			error = ledger.rollback (transaction, ledger.latest (transaction, block_a.hashables.destination), list);
		}
		if (!error)
		{
			badem::account_info info;
			auto error (ledger.store.account_get (transaction, pending.source, info));
			(void)error;
			assert (!error);
			ledger.store.pending_del (transaction, key);
			ledger.rep_weights.representation_add (info.representative, pending.amount.number ());
			badem::account_info new_info (block_a.hashables.previous, info.representative, info.open_block, ledger.balance (transaction, block_a.hashables.previous), badem::seconds_since_epoch (), info.block_count - 1, badem::epoch::epoch_0);
			ledger.change_latest (transaction, pending.source, info, new_info);
			ledger.store.block_del (transaction, hash);
			ledger.store.frontier_del (transaction, hash);
			ledger.store.frontier_put (transaction, block_a.hashables.previous, pending.source);
			ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
			ledger.stats.inc (badem::stat::type::rollback, badem::stat::detail::send);
		}
	}
	void receive_block (badem::receive_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto amount (ledger.amount (transaction, block_a.hashables.source));
		auto destination_account (ledger.account (transaction, hash));
		auto source_account (ledger.account (transaction, block_a.hashables.source));
		badem::account_info info;
		auto error (ledger.store.account_get (transaction, destination_account, info));
		(void)error;
		assert (!error);
		ledger.rep_weights.representation_add (info.representative, 0 - amount);
		badem::account_info new_info (block_a.hashables.previous, info.representative, info.open_block, ledger.balance (transaction, block_a.hashables.previous), badem::seconds_since_epoch (), info.block_count - 1, badem::epoch::epoch_0);
		ledger.change_latest (transaction, destination_account, info, new_info);
		ledger.store.block_del (transaction, hash);
		ledger.store.pending_put (transaction, badem::pending_key (destination_account, block_a.hashables.source), { source_account, amount, badem::epoch::epoch_0 });
		ledger.store.frontier_del (transaction, hash);
		ledger.store.frontier_put (transaction, block_a.hashables.previous, destination_account);
		ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
		ledger.stats.inc (badem::stat::type::rollback, badem::stat::detail::receive);
	}
	void open_block (badem::open_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto amount (ledger.amount (transaction, block_a.hashables.source));
		auto destination_account (ledger.account (transaction, hash));
		auto source_account (ledger.account (transaction, block_a.hashables.source));
		ledger.rep_weights.representation_add (block_a.representative (), 0 - amount);
		badem::account_info new_info;
		ledger.change_latest (transaction, destination_account, new_info, new_info);
		ledger.store.block_del (transaction, hash);
		ledger.store.pending_put (transaction, badem::pending_key (destination_account, block_a.hashables.source), { source_account, amount, badem::epoch::epoch_0 });
		ledger.store.frontier_del (transaction, hash);
		ledger.stats.inc (badem::stat::type::rollback, badem::stat::detail::open);
	}
	void change_block (badem::change_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto rep_block (ledger.representative (transaction, block_a.hashables.previous));
		auto account (ledger.account (transaction, block_a.hashables.previous));
		badem::account_info info;
		auto error (ledger.store.account_get (transaction, account, info));
		(void)error;
		assert (!error);
		auto balance (ledger.balance (transaction, block_a.hashables.previous));
		auto block = ledger.store.block_get (transaction, rep_block);
		release_assert (block != nullptr);
		auto representative = block->representative ();
		ledger.rep_weights.representation_add (block_a.representative (), 0 - balance);
		ledger.rep_weights.representation_add (representative, balance);
		ledger.store.block_del (transaction, hash);
		badem::account_info new_info (block_a.hashables.previous, representative, info.open_block, info.balance, badem::seconds_since_epoch (), info.block_count - 1, badem::epoch::epoch_0);
		ledger.change_latest (transaction, account, info, new_info);
		ledger.store.frontier_del (transaction, hash);
		ledger.store.frontier_put (transaction, block_a.hashables.previous, account);
		ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
		ledger.stats.inc (badem::stat::type::rollback, badem::stat::detail::change);
	}
	void state_block (badem::state_block const & block_a) override
	{
		auto hash (block_a.hash ());
		badem::block_hash rep_block_hash (0);
		if (!block_a.hashables.previous.is_zero ())
		{
			rep_block_hash = ledger.representative (transaction, block_a.hashables.previous);
		}
		auto balance (ledger.balance (transaction, block_a.hashables.previous));
		auto is_send (block_a.hashables.balance < balance);
		// Add in amount delta
		ledger.rep_weights.representation_add (block_a.representative (), 0 - block_a.hashables.balance.number ());
		badem::account representative{ 0 };
		if (!rep_block_hash.is_zero ())
		{
			// Move existing representation
			auto block (ledger.store.block_get (transaction, rep_block_hash));
			assert (block != nullptr);
			representative = block->representative ();
			ledger.rep_weights.representation_add (representative, balance);
		}

		badem::account_info info;
		auto error (ledger.store.account_get (transaction, block_a.hashables.account, info));

		if (is_send)
		{
			badem::pending_key key (block_a.hashables.link, hash);
			while (!error && !ledger.store.pending_exists (transaction, key))
			{
				error = ledger.rollback (transaction, ledger.latest (transaction, block_a.hashables.link), list);
			}
			ledger.store.pending_del (transaction, key);
			ledger.stats.inc (badem::stat::type::rollback, badem::stat::detail::send);
		}
		else if (!block_a.hashables.link.is_zero () && !ledger.is_epoch_link (block_a.hashables.link))
		{
			auto source_version (ledger.store.block_version (transaction, block_a.hashables.link));
			badem::pending_info pending_info (ledger.account (transaction, block_a.hashables.link), block_a.hashables.balance.number () - balance, source_version);
			ledger.store.pending_put (transaction, badem::pending_key (block_a.hashables.account, block_a.hashables.link), pending_info);
			ledger.stats.inc (badem::stat::type::rollback, badem::stat::detail::receive);
		}

		assert (!error);
		auto previous_version (ledger.store.block_version (transaction, block_a.hashables.previous));
		badem::account_info new_info (block_a.hashables.previous, representative, info.open_block, balance, badem::seconds_since_epoch (), info.block_count - 1, previous_version);
		ledger.change_latest (transaction, block_a.hashables.account, info, new_info);

		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		if (previous != nullptr)
		{
			ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
			if (previous->type () < badem::block_type::state)
			{
				ledger.store.frontier_put (transaction, block_a.hashables.previous, block_a.hashables.account);
			}
		}
		else
		{
			ledger.stats.inc (badem::stat::type::rollback, badem::stat::detail::open);
		}
		ledger.store.block_del (transaction, hash);
	}
	badem::write_transaction const & transaction;
	badem::ledger & ledger;
	std::vector<std::shared_ptr<badem::block>> & list;
	bool error{ false };
};

class ledger_processor : public badem::block_visitor
{
public:
	ledger_processor (badem::ledger &, badem::write_transaction const &, badem::signature_verification = badem::signature_verification::unknown);
	virtual ~ledger_processor () = default;
	void send_block (badem::send_block const &) override;
	void receive_block (badem::receive_block const &) override;
	void open_block (badem::open_block const &) override;
	void change_block (badem::change_block const &) override;
	void state_block (badem::state_block const &) override;
	void state_block_impl (badem::state_block const &);
	void epoch_block_impl (badem::state_block const &);
	badem::ledger & ledger;
	badem::write_transaction const & transaction;
	badem::signature_verification verification;
	badem::process_return result;

private:
	bool validate_epoch_block (badem::state_block const & block_a);
};

// Returns true if this block which has an epoch link is correctly formed.
bool ledger_processor::validate_epoch_block (badem::state_block const & block_a)
{
	assert (ledger.is_epoch_link (block_a.hashables.link));
	badem::amount prev_balance (0);
	if (!block_a.hashables.previous.is_zero ())
	{
		result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? badem::process_result::progress : badem::process_result::gap_previous;
		if (result.code == badem::process_result::progress)
		{
			prev_balance = ledger.balance (transaction, block_a.hashables.previous);
		}
		else if (result.verified == badem::signature_verification::unknown)
		{
			// Check for possible regular state blocks with epoch link (send subtype)
			if (validate_message (block_a.hashables.account, block_a.hash (), block_a.signature))
			{
				// Is epoch block signed correctly
				if (validate_message (ledger.epoch_signer (block_a.link ()), block_a.hash (), block_a.signature))
				{
					result.verified = badem::signature_verification::invalid;
					result.code = badem::process_result::bad_signature;
				}
				else
				{
					result.verified = badem::signature_verification::valid_epoch;
				}
			}
			else
			{
				result.verified = badem::signature_verification::valid;
			}
		}
	}
	return (block_a.hashables.balance == prev_balance);
}

void ledger_processor::state_block (badem::state_block const & block_a)
{
	result.code = badem::process_result::progress;
	auto is_epoch_block = false;
	if (ledger.is_epoch_link (block_a.hashables.link))
	{
		// This function also modifies the result variable if epoch is mal-formed
		is_epoch_block = validate_epoch_block (block_a);
	}

	if (result.code == badem::process_result::progress)
	{
		if (is_epoch_block)
		{
			epoch_block_impl (block_a);
		}
		else
		{
			state_block_impl (block_a);
		}
	}
}

void ledger_processor::state_block_impl (badem::state_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? badem::process_result::old : badem::process_result::progress; // Have we seen this block before? (Unambiguous)
	if (result.code == badem::process_result::progress)
	{
		// Validate block if not verified outside of ledger
		if (result.verified != badem::signature_verification::valid)
		{
			result.code = validate_message (block_a.hashables.account, hash, block_a.signature) ? badem::process_result::bad_signature : badem::process_result::progress; // Is this block signed correctly (Unambiguous)
		}
		if (result.code == badem::process_result::progress)
		{
			assert (!validate_message (block_a.hashables.account, hash, block_a.signature));
			result.verified = badem::signature_verification::valid;
			result.code = block_a.hashables.account.is_zero () ? badem::process_result::opened_burn_account : badem::process_result::progress; // Is this for the burn account? (Unambiguous)
			if (result.code == badem::process_result::progress)
			{
				badem::epoch epoch (badem::epoch::epoch_0);
				badem::account_info info;
				result.amount = block_a.hashables.balance;
				auto is_send (false);
				auto account_error (ledger.store.account_get (transaction, block_a.hashables.account, info));
				if (!account_error)
				{
					epoch = info.epoch ();
					// Account already exists
					result.code = block_a.hashables.previous.is_zero () ? badem::process_result::fork : badem::process_result::progress; // Has this account already been opened? (Ambigious)
					if (result.code == badem::process_result::progress)
					{
						result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? badem::process_result::progress : badem::process_result::gap_previous; // Does the previous block exist in the ledger? (Unambigious)
						if (result.code == badem::process_result::progress)
						{
							is_send = block_a.hashables.balance < info.balance;
							result.amount = is_send ? (info.balance.number () - result.amount.number ()) : (result.amount.number () - info.balance.number ());
							result.code = block_a.hashables.previous == info.head ? badem::process_result::progress : badem::process_result::fork; // Is the previous block the account's head block? (Ambigious)
						}
					}
				}
				else
				{
					// Account does not yet exists
					result.code = block_a.previous ().is_zero () ? badem::process_result::progress : badem::process_result::gap_previous; // Does the first block in an account yield 0 for previous() ? (Unambigious)
					if (result.code == badem::process_result::progress)
					{
						result.code = !block_a.hashables.link.is_zero () ? badem::process_result::progress : badem::process_result::gap_source; // Is the first block receiving from a send ? (Unambigious)
					}
				}
				if (result.code == badem::process_result::progress)
				{
					if (!is_send)
					{
						if (!block_a.hashables.link.is_zero ())
						{
							result.code = ledger.store.source_exists (transaction, block_a.hashables.link) ? badem::process_result::progress : badem::process_result::gap_source; // Have we seen the source block already? (Harmless)
							if (result.code == badem::process_result::progress)
							{
								badem::pending_key key (block_a.hashables.account, block_a.hashables.link);
								badem::pending_info pending;
								result.code = ledger.store.pending_get (transaction, key, pending) ? badem::process_result::unreceivable : badem::process_result::progress; // Has this source already been received (Malformed)
								if (result.code == badem::process_result::progress)
								{
									result.code = result.amount == pending.amount ? badem::process_result::progress : badem::process_result::balance_mismatch;
									epoch = std::max (epoch, pending.epoch);
								}
							}
						}
						else
						{
							// If there's no link, the balance must remain the same, only the representative can change
							result.code = result.amount.is_zero () ? badem::process_result::progress : badem::process_result::balance_mismatch;
						}
					}
				}
				if (result.code == badem::process_result::progress)
				{
					ledger.stats.inc (badem::stat::type::ledger, badem::stat::detail::state_block);
					result.state_is_send = is_send;
					badem::block_sideband sideband (badem::block_type::state, block_a.hashables.account /* unused */, 0, 0 /* unused */, info.block_count + 1, badem::seconds_since_epoch (), epoch);
					ledger.store.block_put (transaction, hash, block_a, sideband);

					if (!info.head.is_zero ())
					{
						// Move existing representation
						ledger.rep_weights.representation_add (info.representative, 0 - info.balance.number ());
					}
					// Add in amount delta
					ledger.rep_weights.representation_add (block_a.representative (), block_a.hashables.balance.number ());

					if (is_send)
					{
						badem::pending_key key (block_a.hashables.link, hash);
						badem::pending_info info (block_a.hashables.account, result.amount.number (), epoch);
						ledger.store.pending_put (transaction, key, info);
					}
					else if (!block_a.hashables.link.is_zero ())
					{
						ledger.store.pending_del (transaction, badem::pending_key (block_a.hashables.account, block_a.hashables.link));
					}

					badem::account_info new_info (hash, block_a.representative (), info.open_block.is_zero () ? hash : info.open_block, block_a.hashables.balance, badem::seconds_since_epoch (), info.block_count + 1, epoch);
					ledger.change_latest (transaction, block_a.hashables.account, info, new_info);
					if (!ledger.store.frontier_get (transaction, info.head).is_zero ())
					{
						ledger.store.frontier_del (transaction, info.head);
					}
					// Frontier table is unnecessary for state blocks and this also prevents old blocks from being inserted on top of state blocks
					result.account = block_a.hashables.account;
				}
			}
		}
	}
}

void ledger_processor::epoch_block_impl (badem::state_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? badem::process_result::old : badem::process_result::progress; // Have we seen this block before? (Unambiguous)
	if (result.code == badem::process_result::progress)
	{
		// Validate block if not verified outside of ledger
		if (result.verified != badem::signature_verification::valid_epoch)
		{
			result.code = validate_message (ledger.epoch_signer (block_a.hashables.link), hash, block_a.signature) ? badem::process_result::bad_signature : badem::process_result::progress; // Is this block signed correctly (Unambiguous)
		}
		if (result.code == badem::process_result::progress)
		{
			assert (!validate_message (ledger.epoch_signer (block_a.hashables.link), hash, block_a.signature));
			result.verified = badem::signature_verification::valid_epoch;
			result.code = block_a.hashables.account.is_zero () ? badem::process_result::opened_burn_account : badem::process_result::progress; // Is this for the burn account? (Unambiguous)
			if (result.code == badem::process_result::progress)
			{
				badem::account_info info;
				auto account_error (ledger.store.account_get (transaction, block_a.hashables.account, info));
				if (!account_error)
				{
					// Account already exists
					result.code = block_a.hashables.previous.is_zero () ? badem::process_result::fork : badem::process_result::progress; // Has this account already been opened? (Ambigious)
					if (result.code == badem::process_result::progress)
					{
						result.code = block_a.hashables.previous == info.head ? badem::process_result::progress : badem::process_result::fork; // Is the previous block the account's head block? (Ambigious)
						if (result.code == badem::process_result::progress)
						{
							result.code = block_a.hashables.representative == info.representative ? badem::process_result::progress : badem::process_result::representative_mismatch;
						}
					}
				}
				else
				{
					result.code = block_a.hashables.representative.is_zero () ? badem::process_result::progress : badem::process_result::representative_mismatch;
				}
				if (result.code == badem::process_result::progress)
				{
					auto epoch = ledger.network_params.ledger.epochs.epoch (block_a.hashables.link);
					// Must be an epoch for an unopened account or the epoch upgrade must be sequential
					auto is_valid_epoch_upgrade = account_error ? static_cast<std::underlying_type_t<badem::epoch>> (epoch) > 0 : badem::epochs::is_sequential (info.epoch (), epoch);
					result.code = is_valid_epoch_upgrade ? badem::process_result::progress : badem::process_result::block_position;
					if (result.code == badem::process_result::progress)
					{
						result.code = block_a.hashables.balance == info.balance ? badem::process_result::progress : badem::process_result::balance_mismatch;
						if (result.code == badem::process_result::progress)
						{
							ledger.stats.inc (badem::stat::type::ledger, badem::stat::detail::epoch_block);
							result.account = block_a.hashables.account;
							result.amount = 0;
							badem::block_sideband sideband (badem::block_type::state, block_a.hashables.account /* unused */, 0, 0 /* unused */, info.block_count + 1, badem::seconds_since_epoch (), epoch);
							ledger.store.block_put (transaction, hash, block_a, sideband);
							badem::account_info new_info (hash, block_a.representative (), info.open_block.is_zero () ? hash : info.open_block, info.balance, badem::seconds_since_epoch (), info.block_count + 1, epoch);
							ledger.change_latest (transaction, block_a.hashables.account, info, new_info);
							if (!ledger.store.frontier_get (transaction, info.head).is_zero ())
							{
								ledger.store.frontier_del (transaction, info.head);
							}
						}
					}
				}
			}
		}
	}
}

void ledger_processor::change_block (badem::change_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? badem::process_result::old : badem::process_result::progress; // Have we seen this block before? (Harmless)
	if (result.code == badem::process_result::progress)
	{
		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		result.code = previous != nullptr ? badem::process_result::progress : badem::process_result::gap_previous; // Have we seen the previous block already? (Harmless)
		if (result.code == badem::process_result::progress)
		{
			result.code = block_a.valid_predecessor (*previous) ? badem::process_result::progress : badem::process_result::block_position;
			if (result.code == badem::process_result::progress)
			{
				auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
				result.code = account.is_zero () ? badem::process_result::fork : badem::process_result::progress;
				if (result.code == badem::process_result::progress)
				{
					badem::account_info info;
					auto latest_error (ledger.store.account_get (transaction, account, info));
					(void)latest_error;
					assert (!latest_error);
					assert (info.head == block_a.hashables.previous);
					// Validate block if not verified outside of ledger
					if (result.verified != badem::signature_verification::valid)
					{
						result.code = validate_message (account, hash, block_a.signature) ? badem::process_result::bad_signature : badem::process_result::progress; // Is this block signed correctly (Malformed)
					}
					if (result.code == badem::process_result::progress)
					{
						assert (!validate_message (account, hash, block_a.signature));
						result.verified = badem::signature_verification::valid;
						badem::block_sideband sideband (badem::block_type::change, account, 0, info.balance, info.block_count + 1, badem::seconds_since_epoch (), badem::epoch::epoch_0);
						ledger.store.block_put (transaction, hash, block_a, sideband);
						auto balance (ledger.balance (transaction, block_a.hashables.previous));
						ledger.rep_weights.representation_add (block_a.representative (), balance);
						ledger.rep_weights.representation_add (info.representative, 0 - balance);
						badem::account_info new_info (hash, block_a.representative (), info.open_block, info.balance, badem::seconds_since_epoch (), info.block_count + 1, badem::epoch::epoch_0);
						ledger.change_latest (transaction, account, info, new_info);
						ledger.store.frontier_del (transaction, block_a.hashables.previous);
						ledger.store.frontier_put (transaction, hash, account);
						result.account = account;
						result.amount = 0;
						ledger.stats.inc (badem::stat::type::ledger, badem::stat::detail::change);
					}
				}
			}
		}
	}
}

void ledger_processor::send_block (badem::send_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? badem::process_result::old : badem::process_result::progress; // Have we seen this block before? (Harmless)
	if (result.code == badem::process_result::progress)
	{
		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		result.code = previous != nullptr ? badem::process_result::progress : badem::process_result::gap_previous; // Have we seen the previous block already? (Harmless)
		if (result.code == badem::process_result::progress)
		{
			result.code = block_a.valid_predecessor (*previous) ? badem::process_result::progress : badem::process_result::block_position;
			if (result.code == badem::process_result::progress)
			{
				auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
				result.code = account.is_zero () ? badem::process_result::fork : badem::process_result::progress;
				if (result.code == badem::process_result::progress)
				{
					// Validate block if not verified outside of ledger
					if (result.verified != badem::signature_verification::valid)
					{
						result.code = validate_message (account, hash, block_a.signature) ? badem::process_result::bad_signature : badem::process_result::progress; // Is this block signed correctly (Malformed)
					}
					if (result.code == badem::process_result::progress)
					{
						assert (!validate_message (account, hash, block_a.signature));
						result.verified = badem::signature_verification::valid;
						badem::account_info info;
						auto latest_error (ledger.store.account_get (transaction, account, info));
						(void)latest_error;
						assert (!latest_error);
						assert (info.head == block_a.hashables.previous);
						result.code = info.balance.number () >= block_a.hashables.balance.number () ? badem::process_result::progress : badem::process_result::negative_spend; // Is this trying to spend a negative amount (Malicious)
						if (result.code == badem::process_result::progress)
						{
							auto amount (info.balance.number () - block_a.hashables.balance.number ());
							ledger.rep_weights.representation_add (info.representative, 0 - amount);
							badem::block_sideband sideband (badem::block_type::send, account, 0, block_a.hashables.balance /* unused */, info.block_count + 1, badem::seconds_since_epoch (), badem::epoch::epoch_0);
							ledger.store.block_put (transaction, hash, block_a, sideband);
							badem::account_info new_info (hash, info.representative, info.open_block, block_a.hashables.balance, badem::seconds_since_epoch (), info.block_count + 1, badem::epoch::epoch_0);
							ledger.change_latest (transaction, account, info, new_info);
							ledger.store.pending_put (transaction, badem::pending_key (block_a.hashables.destination, hash), { account, amount, badem::epoch::epoch_0 });
							ledger.store.frontier_del (transaction, block_a.hashables.previous);
							ledger.store.frontier_put (transaction, hash, account);
							result.account = account;
							result.amount = amount;
							result.pending_account = block_a.hashables.destination;
							ledger.stats.inc (badem::stat::type::ledger, badem::stat::detail::send);
						}
					}
				}
			}
		}
	}
}

void ledger_processor::receive_block (badem::receive_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? badem::process_result::old : badem::process_result::progress; // Have we seen this block already?  (Harmless)
	if (result.code == badem::process_result::progress)
	{
		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		result.code = previous != nullptr ? badem::process_result::progress : badem::process_result::gap_previous;
		if (result.code == badem::process_result::progress)
		{
			result.code = block_a.valid_predecessor (*previous) ? badem::process_result::progress : badem::process_result::block_position;
			if (result.code == badem::process_result::progress)
			{
				auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
				result.code = account.is_zero () ? badem::process_result::gap_previous : badem::process_result::progress; //Have we seen the previous block? No entries for account at all (Harmless)
				if (result.code == badem::process_result::progress)
				{
					// Validate block if not verified outside of ledger
					if (result.verified != badem::signature_verification::valid)
					{
						result.code = validate_message (account, hash, block_a.signature) ? badem::process_result::bad_signature : badem::process_result::progress; // Is the signature valid (Malformed)
					}
					if (result.code == badem::process_result::progress)
					{
						assert (!validate_message (account, hash, block_a.signature));
						result.verified = badem::signature_verification::valid;
						result.code = ledger.store.source_exists (transaction, block_a.hashables.source) ? badem::process_result::progress : badem::process_result::gap_source; // Have we seen the source block already? (Harmless)
						if (result.code == badem::process_result::progress)
						{
							badem::account_info info;
							ledger.store.account_get (transaction, account, info);
							result.code = info.head == block_a.hashables.previous ? badem::process_result::progress : badem::process_result::gap_previous; // Block doesn't immediately follow latest block (Harmless)
							if (result.code == badem::process_result::progress)
							{
								badem::pending_key key (account, block_a.hashables.source);
								badem::pending_info pending;
								result.code = ledger.store.pending_get (transaction, key, pending) ? badem::process_result::unreceivable : badem::process_result::progress; // Has this source already been received (Malformed)
								if (result.code == badem::process_result::progress)
								{
									result.code = pending.epoch == badem::epoch::epoch_0 ? badem::process_result::progress : badem::process_result::unreceivable; // Are we receiving a state-only send? (Malformed)
									if (result.code == badem::process_result::progress)
									{
										auto new_balance (info.balance.number () + pending.amount.number ());
										badem::account_info source_info;
										auto error (ledger.store.account_get (transaction, pending.source, source_info));
										(void)error;
										assert (!error);
										ledger.store.pending_del (transaction, key);
										badem::block_sideband sideband (badem::block_type::receive, account, 0, new_balance, info.block_count + 1, badem::seconds_since_epoch (), badem::epoch::epoch_0);
										ledger.store.block_put (transaction, hash, block_a, sideband);
										badem::account_info new_info (hash, info.representative, info.open_block, new_balance, badem::seconds_since_epoch (), info.block_count + 1, badem::epoch::epoch_0);
										ledger.change_latest (transaction, account, info, new_info);
										ledger.rep_weights.representation_add (info.representative, pending.amount.number ());
										ledger.store.frontier_del (transaction, block_a.hashables.previous);
										ledger.store.frontier_put (transaction, hash, account);
										result.account = account;
										result.amount = pending.amount;
										ledger.stats.inc (badem::stat::type::ledger, badem::stat::detail::receive);
									}
								}
							}
						}
					}
				}
				else
				{
					result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? badem::process_result::fork : badem::process_result::gap_previous; // If we have the block but it's not the latest we have a signed fork (Malicious)
				}
			}
		}
	}
}

void ledger_processor::open_block (badem::open_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? badem::process_result::old : badem::process_result::progress; // Have we seen this block already? (Harmless)
	if (result.code == badem::process_result::progress)
	{
		// Validate block if not verified outside of ledger
		if (result.verified != badem::signature_verification::valid)
		{
			result.code = validate_message (block_a.hashables.account, hash, block_a.signature) ? badem::process_result::bad_signature : badem::process_result::progress; // Is the signature valid (Malformed)
		}
		if (result.code == badem::process_result::progress)
		{
			assert (!validate_message (block_a.hashables.account, hash, block_a.signature));
			result.verified = badem::signature_verification::valid;
			result.code = ledger.store.source_exists (transaction, block_a.hashables.source) ? badem::process_result::progress : badem::process_result::gap_source; // Have we seen the source block? (Harmless)
			if (result.code == badem::process_result::progress)
			{
				badem::account_info info;
				result.code = ledger.store.account_get (transaction, block_a.hashables.account, info) ? badem::process_result::progress : badem::process_result::fork; // Has this account already been opened? (Malicious)
				if (result.code == badem::process_result::progress)
				{
					badem::pending_key key (block_a.hashables.account, block_a.hashables.source);
					badem::pending_info pending;
					result.code = ledger.store.pending_get (transaction, key, pending) ? badem::process_result::unreceivable : badem::process_result::progress; // Has this source already been received (Malformed)
					if (result.code == badem::process_result::progress)
					{
						result.code = block_a.hashables.account == ledger.network_params.ledger.burn_account ? badem::process_result::opened_burn_account : badem::process_result::progress; // Is it burning 0 account? (Malicious)
						if (result.code == badem::process_result::progress)
						{
							result.code = pending.epoch == badem::epoch::epoch_0 ? badem::process_result::progress : badem::process_result::unreceivable; // Are we receiving a state-only send? (Malformed)
							if (result.code == badem::process_result::progress)
							{
								badem::account_info source_info;
								auto error (ledger.store.account_get (transaction, pending.source, source_info));
								(void)error;
								assert (!error);
								ledger.store.pending_del (transaction, key);
								badem::block_sideband sideband (badem::block_type::open, block_a.hashables.account, 0, pending.amount, 1, badem::seconds_since_epoch (), badem::epoch::epoch_0);
								ledger.store.block_put (transaction, hash, block_a, sideband);
								badem::account_info new_info (hash, block_a.representative (), hash, pending.amount.number (), badem::seconds_since_epoch (), 1, badem::epoch::epoch_0);
								ledger.change_latest (transaction, block_a.hashables.account, info, new_info);
								ledger.rep_weights.representation_add (block_a.representative (), pending.amount.number ());
								ledger.store.frontier_put (transaction, hash, block_a.hashables.account);
								result.account = block_a.hashables.account;
								result.amount = pending.amount;
								ledger.stats.inc (badem::stat::type::ledger, badem::stat::detail::open);
							}
						}
					}
				}
			}
		}
	}
}

ledger_processor::ledger_processor (badem::ledger & ledger_a, badem::write_transaction const & transaction_a, badem::signature_verification verification_a) :
ledger (ledger_a),
transaction (transaction_a),
verification (verification_a)
{
	result.verified = verification;
}
} // namespace

badem::ledger::ledger (badem::block_store & store_a, badem::stat & stat_a, bool cache_reps_a, bool cache_cemented_count_a) :
store (store_a),
stats (stat_a),
check_bootstrap_weights (true)
{
	if (!store.init_error ())
	{
		auto transaction = store.tx_begin_read ();
		if (cache_reps_a)
		{
			for (auto i (store.latest_begin (transaction)), n (store.latest_end ()); i != n; ++i)
			{
				badem::account_info const & info (i->second);
				rep_weights.representation_add (info.representative, info.balance.number ());
			}
		}

		if (cache_cemented_count_a)
		{
			for (auto i (store.confirmation_height_begin (transaction)), n (store.confirmation_height_end ()); i != n; ++i)
			{
				cemented_count += i->second;
			}
		}

		// Cache block count
		block_count_cache = store.block_count (transaction).sum ();
	}
}

// Balance for account containing hash
badem::uint128_t badem::ledger::balance (badem::transaction const & transaction_a, badem::block_hash const & hash_a) const
{
	return hash_a.is_zero () ? 0 : store.block_balance (transaction_a, hash_a);
}

// Balance for an account by account number
badem::uint128_t badem::ledger::account_balance (badem::transaction const & transaction_a, badem::account const & account_a)
{
	badem::uint128_t result (0);
	badem::account_info info;
	auto none (store.account_get (transaction_a, account_a, info));
	if (!none)
	{
		result = info.balance.number ();
	}
	return result;
}

badem::uint128_t badem::ledger::account_pending (badem::transaction const & transaction_a, badem::account const & account_a)
{
	badem::uint128_t result (0);
	badem::account end (account_a.number () + 1);
	for (auto i (store.pending_begin (transaction_a, badem::pending_key (account_a, 0))), n (store.pending_begin (transaction_a, badem::pending_key (end, 0))); i != n; ++i)
	{
		badem::pending_info const & info (i->second);
		result += info.amount.number ();
	}
	return result;
}

badem::process_return badem::ledger::process (badem::write_transaction const & transaction_a, badem::block const & block_a, badem::signature_verification verification)
{
	assert (!badem::work_validate (block_a));
	ledger_processor processor (*this, transaction_a, verification);
	block_a.visit (processor);
	if (processor.result.code == badem::process_result::progress)
	{
		++block_count_cache;
	}
	return processor.result;
}

badem::block_hash badem::ledger::representative (badem::transaction const & transaction_a, badem::block_hash const & hash_a)
{
	auto result (representative_calculated (transaction_a, hash_a));
	assert (result.is_zero () || store.block_exists (transaction_a, result));
	return result;
}

badem::block_hash badem::ledger::representative_calculated (badem::transaction const & transaction_a, badem::block_hash const & hash_a)
{
	representative_visitor visitor (transaction_a, store);
	visitor.compute (hash_a);
	return visitor.result;
}

bool badem::ledger::block_exists (badem::block_hash const & hash_a)
{
	auto transaction (store.tx_begin_read ());
	auto result (store.block_exists (transaction, hash_a));
	return result;
}

bool badem::ledger::block_exists (badem::block_type type, badem::block_hash const & hash_a)
{
	auto transaction (store.tx_begin_read ());
	auto result (store.block_exists (transaction, type, hash_a));
	return result;
}

std::string badem::ledger::block_text (char const * hash_a)
{
	return block_text (badem::block_hash (hash_a));
}

std::string badem::ledger::block_text (badem::block_hash const & hash_a)
{
	std::string result;
	auto transaction (store.tx_begin_read ());
	auto block (store.block_get (transaction, hash_a));
	if (block != nullptr)
	{
		block->serialize_json (result);
	}
	return result;
}

bool badem::ledger::is_send (badem::transaction const & transaction_a, badem::state_block const & block_a) const
{
	bool result (false);
	badem::block_hash previous (block_a.hashables.previous);
	if (!previous.is_zero ())
	{
		if (block_a.hashables.balance < balance (transaction_a, previous))
		{
			result = true;
		}
	}
	return result;
}

badem::account const & badem::ledger::block_destination (badem::transaction const & transaction_a, badem::block const & block_a)
{
	badem::send_block const * send_block (dynamic_cast<badem::send_block const *> (&block_a));
	badem::state_block const * state_block (dynamic_cast<badem::state_block const *> (&block_a));
	if (send_block != nullptr)
	{
		return send_block->hashables.destination;
	}
	else if (state_block != nullptr && is_send (transaction_a, *state_block))
	{
		return state_block->hashables.link;
	}
	static badem::account result (0);
	return result;
}

badem::block_hash badem::ledger::block_source (badem::transaction const & transaction_a, badem::block const & block_a)
{
	/*
	 * block_source() requires that the previous block of the block
	 * passed in exist in the database.  This is because it will try
	 * to check account balances to determine if it is a send block.
	 */
	assert (block_a.previous ().is_zero () || store.block_exists (transaction_a, block_a.previous ()));

	// If block_a.source () is nonzero, then we have our source.
	// However, universal blocks will always return zero.
	badem::block_hash result (block_a.source ());
	badem::state_block const * state_block (dynamic_cast<badem::state_block const *> (&block_a));
	if (state_block != nullptr && !is_send (transaction_a, *state_block))
	{
		result = state_block->hashables.link;
	}
	return result;
}

// Vote weight of an account
badem::uint128_t badem::ledger::weight (badem::account const & account_a)
{
	if (check_bootstrap_weights.load ())
	{
		if (block_count_cache < bootstrap_weight_max_blocks)
		{
			auto weight = bootstrap_weights.find (account_a);
			if (weight != bootstrap_weights.end ())
			{
				return weight->second;
			}
		}
		else
		{
			check_bootstrap_weights = false;
		}
	}
	return rep_weights.representation_get (account_a);
}

// Rollback blocks until `block_a' doesn't exist or it tries to penetrate the confirmation height
bool badem::ledger::rollback (badem::write_transaction const & transaction_a, badem::block_hash const & block_a, std::vector<std::shared_ptr<badem::block>> & list_a)
{
	assert (store.block_exists (transaction_a, block_a));
	auto account_l (account (transaction_a, block_a));
	auto block_account_height (store.block_account_height (transaction_a, block_a));
	rollback_visitor rollback (transaction_a, *this, list_a);
	badem::account_info account_info;
	auto error (false);
	while (!error && store.block_exists (transaction_a, block_a))
	{
		uint64_t confirmation_height;
		auto latest_error = store.confirmation_height_get (transaction_a, account_l, confirmation_height);
		assert (!latest_error);
		(void)latest_error;
		if (block_account_height > confirmation_height)
		{
			latest_error = store.account_get (transaction_a, account_l, account_info);
			assert (!latest_error);
			auto block (store.block_get (transaction_a, account_info.head));
			list_a.push_back (block);
			block->visit (rollback);
			error = rollback.error;
			if (!error)
			{
				--block_count_cache;
			}
		}
		else
		{
			error = true;
		}
	}
	return error;
}

bool badem::ledger::rollback (badem::write_transaction const & transaction_a, badem::block_hash const & block_a)
{
	std::vector<std::shared_ptr<badem::block>> rollback_list;
	return rollback (transaction_a, block_a, rollback_list);
}

// Return account containing hash
badem::account badem::ledger::account (badem::transaction const & transaction_a, badem::block_hash const & hash_a) const
{
	return store.block_account (transaction_a, hash_a);
}

// Return amount decrease or increase for block
badem::uint128_t badem::ledger::amount (badem::transaction const & transaction_a, badem::account const & account_a)
{
	release_assert (account_a == network_params.ledger.genesis_account);
	return network_params.ledger.genesis_amount;
}

badem::uint128_t badem::ledger::amount (badem::transaction const & transaction_a, badem::block_hash const & hash_a)
{
	auto block (store.block_get (transaction_a, hash_a));
	auto block_balance (balance (transaction_a, hash_a));
	auto previous_balance (balance (transaction_a, block->previous ()));
	return block_balance > previous_balance ? block_balance - previous_balance : previous_balance - block_balance;
}

// Return latest block for account
badem::block_hash badem::ledger::latest (badem::transaction const & transaction_a, badem::account const & account_a)
{
	badem::account_info info;
	auto latest_error (store.account_get (transaction_a, account_a, info));
	return latest_error ? 0 : info.head;
}

// Return latest root for account, account number if there are no blocks for this account.
badem::root badem::ledger::latest_root (badem::transaction const & transaction_a, badem::account const & account_a)
{
	badem::account_info info;
	if (store.account_get (transaction_a, account_a, info))
	{
		return account_a;
	}
	else
	{
		return info.head;
	}
}

void badem::ledger::dump_account_chain (badem::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	auto hash (latest (transaction, account_a));
	while (!hash.is_zero ())
	{
		auto block (store.block_get (transaction, hash));
		assert (block != nullptr);
		std::cerr << hash.to_string () << std::endl;
		hash = block->previous ();
	}
}

class block_fit_visitor : public badem::block_visitor
{
public:
	block_fit_visitor (badem::ledger & ledger_a, badem::transaction const & transaction_a) :
	ledger (ledger_a),
	transaction (transaction_a),
	result (false)
	{
	}
	void send_block (badem::send_block const & block_a) override
	{
		result = ledger.store.block_exists (transaction, block_a.previous ());
	}
	void receive_block (badem::receive_block const & block_a) override
	{
		result = ledger.store.block_exists (transaction, block_a.previous ());
		result &= ledger.store.block_exists (transaction, block_a.source ());
	}
	void open_block (badem::open_block const & block_a) override
	{
		result = ledger.store.block_exists (transaction, block_a.source ());
	}
	void change_block (badem::change_block const & block_a) override
	{
		result = ledger.store.block_exists (transaction, block_a.previous ());
	}
	void state_block (badem::state_block const & block_a) override
	{
		result = block_a.previous ().is_zero () || ledger.store.block_exists (transaction, block_a.previous ());
		if (result && !ledger.is_send (transaction, block_a))
		{
			result &= ledger.store.block_exists (transaction, block_a.hashables.link) || block_a.hashables.link.is_zero () || ledger.is_epoch_link (block_a.hashables.link);
		}
	}
	badem::ledger & ledger;
	badem::transaction const & transaction;
	bool result;
};

bool badem::ledger::could_fit (badem::transaction const & transaction_a, badem::block const & block_a)
{
	block_fit_visitor visitor (*this, transaction_a);
	block_a.visit (visitor);
	return visitor.result;
}

bool badem::ledger::is_epoch_link (badem::link const & link_a)
{
	return network_params.ledger.epochs.is_epoch_link (link_a);
}

badem::account const & badem::ledger::epoch_signer (badem::link const & link_a) const
{
	return network_params.ledger.epochs.signer (network_params.ledger.epochs.epoch (link_a));
}

badem::link const & badem::ledger::epoch_link (badem::epoch epoch_a) const
{
	return network_params.ledger.epochs.link (epoch_a);
}

void badem::ledger::change_latest (badem::write_transaction const & transaction_a, badem::account const & account_a, badem::account_info const & old_a, badem::account_info const & new_a)
{
	if (!new_a.head.is_zero ())
	{
		if (old_a.head.is_zero () && new_a.open_block == new_a.head)
		{
			assert (!store.confirmation_height_exists (transaction_a, account_a));
			store.confirmation_height_put (transaction_a, account_a, 0);
		}
		if (!old_a.head.is_zero () && old_a.epoch () != new_a.epoch ())
		{
			// store.account_put won't erase existing entries if they're in different tables
			store.account_del (transaction_a, account_a);
		}
		store.account_put (transaction_a, account_a, new_a);
	}
	else
	{
		store.confirmation_height_del (transaction_a, account_a);
		store.account_del (transaction_a, account_a);
	}
}

std::shared_ptr<badem::block> badem::ledger::successor (badem::transaction const & transaction_a, badem::qualified_root const & root_a)
{
	badem::block_hash successor (0);
	auto get_from_previous = false;
	if (root_a.previous ().is_zero ())
	{
		badem::account_info info;
		if (!store.account_get (transaction_a, root_a.root (), info))
		{
			successor = info.open_block;
		}
		else
		{
			get_from_previous = true;
		}
	}
	else
	{
		get_from_previous = true;
	}

	if (get_from_previous)
	{
		successor = store.block_successor (transaction_a, root_a.previous ());
	}
	std::shared_ptr<badem::block> result;
	if (!successor.is_zero ())
	{
		result = store.block_get (transaction_a, successor);
	}
	assert (successor.is_zero () || result != nullptr);
	return result;
}

std::shared_ptr<badem::block> badem::ledger::forked_block (badem::transaction const & transaction_a, badem::block const & block_a)
{
	assert (!store.block_exists (transaction_a, block_a.type (), block_a.hash ()));
	auto root (block_a.root ());
	assert (store.block_exists (transaction_a, root) || store.account_exists (transaction_a, root));
	auto result (store.block_get (transaction_a, store.block_successor (transaction_a, root)));
	if (result == nullptr)
	{
		badem::account_info info;
		auto error (store.account_get (transaction_a, root, info));
		(void)error;
		assert (!error);
		result = store.block_get (transaction_a, info.open_block);
		assert (result != nullptr);
	}
	return result;
}

bool badem::ledger::block_confirmed (badem::transaction const & transaction_a, badem::block_hash const & hash_a) const
{
	auto confirmed (false);
	auto block_height (store.block_account_height (transaction_a, hash_a));
	if (block_height > 0) // 0 indicates that the block doesn't exist
	{
		uint64_t confirmation_height;
		release_assert (!store.confirmation_height_get (transaction_a, account (transaction_a, hash_a), confirmation_height));
		confirmed = (confirmation_height >= block_height);
	}
	return confirmed;
}

bool badem::ledger::block_not_confirmed_or_not_exists (badem::block const & block_a) const
{
	bool result (true);
	auto hash (block_a.hash ());
	auto transaction (store.tx_begin_read ());
	if (store.block_exists (transaction, block_a.type (), hash))
	{
		result = !block_confirmed (transaction, hash);
	}
	return result;
}

namespace badem
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (ledger & ledger, const std::string & name)
{
	auto composite = std::make_unique<seq_con_info_composite> (name);
	auto count = ledger.bootstrap_weights_size.load ();
	auto sizeof_element = sizeof (decltype (ledger.bootstrap_weights)::value_type);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "bootstrap_weights", count, sizeof_element }));
	composite->add_component (collect_seq_con_info (ledger.rep_weights, "rep_weights"));
	return composite;
}
}
