#pragma once

#include <badem/lib/rep_weights.hpp>
#include <badem/secure/blockstore.hpp>

namespace badem
{
template <typename Val, typename Derived_Store>
class block_predecessor_set;

/** This base class implements the block_store interface functions which have DB agnostic functionality */
template <typename Val, typename Derived_Store>
class block_store_partial : public block_store
{
public:
	using block_store::block_exists;
	using block_store::unchecked_put;

	friend class badem::block_predecessor_set<Val, Derived_Store>;

	std::mutex cache_mutex;

	/**
	 * If using a different store version than the latest then you may need
	 * to modify some of the objects in the store to be appropriate for the version before an upgrade.
	 */
	void initialize (badem::write_transaction const & transaction_a, badem::genesis const & genesis_a, badem::rep_weights & rep_weights, std::atomic<uint64_t> & cemented_count, std::atomic<uint64_t> & block_count_cache) override
	{
		auto hash_l (genesis_a.hash ());
		assert (latest_begin (transaction_a) == latest_end ());
		badem::block_sideband sideband (badem::block_type::open, network_params.ledger.genesis_account, 0, network_params.ledger.genesis_amount, 1, badem::seconds_since_epoch (), badem::epoch::epoch_0);
		block_put (transaction_a, hash_l, *genesis_a.open, sideband);
		++block_count_cache;
		confirmation_height_put (transaction_a, network_params.ledger.genesis_account, 1);
		++cemented_count;
		account_put (transaction_a, network_params.ledger.genesis_account, { hash_l, network_params.ledger.genesis_account, genesis_a.open->hash (), std::numeric_limits<badem::uint128_t>::max (), badem::seconds_since_epoch (), 1, badem::epoch::epoch_0 });
		rep_weights.representation_put (network_params.ledger.genesis_account, std::numeric_limits<badem::uint128_t>::max ());
		frontier_put (transaction_a, hash_l, network_params.ledger.genesis_account);
	}

	badem::uint128_t block_balance (badem::transaction const & transaction_a, badem::block_hash const & hash_a) override
	{
		badem::block_sideband sideband;
		auto block (block_get (transaction_a, hash_a, &sideband));
		badem::uint128_t result (block_balance_calculated (block, sideband));
		return result;
	}

	bool account_exists (badem::transaction const & transaction_a, badem::account const & account_a) override
	{
		auto iterator (latest_begin (transaction_a, account_a));
		return iterator != latest_end () && badem::account (iterator->first) == account_a;
	}

	void confirmation_height_clear (badem::write_transaction const & transaction_a, badem::account const & account, uint64_t existing_confirmation_height) override
	{
		if (existing_confirmation_height > 0)
		{
			confirmation_height_put (transaction_a, account, 0);
		}
	}

	void confirmation_height_clear (badem::write_transaction const & transaction_a) override
	{
		for (auto i (confirmation_height_begin (transaction_a)), n (confirmation_height_end ()); i != n; ++i)
		{
			confirmation_height_clear (transaction_a, i->first, i->second);
		}
	}

	bool pending_exists (badem::transaction const & transaction_a, badem::pending_key const & key_a) override
	{
		auto iterator (pending_begin (transaction_a, key_a));
		return iterator != pending_end () && badem::pending_key (iterator->first) == key_a;
	}

	std::vector<badem::unchecked_info> unchecked_get (badem::transaction const & transaction_a, badem::block_hash const & hash_a) override
	{
		std::vector<badem::unchecked_info> result;
		for (auto i (unchecked_begin (transaction_a, badem::unchecked_key (hash_a, 0))), n (unchecked_end ()); i != n && i->first.key () == hash_a; ++i)
		{
			badem::unchecked_info const & unchecked_info (i->second);
			result.push_back (unchecked_info);
		}
		return result;
	}

	void block_put (badem::write_transaction const & transaction_a, badem::block_hash const & hash_a, badem::block const & block_a, badem::block_sideband const & sideband_a) override
	{
		assert (block_a.type () == sideband_a.type);
		assert (sideband_a.successor.is_zero () || block_exists (transaction_a, sideband_a.successor));
		std::vector<uint8_t> vector;
		{
			badem::vectorstream stream (vector);
			block_a.serialize (stream);
			sideband_a.serialize (stream);
		}
		block_raw_put (transaction_a, vector, block_a.type (), hash_a);
		badem::block_predecessor_set<Val, Derived_Store> predecessor (transaction_a, *this);
		block_a.visit (predecessor);
		assert (block_a.previous ().is_zero () || block_successor (transaction_a, block_a.previous ()) == hash_a);
	}

	// Converts a block hash to a block height
	uint64_t block_account_height (badem::transaction const & transaction_a, badem::block_hash const & hash_a) const override
	{
		badem::block_sideband sideband;
		auto block = block_get (transaction_a, hash_a, &sideband);
		assert (block != nullptr);
		return sideband.height;
	}

	std::shared_ptr<badem::block> block_get (badem::transaction const & transaction_a, badem::block_hash const & hash_a, badem::block_sideband * sideband_a = nullptr) const override
	{
		badem::block_type type;
		auto value (block_raw_get (transaction_a, hash_a, type));
		std::shared_ptr<badem::block> result;
		if (value.size () != 0)
		{
			badem::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			result = badem::deserialize_block (stream, type);
			assert (result != nullptr);
			if (sideband_a)
			{
				sideband_a->type = type;
				if (full_sideband (transaction_a) || entry_has_sideband (value.size (), type))
				{
					auto error (sideband_a->deserialize (stream));
					(void)error;
					assert (!error);
				}
				else
				{
					// Reconstruct sideband data for block.
					sideband_a->account = block_account_computed (transaction_a, hash_a);
					sideband_a->balance = block_balance_computed (transaction_a, hash_a);
					sideband_a->successor = block_successor (transaction_a, hash_a);
					sideband_a->height = 0;
					sideband_a->timestamp = 0;
				}
			}
		}
		return result;
	}

	bool block_exists (badem::transaction const & transaction_a, badem::block_type type, badem::block_hash const & hash_a) override
	{
		auto junk = block_raw_get_by_type (transaction_a, hash_a, type);
		return junk.is_initialized ();
	}

	bool block_exists (badem::transaction const & tx_a, badem::block_hash const & hash_a) override
	{
		// Table lookups are ordered by match probability
		// clang-format off
		return
			block_exists (tx_a, badem::block_type::state, hash_a) ||
			block_exists (tx_a, badem::block_type::send, hash_a) ||
			block_exists (tx_a, badem::block_type::receive, hash_a) ||
			block_exists (tx_a, badem::block_type::open, hash_a) ||
			block_exists (tx_a, badem::block_type::change, hash_a);
		// clang-format on
	}

	bool root_exists (badem::transaction const & transaction_a, badem::root const & root_a) override
	{
		return block_exists (transaction_a, root_a) || account_exists (transaction_a, root_a);
	}

	bool source_exists (badem::transaction const & transaction_a, badem::block_hash const & source_a) override
	{
		return block_exists (transaction_a, badem::block_type::state, source_a) || block_exists (transaction_a, badem::block_type::send, source_a);
	}

	badem::account block_account (badem::transaction const & transaction_a, badem::block_hash const & hash_a) const override
	{
		badem::block_sideband sideband;
		auto block (block_get (transaction_a, hash_a, &sideband));
		badem::account result (block->account ());
		if (result.is_zero ())
		{
			result = sideband.account;
		}
		assert (!result.is_zero ());
		return result;
	}

	badem::uint128_t block_balance_calculated (std::shared_ptr<badem::block> block_a, badem::block_sideband const & sideband_a) const override
	{
		badem::uint128_t result;
		switch (block_a->type ())
		{
			case badem::block_type::open:
			case badem::block_type::receive:
			case badem::block_type::change:
				result = sideband_a.balance.number ();
				break;
			case badem::block_type::send:
				result = boost::polymorphic_downcast<badem::send_block *> (block_a.get ())->hashables.balance.number ();
				break;
			case badem::block_type::state:
				result = boost::polymorphic_downcast<badem::state_block *> (block_a.get ())->hashables.balance.number ();
				break;
			case badem::block_type::invalid:
			case badem::block_type::not_a_block:
				release_assert (false);
				break;
		}
		return result;
	}

	badem::block_hash block_successor (badem::transaction const & transaction_a, badem::block_hash const & hash_a) const override
	{
		badem::block_type type;
		auto value (block_raw_get (transaction_a, hash_a, type));
		badem::block_hash result;
		if (value.size () != 0)
		{
			assert (value.size () >= result.bytes.size ());
			badem::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()) + block_successor_offset (transaction_a, value.size (), type), result.bytes.size ());
			auto error (badem::try_read (stream, result.bytes));
			(void)error;
			assert (!error);
		}
		else
		{
			result.clear ();
		}
		return result;
	}

	bool full_sideband (badem::transaction const & transaction_a) const
	{
		return version_get (transaction_a) > 12;
	}

	void block_successor_clear (badem::write_transaction const & transaction_a, badem::block_hash const & hash_a) override
	{
		badem::block_type type;
		auto value (block_raw_get (transaction_a, hash_a, type));
		assert (value.size () != 0);
		std::vector<uint8_t> data (static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size ());
		std::fill_n (data.begin () + block_successor_offset (transaction_a, value.size (), type), sizeof (badem::block_hash), uint8_t{ 0 });
		block_raw_put (transaction_a, data, type, hash_a);
	}

	void unchecked_put (badem::write_transaction const & transaction_a, badem::block_hash const & hash_a, std::shared_ptr<badem::block> const & block_a) override
	{
		badem::unchecked_key key (hash_a, block_a->hash ());
		badem::unchecked_info info (block_a, block_a->account (), badem::seconds_since_epoch (), badem::signature_verification::unknown);
		unchecked_put (transaction_a, key, info);
	}

	std::shared_ptr<badem::vote> vote_current (badem::transaction const & transaction_a, badem::account const & account_a) override
	{
		assert (!cache_mutex.try_lock ());
		std::shared_ptr<badem::vote> result;
		auto existing (vote_cache_l1.find (account_a));
		auto have_existing (true);
		if (existing == vote_cache_l1.end ())
		{
			existing = vote_cache_l2.find (account_a);
			if (existing == vote_cache_l2.end ())
			{
				have_existing = false;
			}
		}
		if (have_existing)
		{
			result = existing->second;
		}
		else
		{
			result = vote_get (transaction_a, account_a);
		}
		return result;
	}

	std::shared_ptr<badem::vote> vote_generate (badem::transaction const & transaction_a, badem::account const & account_a, badem::raw_key const & key_a, std::shared_ptr<badem::block> block_a) override
	{
		badem::lock_guard<std::mutex> lock (cache_mutex);
		auto result (vote_current (transaction_a, account_a));
		uint64_t sequence ((result ? result->sequence : 0) + 1);
		result = std::make_shared<badem::vote> (account_a, key_a, sequence, block_a);
		vote_cache_l1[account_a] = result;
		return result;
	}

	std::shared_ptr<badem::vote> vote_generate (badem::transaction const & transaction_a, badem::account const & account_a, badem::raw_key const & key_a, std::vector<badem::block_hash> blocks_a) override
	{
		badem::lock_guard<std::mutex> lock (cache_mutex);
		auto result (vote_current (transaction_a, account_a));
		uint64_t sequence ((result ? result->sequence : 0) + 1);
		result = std::make_shared<badem::vote> (account_a, key_a, sequence, blocks_a);
		vote_cache_l1[account_a] = result;
		return result;
	}

	std::shared_ptr<badem::vote> vote_max (badem::transaction const & transaction_a, std::shared_ptr<badem::vote> vote_a) override
	{
		badem::lock_guard<std::mutex> lock (cache_mutex);
		auto current (vote_current (transaction_a, vote_a->account));
		auto result (vote_a);
		if (current != nullptr && current->sequence > result->sequence)
		{
			result = current;
		}
		vote_cache_l1[vote_a->account] = result;
		return result;
	}

	badem::store_iterator<badem::unchecked_key, badem::unchecked_info> unchecked_end () override
	{
		return badem::store_iterator<badem::unchecked_key, badem::unchecked_info> (nullptr);
	}

	badem::store_iterator<badem::account, std::shared_ptr<badem::vote>> vote_end () override
	{
		return badem::store_iterator<badem::account, std::shared_ptr<badem::vote>> (nullptr);
	}

	badem::store_iterator<badem::endpoint_key, badem::no_value> peers_end () const override
	{
		return badem::store_iterator<badem::endpoint_key, badem::no_value> (nullptr);
	}

	badem::store_iterator<badem::pending_key, badem::pending_info> pending_end () override
	{
		return badem::store_iterator<badem::pending_key, badem::pending_info> (nullptr);
	}

	badem::store_iterator<uint64_t, badem::amount> online_weight_end () const override
	{
		return badem::store_iterator<uint64_t, badem::amount> (nullptr);
	}

	badem::store_iterator<badem::account, badem::account_info> latest_end () override
	{
		return badem::store_iterator<badem::account, badem::account_info> (nullptr);
	}

	badem::store_iterator<badem::account, uint64_t> confirmation_height_end () override
	{
		return badem::store_iterator<badem::account, uint64_t> (nullptr);
	}

	std::mutex & get_cache_mutex () override
	{
		return cache_mutex;
	}

	void block_del (badem::write_transaction const & transaction_a, badem::block_hash const & hash_a) override
	{
		auto status = del (transaction_a, tables::state_blocks, hash_a);
		release_assert (success (status) || not_found (status));
		if (!success (status))
		{
			auto status = del (transaction_a, tables::send_blocks, hash_a);
			release_assert (success (status) || not_found (status));
			if (!success (status))
			{
				auto status = del (transaction_a, tables::receive_blocks, hash_a);
				release_assert (success (status) || not_found (status));
				if (!success (status))
				{
					auto status = del (transaction_a, tables::open_blocks, hash_a);
					release_assert (success (status) || not_found (status));
					if (!success (status))
					{
						auto status = del (transaction_a, tables::change_blocks, hash_a);
						release_assert (success (status));
					}
				}
			}
		}
	}

	int version_get (badem::transaction const & transaction_a) const override
	{
		badem::uint256_union version_key (1);
		badem::db_val<Val> data;
		auto status = get (transaction_a, tables::meta, badem::db_val<Val> (version_key), data);
		int result (1);
		if (!not_found (status))
		{
			badem::uint256_union version_value (data);
			assert (version_value.qwords[2] == 0 && version_value.qwords[1] == 0 && version_value.qwords[0] == 0);
			result = version_value.number ().convert_to<int> ();
		}
		return result;
	}

	badem::epoch block_version (badem::transaction const & transaction_a, badem::block_hash const & hash_a) override
	{
		badem::db_val<Val> value;
		badem::block_sideband sideband;
		auto block = block_get (transaction_a, hash_a, &sideband);
		if (sideband.type == badem::block_type::state)
		{
			return sideband.epoch;
		}

		return badem::epoch::epoch_0;
	}

	void block_raw_put (badem::write_transaction const & transaction_a, std::vector<uint8_t> const & data, badem::block_type block_type_a, badem::block_hash const & hash_a)
	{
		auto database_a = block_database (block_type_a);
		badem::db_val<Val> value{ data.size (), (void *)data.data () };
		auto status = put (transaction_a, database_a, hash_a, value);
		release_assert (success (status));
	}

	void pending_put (badem::write_transaction const & transaction_a, badem::pending_key const & key_a, badem::pending_info const & pending_info_a) override
	{
		badem::db_val<Val> pending (pending_info_a);
		auto status = put (transaction_a, tables::pending, key_a, pending);
		release_assert (success (status));
	}

	void pending_del (badem::write_transaction const & transaction_a, badem::pending_key const & key_a) override
	{
		auto status1 = del (transaction_a, tables::pending, key_a);
		release_assert (success (status1));
	}

	bool pending_get (badem::transaction const & transaction_a, badem::pending_key const & key_a, badem::pending_info & pending_a) override
	{
		badem::db_val<Val> value;
		badem::db_val<Val> key (key_a);
		auto status1 = get (transaction_a, tables::pending, key, value);
		release_assert (success (status1) || not_found (status1));
		bool result (true);
		if (success (status1))
		{
			badem::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			result = pending_a.deserialize (stream);
		}
		return result;
	}

	void frontier_put (badem::write_transaction const & transaction_a, badem::block_hash const & block_a, badem::account const & account_a) override
	{
		badem::db_val<Val> account (account_a);
		auto status (put (transaction_a, tables::frontiers, block_a, account));
		release_assert (success (status));
	}

	badem::account frontier_get (badem::transaction const & transaction_a, badem::block_hash const & block_a) const override
	{
		badem::db_val<Val> value;
		auto status (get (transaction_a, tables::frontiers, badem::db_val<Val> (block_a), value));
		release_assert (success (status) || not_found (status));
		badem::account result (0);
		if (success (status))
		{
			result = static_cast<badem::account> (value);
		}
		return result;
	}

	void frontier_del (badem::write_transaction const & transaction_a, badem::block_hash const & block_a) override
	{
		auto status (del (transaction_a, tables::frontiers, block_a));
		release_assert (success (status));
	}

	void unchecked_put (badem::write_transaction const & transaction_a, badem::unchecked_key const & key_a, badem::unchecked_info const & info_a) override
	{
		badem::db_val<Val> info (info_a);
		auto status (put (transaction_a, tables::unchecked, key_a, info));
		release_assert (success (status));
	}

	void unchecked_del (badem::write_transaction const & transaction_a, badem::unchecked_key const & key_a) override
	{
		auto status (del (transaction_a, tables::unchecked, key_a));
		release_assert (success (status) || not_found (status));
	}

	std::shared_ptr<badem::vote> vote_get (badem::transaction const & transaction_a, badem::account const & account_a) override
	{
		badem::db_val<Val> value;
		auto status (get (transaction_a, tables::vote, badem::db_val<Val> (account_a), value));
		release_assert (success (status) || not_found (status));
		if (success (status))
		{
			std::shared_ptr<badem::vote> result (value);
			assert (result != nullptr);
			return result;
		}
		return nullptr;
	}

	void flush (badem::write_transaction const & transaction_a) override
	{
		{
			badem::lock_guard<std::mutex> lock (cache_mutex);
			vote_cache_l1.swap (vote_cache_l2);
			vote_cache_l1.clear ();
		}
		for (auto i (vote_cache_l2.begin ()), n (vote_cache_l2.end ()); i != n; ++i)
		{
			std::vector<uint8_t> vector;
			{
				badem::vectorstream stream (vector);
				i->second->serialize (stream);
			}
			badem::db_val<Val> value (vector.size (), vector.data ());
			auto status1 (put (transaction_a, tables::vote, i->first, value));
			release_assert (success (status1));
		}
	}

	void online_weight_put (badem::write_transaction const & transaction_a, uint64_t time_a, badem::amount const & amount_a) override
	{
		badem::db_val<Val> value (amount_a);
		auto status (put (transaction_a, tables::online_weight, time_a, value));
		release_assert (success (status));
	}

	void online_weight_del (badem::write_transaction const & transaction_a, uint64_t time_a) override
	{
		auto status (del (transaction_a, tables::online_weight, time_a));
		release_assert (success (status));
	}

	void account_put (badem::write_transaction const & transaction_a, badem::account const & account_a, badem::account_info const & info_a) override
	{
		// Check we are still in sync with other tables
		assert (confirmation_height_exists (transaction_a, account_a));
		badem::db_val<Val> info (info_a);
		auto status = put (transaction_a, tables::accounts, account_a, info);
		release_assert (success (status));
	}

	void account_del (badem::write_transaction const & transaction_a, badem::account const & account_a) override
	{
		auto status1 = del (transaction_a, tables::accounts, account_a);
		release_assert (success (status1));
	}

	bool account_get (badem::transaction const & transaction_a, badem::account const & account_a, badem::account_info & info_a) override
	{
		badem::db_val<Val> value;
		badem::db_val<Val> account (account_a);
		auto status1 (get (transaction_a, tables::accounts, account, value));
		release_assert (success (status1) || not_found (status1));
		bool result (true);
		if (success (status1))
		{
			badem::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			result = info_a.deserialize (stream);
		}
		return result;
	}

	void unchecked_clear (badem::write_transaction const & transaction_a) override
	{
		auto status = drop (transaction_a, tables::unchecked);
		release_assert (success (status));
	}

	size_t online_weight_count (badem::transaction const & transaction_a) const override
	{
		return count (transaction_a, tables::online_weight);
	}

	void online_weight_clear (badem::write_transaction const & transaction_a) override
	{
		auto status (drop (transaction_a, tables::online_weight));
		release_assert (success (status));
	}

	void peer_put (badem::write_transaction const & transaction_a, badem::endpoint_key const & endpoint_a) override
	{
		badem::db_val<Val> zero (static_cast<uint64_t> (0));
		auto status = put (transaction_a, tables::peers, endpoint_a, zero);
		release_assert (success (status));
	}

	void peer_del (badem::write_transaction const & transaction_a, badem::endpoint_key const & endpoint_a) override
	{
		auto status (del (transaction_a, tables::peers, endpoint_a));
		release_assert (success (status));
	}

	bool peer_exists (badem::transaction const & transaction_a, badem::endpoint_key const & endpoint_a) const override
	{
		return exists (transaction_a, tables::peers, badem::db_val<Val> (endpoint_a));
	}

	size_t peer_count (badem::transaction const & transaction_a) const override
	{
		return count (transaction_a, tables::peers);
	}

	void peer_clear (badem::write_transaction const & transaction_a) override
	{
		auto status = drop (transaction_a, tables::peers);
		release_assert (success (status));
	}

	bool exists (badem::transaction const & transaction_a, tables table_a, badem::db_val<Val> const & key_a) const
	{
		return static_cast<const Derived_Store &> (*this).exists (transaction_a, table_a, key_a);
	}

	badem::block_counts block_count (badem::transaction const & transaction_a) override
	{
		badem::block_counts result;
		result.send = count (transaction_a, tables::send_blocks);
		result.receive = count (transaction_a, tables::receive_blocks);
		result.open = count (transaction_a, tables::open_blocks);
		result.change = count (transaction_a, tables::change_blocks);
		result.state = count (transaction_a, tables::state_blocks);
		return result;
	}

	size_t account_count (badem::transaction const & transaction_a) override
	{
		return count (transaction_a, tables::accounts);
	}

	std::shared_ptr<badem::block> block_random (badem::transaction const & transaction_a) override
	{
		auto count (block_count (transaction_a));
		release_assert (std::numeric_limits<CryptoPP::word32>::max () > count.sum ());
		auto region = static_cast<size_t> (badem::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (count.sum () - 1)));
		std::shared_ptr<badem::block> result;
		auto & derived_store = static_cast<Derived_Store &> (*this);
		if (region < count.send)
		{
			result = derived_store.template block_random<badem::send_block> (transaction_a, tables::send_blocks);
		}
		else
		{
			region -= count.send;
			if (region < count.receive)
			{
				result = derived_store.template block_random<badem::receive_block> (transaction_a, tables::receive_blocks);
			}
			else
			{
				region -= count.receive;
				if (region < count.open)
				{
					result = derived_store.template block_random<badem::open_block> (transaction_a, tables::open_blocks);
				}
				else
				{
					region -= count.open;
					if (region < count.change)
					{
						result = derived_store.template block_random<badem::change_block> (transaction_a, tables::change_blocks);
					}
					else
					{
						result = derived_store.template block_random<badem::state_block> (transaction_a, tables::state_blocks);
					}
				}
			}
		}
		assert (result != nullptr);
		return result;
	}

	uint64_t confirmation_height_count (badem::transaction const & transaction_a) override
	{
		return count (transaction_a, tables::confirmation_height);
	}

	void confirmation_height_put (badem::write_transaction const & transaction_a, badem::account const & account_a, uint64_t confirmation_height_a) override
	{
		badem::db_val<Val> confirmation_height (confirmation_height_a);
		auto status = put (transaction_a, tables::confirmation_height, account_a, confirmation_height);
		release_assert (success (status));
	}

	bool confirmation_height_get (badem::transaction const & transaction_a, badem::account const & account_a, uint64_t & confirmation_height_a) override
	{
		badem::db_val<Val> value;
		auto status = get (transaction_a, tables::confirmation_height, badem::db_val<Val> (account_a), value);
		release_assert (success (status) || not_found (status));
		confirmation_height_a = 0;
		if (success (status))
		{
			confirmation_height_a = static_cast<uint64_t> (value);
		}
		return (!success (status));
	}

	void confirmation_height_del (badem::write_transaction const & transaction_a, badem::account const & account_a) override
	{
		auto status (del (transaction_a, tables::confirmation_height, badem::db_val<Val> (account_a)));
		release_assert (success (status));
	}

	bool confirmation_height_exists (badem::transaction const & transaction_a, badem::account const & account_a) const override
	{
		return exists (transaction_a, tables::confirmation_height, badem::db_val<Val> (account_a));
	}

	badem::store_iterator<badem::account, badem::account_info> latest_begin (badem::transaction const & transaction_a, badem::account const & account_a) override
	{
		return make_iterator<badem::account, badem::account_info> (transaction_a, tables::accounts, badem::db_val<Val> (account_a));
	}

	badem::store_iterator<badem::account, badem::account_info> latest_begin (badem::transaction const & transaction_a) override
	{
		return make_iterator<badem::account, badem::account_info> (transaction_a, tables::accounts);
	}

	badem::store_iterator<badem::pending_key, badem::pending_info> pending_begin (badem::transaction const & transaction_a, badem::pending_key const & key_a) override
	{
		return make_iterator<badem::pending_key, badem::pending_info> (transaction_a, tables::pending, badem::db_val<Val> (key_a));
	}

	badem::store_iterator<badem::pending_key, badem::pending_info> pending_begin (badem::transaction const & transaction_a) override
	{
		return make_iterator<badem::pending_key, badem::pending_info> (transaction_a, tables::pending);
	}

	badem::store_iterator<badem::unchecked_key, badem::unchecked_info> unchecked_begin (badem::transaction const & transaction_a) override
	{
		return make_iterator<badem::unchecked_key, badem::unchecked_info> (transaction_a, tables::unchecked);
	}

	badem::store_iterator<badem::unchecked_key, badem::unchecked_info> unchecked_begin (badem::transaction const & transaction_a, badem::unchecked_key const & key_a) override
	{
		return make_iterator<badem::unchecked_key, badem::unchecked_info> (transaction_a, tables::unchecked, badem::db_val<Val> (key_a));
	}

	badem::store_iterator<badem::account, std::shared_ptr<badem::vote>> vote_begin (badem::transaction const & transaction_a) override
	{
		return make_iterator<badem::account, std::shared_ptr<badem::vote>> (transaction_a, tables::vote);
	}

	badem::store_iterator<uint64_t, badem::amount> online_weight_begin (badem::transaction const & transaction_a) const override
	{
		return make_iterator<uint64_t, badem::amount> (transaction_a, tables::online_weight);
	}

	badem::store_iterator<badem::endpoint_key, badem::no_value> peers_begin (badem::transaction const & transaction_a) const override
	{
		return make_iterator<badem::endpoint_key, badem::no_value> (transaction_a, tables::peers);
	}

	badem::store_iterator<badem::account, uint64_t> confirmation_height_begin (badem::transaction const & transaction_a, badem::account const & account_a) override
	{
		return make_iterator<badem::account, uint64_t> (transaction_a, tables::confirmation_height, badem::db_val<Val> (account_a));
	}

	badem::store_iterator<badem::account, uint64_t> confirmation_height_begin (badem::transaction const & transaction_a) override
	{
		return make_iterator<badem::account, uint64_t> (transaction_a, tables::confirmation_height);
	}

	size_t unchecked_count (badem::transaction const & transaction_a) override
	{
		return count (transaction_a, tables::unchecked);
	}

protected:
	badem::network_params network_params;
	std::unordered_map<badem::account, std::shared_ptr<badem::vote>> vote_cache_l1;
	std::unordered_map<badem::account, std::shared_ptr<badem::vote>> vote_cache_l2;
	static int constexpr version{ 15 };

	template <typename T>
	std::shared_ptr<badem::block> block_random (badem::transaction const & transaction_a, tables table_a)
	{
		badem::block_hash hash;
		badem::random_pool::generate_block (hash.bytes.data (), hash.bytes.size ());
		auto existing = make_iterator<badem::block_hash, std::shared_ptr<T>> (transaction_a, table_a, badem::db_val<Val> (hash));
		if (existing == badem::store_iterator<badem::block_hash, std::shared_ptr<T>> (nullptr))
		{
			existing = make_iterator<badem::block_hash, std::shared_ptr<T>> (transaction_a, table_a);
		}
		auto end (badem::store_iterator<badem::block_hash, std::shared_ptr<T>> (nullptr));
		assert (existing != end);
		return block_get (transaction_a, badem::block_hash (existing->first));
	}

	template <typename Key, typename Value>
	badem::store_iterator<Key, Value> make_iterator (badem::transaction const & transaction_a, tables table_a) const
	{
		return static_cast<Derived_Store const &> (*this).template make_iterator<Key, Value> (transaction_a, table_a);
	}

	template <typename Key, typename Value>
	badem::store_iterator<Key, Value> make_iterator (badem::transaction const & transaction_a, tables table_a, badem::db_val<Val> const & key) const
	{
		return static_cast<Derived_Store const &> (*this).template make_iterator<Key, Value> (transaction_a, table_a, key);
	}

	bool entry_has_sideband (size_t entry_size_a, badem::block_type type_a) const
	{
		return entry_size_a == badem::block::size (type_a) + badem::block_sideband::size (type_a);
	}

	badem::db_val<Val> block_raw_get (badem::transaction const & transaction_a, badem::block_hash const & hash_a, badem::block_type & type_a) const
	{
		badem::db_val<Val> result;
		// Table lookups are ordered by match probability
		badem::block_type block_types[]{ badem::block_type::state, badem::block_type::send, badem::block_type::receive, badem::block_type::open, badem::block_type::change };
		for (auto current_type : block_types)
		{
			auto db_val (block_raw_get_by_type (transaction_a, hash_a, current_type));
			if (db_val.is_initialized ())
			{
				type_a = current_type;
				result = db_val.get ();
				break;
			}
		}

		return result;
	}

	// Return account containing hash
	badem::account block_account_computed (badem::transaction const & transaction_a, badem::block_hash const & hash_a) const
	{
		assert (!full_sideband (transaction_a));
		badem::account result (0);
		auto hash (hash_a);
		while (result.is_zero ())
		{
			auto block (block_get (transaction_a, hash));
			assert (block);
			result = block->account ();
			if (result.is_zero ())
			{
				auto type (badem::block_type::invalid);
				auto value (block_raw_get (transaction_a, block->previous (), type));
				if (entry_has_sideband (value.size (), type))
				{
					result = block_account (transaction_a, block->previous ());
				}
				else
				{
					badem::block_info block_info;
					if (!block_info_get (transaction_a, hash, block_info))
					{
						result = block_info.account;
					}
					else
					{
						result = frontier_get (transaction_a, hash);
						if (result.is_zero ())
						{
							auto successor (block_successor (transaction_a, hash));
							assert (!successor.is_zero ());
							hash = successor;
						}
					}
				}
			}
		}
		assert (!result.is_zero ());
		return result;
	}

	badem::uint128_t block_balance_computed (badem::transaction const & transaction_a, badem::block_hash const & hash_a) const
	{
		assert (!full_sideband (transaction_a));
		summation_visitor visitor (transaction_a, *this);
		return visitor.compute_balance (hash_a);
	}

	size_t block_successor_offset (badem::transaction const & transaction_a, size_t entry_size_a, badem::block_type type_a) const
	{
		size_t result;
		if (full_sideband (transaction_a) || entry_has_sideband (entry_size_a, type_a))
		{
			result = entry_size_a - badem::block_sideband::size (type_a);
		}
		else
		{
			// Read old successor-only sideband
			assert (entry_size_a == badem::block::size (type_a) + sizeof (badem::block_hash));
			result = entry_size_a - sizeof (badem::block_hash);
		}
		return result;
	}

	boost::optional<badem::db_val<Val>> block_raw_get_by_type (badem::transaction const & transaction_a, badem::block_hash const & hash_a, badem::block_type & type_a) const
	{
		badem::db_val<Val> value;
		badem::db_val<Val> hash (hash_a);
		int status = status_code_not_found ();
		switch (type_a)
		{
			case badem::block_type::send:
			{
				status = get (transaction_a, tables::send_blocks, hash, value);
				break;
			}
			case badem::block_type::receive:
			{
				status = get (transaction_a, tables::receive_blocks, hash, value);
				break;
			}
			case badem::block_type::open:
			{
				status = get (transaction_a, tables::open_blocks, hash, value);
				break;
			}
			case badem::block_type::change:
			{
				status = get (transaction_a, tables::change_blocks, hash, value);
				break;
			}
			case badem::block_type::state:
			{
				status = get (transaction_a, tables::state_blocks, hash, value);
				break;
			}
			case badem::block_type::invalid:
			case badem::block_type::not_a_block:
			{
				break;
			}
		}

		release_assert (success (status) || not_found (status));
		boost::optional<badem::db_val<Val>> result;
		if (success (status))
		{
			result = value;
		}
		return result;
	}

	tables block_database (badem::block_type type_a)
	{
		tables result = tables::frontiers;
		switch (type_a)
		{
			case badem::block_type::send:
				result = tables::send_blocks;
				break;
			case badem::block_type::receive:
				result = tables::receive_blocks;
				break;
			case badem::block_type::open:
				result = tables::open_blocks;
				break;
			case badem::block_type::change:
				result = tables::change_blocks;
				break;
			case badem::block_type::state:
				result = tables::state_blocks;
				break;
			default:
				assert (false);
				break;
		}
		return result;
	}

	size_t count (badem::transaction const & transaction_a, std::initializer_list<tables> dbs_a) const
	{
		size_t total_count = 0;
		for (auto db : dbs_a)
		{
			total_count += count (transaction_a, db);
		}
		return total_count;
	}

	int get (badem::transaction const & transaction_a, tables table_a, badem::db_val<Val> const & key_a, badem::db_val<Val> & value_a) const
	{
		return static_cast<Derived_Store const &> (*this).get (transaction_a, table_a, key_a, value_a);
	}

	int put (badem::write_transaction const & transaction_a, tables table_a, badem::db_val<Val> const & key_a, badem::db_val<Val> const & value_a)
	{
		return static_cast<Derived_Store &> (*this).put (transaction_a, table_a, key_a, value_a);
	}

	int del (badem::write_transaction const & transaction_a, tables table_a, badem::db_val<Val> const & key_a)
	{
		return static_cast<Derived_Store &> (*this).del (transaction_a, table_a, key_a);
	}

	virtual size_t count (badem::transaction const & transaction_a, tables table_a) const = 0;
	virtual int drop (badem::write_transaction const & transaction_a, tables table_a) = 0;
	virtual bool not_found (int status) const = 0;
	virtual bool success (int status) const = 0;
	virtual int status_code_not_found () const = 0;
};

/**
 * Fill in our predecessors
 */
template <typename Val, typename Derived_Store>
class block_predecessor_set : public badem::block_visitor
{
public:
	block_predecessor_set (badem::write_transaction const & transaction_a, badem::block_store_partial<Val, Derived_Store> & store_a) :
	transaction (transaction_a),
	store (store_a)
	{
	}
	virtual ~block_predecessor_set () = default;
	void fill_value (badem::block const & block_a)
	{
		auto hash (block_a.hash ());
		badem::block_type type;
		auto value (store.block_raw_get (transaction, block_a.previous (), type));
		assert (value.size () != 0);
		std::vector<uint8_t> data (static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size ());
		std::copy (hash.bytes.begin (), hash.bytes.end (), data.begin () + store.block_successor_offset (transaction, value.size (), type));
		store.block_raw_put (transaction, data, type, block_a.previous ());
	}
	void send_block (badem::send_block const & block_a) override
	{
		fill_value (block_a);
	}
	void receive_block (badem::receive_block const & block_a) override
	{
		fill_value (block_a);
	}
	void open_block (badem::open_block const & block_a) override
	{
		// Open blocks don't have a predecessor
	}
	void change_block (badem::change_block const & block_a) override
	{
		fill_value (block_a);
	}
	void state_block (badem::state_block const & block_a) override
	{
		if (!block_a.previous ().is_zero ())
		{
			fill_value (block_a);
		}
	}
	badem::write_transaction const & transaction;
	badem::block_store_partial<Val, Derived_Store> & store;
};
}
