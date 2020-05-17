#pragma once

#include <badem/crypto_lib/random_pool.hpp>
#include <badem/lib/config.hpp>
#include <badem/lib/diagnosticsconfig.hpp>
#include <badem/lib/logger_mt.hpp>
#include <badem/lib/memory.hpp>
#include <badem/lib/rocksdbconfig.hpp>
#include <badem/secure/common.hpp>
#include <badem/secure/versioning.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/polymorphic_cast.hpp>

#include <stack>

namespace badem
{
/**
 * Encapsulates database specific container
 */
template <typename Val>
class db_val
{
public:
	db_val (Val const & value_a) :
	value (value_a)
	{
	}

	db_val () :
	db_val (0, nullptr)
	{
	}

	db_val (badem::uint128_union const & val_a) :
	db_val (sizeof (val_a), const_cast<badem::uint128_union *> (&val_a))
	{
	}

	db_val (badem::uint256_union const & val_a) :
	db_val (sizeof (val_a), const_cast<badem::uint256_union *> (&val_a))
	{
	}

	db_val (badem::account_info const & val_a) :
	db_val (val_a.db_size (), const_cast<badem::account_info *> (&val_a))
	{
	}

	db_val (badem::account_info_v13 const & val_a) :
	db_val (val_a.db_size (), const_cast<badem::account_info_v13 *> (&val_a))
	{
	}

	db_val (badem::account_info_v14 const & val_a) :
	db_val (val_a.db_size (), const_cast<badem::account_info_v14 *> (&val_a))
	{
	}

	db_val (badem::pending_info const & val_a) :
	db_val (val_a.db_size (), const_cast<badem::pending_info *> (&val_a))
	{
		static_assert (std::is_standard_layout<badem::pending_info>::value, "Standard layout is required");
	}

	db_val (badem::pending_info_v14 const & val_a) :
	db_val (val_a.db_size (), const_cast<badem::pending_info_v14 *> (&val_a))
	{
		static_assert (std::is_standard_layout<badem::pending_info_v14>::value, "Standard layout is required");
	}

	db_val (badem::pending_key const & val_a) :
	db_val (sizeof (val_a), const_cast<badem::pending_key *> (&val_a))
	{
		static_assert (std::is_standard_layout<badem::pending_key>::value, "Standard layout is required");
	}

	db_val (badem::unchecked_info const & val_a) :
	buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			badem::vectorstream stream (*buffer);
			val_a.serialize (stream);
		}
		convert_buffer_to_value ();
	}

	db_val (badem::unchecked_key const & val_a) :
	db_val (sizeof (val_a), const_cast<badem::unchecked_key *> (&val_a))
	{
		static_assert (std::is_standard_layout<badem::unchecked_key>::value, "Standard layout is required");
	}

	db_val (badem::block_info const & val_a) :
	db_val (sizeof (val_a), const_cast<badem::block_info *> (&val_a))
	{
		static_assert (std::is_standard_layout<badem::block_info>::value, "Standard layout is required");
	}

	db_val (badem::endpoint_key const & val_a) :
	db_val (sizeof (val_a), const_cast<badem::endpoint_key *> (&val_a))
	{
		static_assert (std::is_standard_layout<badem::endpoint_key>::value, "Standard layout is required");
	}

	db_val (std::shared_ptr<badem::block> const & val_a) :
	buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			badem::vectorstream stream (*buffer);
			badem::serialize_block (stream, *val_a);
		}
		convert_buffer_to_value ();
	}

	db_val (uint64_t val_a) :
	buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			boost::endian::native_to_big_inplace (val_a);
			badem::vectorstream stream (*buffer);
			badem::write (stream, val_a);
		}
		convert_buffer_to_value ();
	}

	explicit operator badem::account_info () const
	{
		badem::account_info result;
		assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator badem::account_info_v13 () const
	{
		badem::account_info_v13 result;
		assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator badem::account_info_v14 () const
	{
		badem::account_info_v14 result;
		assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator badem::block_info () const
	{
		badem::block_info result;
		assert (size () == sizeof (result));
		static_assert (sizeof (badem::block_info::account) + sizeof (badem::block_info::balance) == sizeof (result), "Packed class");
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator badem::pending_info_v14 () const
	{
		badem::pending_info_v14 result;
		assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator badem::pending_info () const
	{
		badem::pending_info result;
		assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator badem::pending_key () const
	{
		badem::pending_key result;
		assert (size () == sizeof (result));
		static_assert (sizeof (badem::pending_key::account) + sizeof (badem::pending_key::hash) == sizeof (result), "Packed class");
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator badem::unchecked_info () const
	{
		badem::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		badem::unchecked_info result;
		bool error (result.deserialize (stream));
		(void)error;
		assert (!error);
		return result;
	}

	explicit operator badem::unchecked_key () const
	{
		badem::unchecked_key result;
		assert (size () == sizeof (result));
		static_assert (sizeof (badem::unchecked_key::previous) + sizeof (badem::pending_key::hash) == sizeof (result), "Packed class");
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator badem::uint128_union () const
	{
		return convert<badem::uint128_union> ();
	}

	explicit operator badem::amount () const
	{
		return convert<badem::amount> ();
	}

	explicit operator badem::block_hash () const
	{
		return convert<badem::block_hash> ();
	}

	explicit operator badem::public_key () const
	{
		return convert<badem::public_key> ();
	}

	explicit operator badem::uint256_union () const
	{
		return convert<badem::uint256_union> ();
	}

	explicit operator std::array<char, 64> () const
	{
		badem::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		std::array<char, 64> result;
		auto error = badem::try_read (stream, result);
		(void)error;
		assert (!error);
		return result;
	}

	explicit operator badem::endpoint_key () const
	{
		badem::endpoint_key result;
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator badem::state_block_w_sideband_v14 () const
	{
		badem::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		badem::state_block_w_sideband_v14 state_block_w_sideband_v14;
		state_block_w_sideband_v14.state_block = std::make_shared<badem::state_block> (error, stream);
		assert (!error);

		state_block_w_sideband_v14.sideband.type = badem::block_type::state;
		error = state_block_w_sideband_v14.sideband.deserialize (stream);
		assert (!error);

		return state_block_w_sideband_v14;
	}

	explicit operator badem::no_value () const
	{
		return no_value::dummy;
	}

	explicit operator std::shared_ptr<badem::block> () const
	{
		badem::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		std::shared_ptr<badem::block> result (badem::deserialize_block (stream));
		return result;
	}

	template <typename Block>
	std::shared_ptr<Block> convert_to_block () const
	{
		badem::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		auto result (std::make_shared<Block> (error, stream));
		assert (!error);
		return result;
	}

	explicit operator std::shared_ptr<badem::send_block> () const
	{
		return convert_to_block<badem::send_block> ();
	}

	explicit operator std::shared_ptr<badem::receive_block> () const
	{
		return convert_to_block<badem::receive_block> ();
	}

	explicit operator std::shared_ptr<badem::open_block> () const
	{
		return convert_to_block<badem::open_block> ();
	}

	explicit operator std::shared_ptr<badem::change_block> () const
	{
		return convert_to_block<badem::change_block> ();
	}

	explicit operator std::shared_ptr<badem::state_block> () const
	{
		return convert_to_block<badem::state_block> ();
	}

	explicit operator std::shared_ptr<badem::vote> () const
	{
		badem::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		auto result (badem::make_shared<badem::vote> (error, stream));
		assert (!error);
		return result;
	}

	explicit operator uint64_t () const
	{
		uint64_t result;
		badem::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (badem::try_read (stream, result));
		(void)error;
		assert (!error);
		boost::endian::big_to_native_inplace (result);
		return result;
	}

	operator Val * () const
	{
		// Allow passing a temporary to a non-c++ function which doesn't have constness
		return const_cast<Val *> (&value);
	}

	operator Val const & () const
	{
		return value;
	}

	// Must be specialized
	void * data () const;
	size_t size () const;
	db_val (size_t size_a, void * data_a);
	void convert_buffer_to_value ();

	Val value;
	std::shared_ptr<std::vector<uint8_t>> buffer;

private:
	template <typename T>
	T convert () const
	{
		T result;
		assert (size () == sizeof (result));
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
		return result;
	}
};

class block_sideband final
{
public:
	block_sideband () = default;
	block_sideband (badem::block_type, badem::account const &, badem::block_hash const &, badem::amount const &, uint64_t, uint64_t, badem::epoch);
	void serialize (badem::stream &) const;
	bool deserialize (badem::stream &);
	static size_t size (badem::block_type);
	badem::block_type type{ badem::block_type::invalid };
	badem::block_hash successor{ 0 };
	badem::account account{ 0 };
	badem::amount balance{ 0 };
	uint64_t height{ 0 };
	uint64_t timestamp{ 0 };
	badem::epoch epoch{ badem::epoch::epoch_0 };
};
class transaction;
class block_store;

/**
 * Summation visitor for blocks, supporting amount and balance computations. These
 * computations are mutually dependant. The natural solution is to use mutual recursion
 * between balance and amount visitors, but this leads to very deep stacks. Hence, the
 * summation visitor uses an iterative approach.
 */
class summation_visitor final : public badem::block_visitor
{
	enum summation_type
	{
		invalid = 0,
		balance = 1,
		amount = 2
	};

	/** Represents an invocation frame */
	class frame final
	{
	public:
		frame (summation_type type_a, badem::block_hash balance_hash_a, badem::block_hash amount_hash_a) :
		type (type_a), balance_hash (balance_hash_a), amount_hash (amount_hash_a)
		{
		}

		/** The summation type guides the block visitor handlers */
		summation_type type{ invalid };
		/** Accumulated balance or amount */
		badem::uint128_t sum{ 0 };
		/** The current balance hash */
		badem::block_hash balance_hash{ 0 };
		/** The current amount hash */
		badem::block_hash amount_hash{ 0 };
		/** If true, this frame is awaiting an invocation result */
		bool awaiting_result{ false };
		/** Set by the invoked frame, representing the return value */
		badem::uint128_t incoming_result{ 0 };
	};

public:
	summation_visitor (badem::transaction const &, badem::block_store const &, bool is_v14_upgrade = false);
	virtual ~summation_visitor () = default;
	/** Computes the balance as of \p block_hash */
	badem::uint128_t compute_balance (badem::block_hash const & block_hash);
	/** Computes the amount delta between \p block_hash and its predecessor */
	badem::uint128_t compute_amount (badem::block_hash const & block_hash);

protected:
	badem::transaction const & transaction;
	badem::block_store const & store;
	badem::network_params network_params;

	/** The final result */
	badem::uint128_t result{ 0 };
	/** The current invocation frame */
	frame * current{ nullptr };
	/** Invocation frames */
	std::stack<frame> frames;
	/** Push a copy of \p hash of the given summation \p type */
	badem::summation_visitor::frame push (badem::summation_visitor::summation_type type, badem::block_hash const & hash);
	void sum_add (badem::uint128_t addend_a);
	void sum_set (badem::uint128_t value_a);
	/** The epilogue yields the result to previous frame, if any */
	void epilogue ();

	badem::uint128_t compute_internal (badem::summation_visitor::summation_type type, badem::block_hash const &);
	void send_block (badem::send_block const &) override;
	void receive_block (badem::receive_block const &) override;
	void open_block (badem::open_block const &) override;
	void change_block (badem::change_block const &) override;
	void state_block (badem::state_block const &) override;

private:
	bool is_v14_upgrade;
	std::shared_ptr<badem::block> block_get (badem::transaction const &, badem::block_hash const &) const;
};

/**
 * Determine the representative for this block
 */
class representative_visitor final : public badem::block_visitor
{
public:
	representative_visitor (badem::transaction const & transaction_a, badem::block_store & store_a);
	~representative_visitor () = default;
	void compute (badem::block_hash const & hash_a);
	void send_block (badem::send_block const & block_a) override;
	void receive_block (badem::receive_block const & block_a) override;
	void open_block (badem::open_block const & block_a) override;
	void change_block (badem::change_block const & block_a) override;
	void state_block (badem::state_block const & block_a) override;
	badem::transaction const & transaction;
	badem::block_store & store;
	badem::block_hash current;
	badem::block_hash result;
};
template <typename T, typename U>
class store_iterator_impl
{
public:
	virtual ~store_iterator_impl () = default;
	virtual badem::store_iterator_impl<T, U> & operator++ () = 0;
	virtual bool operator== (badem::store_iterator_impl<T, U> const & other_a) const = 0;
	virtual bool is_end_sentinal () const = 0;
	virtual void fill (std::pair<T, U> &) const = 0;
	badem::store_iterator_impl<T, U> & operator= (badem::store_iterator_impl<T, U> const &) = delete;
	bool operator== (badem::store_iterator_impl<T, U> const * other_a) const
	{
		return (other_a != nullptr && *this == *other_a) || (other_a == nullptr && is_end_sentinal ());
	}
	bool operator!= (badem::store_iterator_impl<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}
};
/**
 * Iterates the key/value pairs of a transaction
 */
template <typename T, typename U>
class store_iterator final
{
public:
	store_iterator (std::nullptr_t)
	{
	}
	store_iterator (std::unique_ptr<badem::store_iterator_impl<T, U>> impl_a) :
	impl (std::move (impl_a))
	{
		impl->fill (current);
	}
	store_iterator (badem::store_iterator<T, U> && other_a) :
	current (std::move (other_a.current)),
	impl (std::move (other_a.impl))
	{
	}
	badem::store_iterator<T, U> & operator++ ()
	{
		++*impl;
		impl->fill (current);
		return *this;
	}
	badem::store_iterator<T, U> & operator= (badem::store_iterator<T, U> && other_a)
	{
		impl = std::move (other_a.impl);
		current = std::move (other_a.current);
		return *this;
	}
	badem::store_iterator<T, U> & operator= (badem::store_iterator<T, U> const &) = delete;
	std::pair<T, U> * operator-> ()
	{
		return &current;
	}
	bool operator== (badem::store_iterator<T, U> const & other_a) const
	{
		return (impl == nullptr && other_a.impl == nullptr) || (impl != nullptr && *impl == other_a.impl.get ()) || (other_a.impl != nullptr && *other_a.impl == impl.get ());
	}
	bool operator!= (badem::store_iterator<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}

private:
	std::pair<T, U> current;
	std::unique_ptr<badem::store_iterator_impl<T, U>> impl;
};

// Keep this in alphabetical order
enum class tables
{
	accounts,
	blocks_info, // LMDB only
	cached_counts, // RocksDB only
	change_blocks,
	confirmation_height,
	frontiers,
	meta,
	online_weight,
	open_blocks,
	peers,
	pending,
	receive_blocks,
	representation,
	send_blocks,
	state_blocks,
	unchecked,
	vote
};

class transaction_impl
{
public:
	virtual ~transaction_impl () = default;
	virtual void * get_handle () const = 0;
};

class read_transaction_impl : public transaction_impl
{
public:
	virtual void reset () = 0;
	virtual void renew () = 0;
};

class write_transaction_impl : public transaction_impl
{
public:
	virtual void commit () const = 0;
	virtual void renew () = 0;
	virtual bool contains (badem::tables table_a) const = 0;
};

class transaction
{
public:
	virtual ~transaction () = default;
	virtual void * get_handle () const = 0;
};

/**
 * RAII wrapper of a read MDB_txn where the constructor starts the transaction
 * and the destructor aborts it.
 */
class read_transaction final : public transaction
{
public:
	explicit read_transaction (std::unique_ptr<badem::read_transaction_impl> read_transaction_impl);
	void * get_handle () const override;
	void reset () const;
	void renew () const;
	void refresh () const;

private:
	std::unique_ptr<badem::read_transaction_impl> impl;
};

/**
 * RAII wrapper of a read-write MDB_txn where the constructor starts the transaction
 * and the destructor commits it.
 */
class write_transaction final : public transaction
{
public:
	explicit write_transaction (std::unique_ptr<badem::write_transaction_impl> write_transaction_impl);
	void * get_handle () const override;
	void commit () const;
	void renew ();
	bool contains (badem::tables table_a) const;

private:
	std::unique_ptr<badem::write_transaction_impl> impl;
};

class rep_weights;

/**
 * Manages block storage and iteration
 */
class block_store
{
public:
	virtual ~block_store () = default;
	virtual void initialize (badem::write_transaction const &, badem::genesis const &, badem::rep_weights &, std::atomic<uint64_t> &, std::atomic<uint64_t> &) = 0;
	virtual void block_put (badem::write_transaction const &, badem::block_hash const &, badem::block const &, badem::block_sideband const &) = 0;
	virtual badem::block_hash block_successor (badem::transaction const &, badem::block_hash const &) const = 0;
	virtual void block_successor_clear (badem::write_transaction const &, badem::block_hash const &) = 0;
	virtual std::shared_ptr<badem::block> block_get (badem::transaction const &, badem::block_hash const &, badem::block_sideband * = nullptr) const = 0;
	virtual std::shared_ptr<badem::block> block_get_v14 (badem::transaction const &, badem::block_hash const &, badem::block_sideband_v14 * = nullptr, bool * = nullptr) const = 0;
	virtual std::shared_ptr<badem::block> block_random (badem::transaction const &) = 0;
	virtual void block_del (badem::write_transaction const &, badem::block_hash const &) = 0;
	virtual bool block_exists (badem::transaction const &, badem::block_hash const &) = 0;
	virtual bool block_exists (badem::transaction const &, badem::block_type, badem::block_hash const &) = 0;
	virtual badem::block_counts block_count (badem::transaction const &) = 0;
	virtual bool root_exists (badem::transaction const &, badem::root const &) = 0;
	virtual bool source_exists (badem::transaction const &, badem::block_hash const &) = 0;
	virtual badem::account block_account (badem::transaction const &, badem::block_hash const &) const = 0;

	virtual void frontier_put (badem::write_transaction const &, badem::block_hash const &, badem::account const &) = 0;
	virtual badem::account frontier_get (badem::transaction const &, badem::block_hash const &) const = 0;
	virtual void frontier_del (badem::write_transaction const &, badem::block_hash const &) = 0;

	virtual void account_put (badem::write_transaction const &, badem::account const &, badem::account_info const &) = 0;
	virtual bool account_get (badem::transaction const &, badem::account const &, badem::account_info &) = 0;
	virtual void account_del (badem::write_transaction const &, badem::account const &) = 0;
	virtual bool account_exists (badem::transaction const &, badem::account const &) = 0;
	virtual size_t account_count (badem::transaction const &) = 0;
	virtual void confirmation_height_clear (badem::write_transaction const &, badem::account const & account, uint64_t existing_confirmation_height) = 0;
	virtual void confirmation_height_clear (badem::write_transaction const &) = 0;
	virtual badem::store_iterator<badem::account, badem::account_info> latest_begin (badem::transaction const &, badem::account const &) = 0;
	virtual badem::store_iterator<badem::account, badem::account_info> latest_begin (badem::transaction const &) = 0;
	virtual badem::store_iterator<badem::account, badem::account_info> latest_end () = 0;

	virtual void pending_put (badem::write_transaction const &, badem::pending_key const &, badem::pending_info const &) = 0;
	virtual void pending_del (badem::write_transaction const &, badem::pending_key const &) = 0;
	virtual bool pending_get (badem::transaction const &, badem::pending_key const &, badem::pending_info &) = 0;
	virtual bool pending_exists (badem::transaction const &, badem::pending_key const &) = 0;
	virtual badem::store_iterator<badem::pending_key, badem::pending_info> pending_begin (badem::transaction const &, badem::pending_key const &) = 0;
	virtual badem::store_iterator<badem::pending_key, badem::pending_info> pending_begin (badem::transaction const &) = 0;
	virtual badem::store_iterator<badem::pending_key, badem::pending_info> pending_end () = 0;

	virtual bool block_info_get (badem::transaction const &, badem::block_hash const &, badem::block_info &) const = 0;
	virtual badem::uint128_t block_balance (badem::transaction const &, badem::block_hash const &) = 0;
	virtual badem::uint128_t block_balance_calculated (std::shared_ptr<badem::block>, badem::block_sideband const &) const = 0;
	virtual badem::epoch block_version (badem::transaction const &, badem::block_hash const &) = 0;

	virtual void unchecked_clear (badem::write_transaction const &) = 0;
	virtual void unchecked_put (badem::write_transaction const &, badem::unchecked_key const &, badem::unchecked_info const &) = 0;
	virtual void unchecked_put (badem::write_transaction const &, badem::block_hash const &, std::shared_ptr<badem::block> const &) = 0;
	virtual std::vector<badem::unchecked_info> unchecked_get (badem::transaction const &, badem::block_hash const &) = 0;
	virtual void unchecked_del (badem::write_transaction const &, badem::unchecked_key const &) = 0;
	virtual badem::store_iterator<badem::unchecked_key, badem::unchecked_info> unchecked_begin (badem::transaction const &) = 0;
	virtual badem::store_iterator<badem::unchecked_key, badem::unchecked_info> unchecked_begin (badem::transaction const &, badem::unchecked_key const &) = 0;
	virtual badem::store_iterator<badem::unchecked_key, badem::unchecked_info> unchecked_end () = 0;
	virtual size_t unchecked_count (badem::transaction const &) = 0;

	// Return latest vote for an account from store
	virtual std::shared_ptr<badem::vote> vote_get (badem::transaction const &, badem::account const &) = 0;
	// Populate vote with the next sequence number
	virtual std::shared_ptr<badem::vote> vote_generate (badem::transaction const &, badem::account const &, badem::raw_key const &, std::shared_ptr<badem::block>) = 0;
	virtual std::shared_ptr<badem::vote> vote_generate (badem::transaction const &, badem::account const &, badem::raw_key const &, std::vector<badem::block_hash>) = 0;
	// Return either vote or the stored vote with a higher sequence number
	virtual std::shared_ptr<badem::vote> vote_max (badem::transaction const &, std::shared_ptr<badem::vote>) = 0;
	// Return latest vote for an account considering the vote cache
	virtual std::shared_ptr<badem::vote> vote_current (badem::transaction const &, badem::account const &) = 0;
	virtual void flush (badem::write_transaction const &) = 0;
	virtual badem::store_iterator<badem::account, std::shared_ptr<badem::vote>> vote_begin (badem::transaction const &) = 0;
	virtual badem::store_iterator<badem::account, std::shared_ptr<badem::vote>> vote_end () = 0;

	virtual void online_weight_put (badem::write_transaction const &, uint64_t, badem::amount const &) = 0;
	virtual void online_weight_del (badem::write_transaction const &, uint64_t) = 0;
	virtual badem::store_iterator<uint64_t, badem::amount> online_weight_begin (badem::transaction const &) const = 0;
	virtual badem::store_iterator<uint64_t, badem::amount> online_weight_end () const = 0;
	virtual size_t online_weight_count (badem::transaction const &) const = 0;
	virtual void online_weight_clear (badem::write_transaction const &) = 0;

	virtual void version_put (badem::write_transaction const &, int) = 0;
	virtual int version_get (badem::transaction const &) const = 0;

	virtual void peer_put (badem::write_transaction const & transaction_a, badem::endpoint_key const & endpoint_a) = 0;
	virtual void peer_del (badem::write_transaction const & transaction_a, badem::endpoint_key const & endpoint_a) = 0;
	virtual bool peer_exists (badem::transaction const & transaction_a, badem::endpoint_key const & endpoint_a) const = 0;
	virtual size_t peer_count (badem::transaction const & transaction_a) const = 0;
	virtual void peer_clear (badem::write_transaction const & transaction_a) = 0;
	virtual badem::store_iterator<badem::endpoint_key, badem::no_value> peers_begin (badem::transaction const & transaction_a) const = 0;
	virtual badem::store_iterator<badem::endpoint_key, badem::no_value> peers_end () const = 0;

	virtual void confirmation_height_put (badem::write_transaction const & transaction_a, badem::account const & account_a, uint64_t confirmation_height_a) = 0;
	virtual bool confirmation_height_get (badem::transaction const & transaction_a, badem::account const & account_a, uint64_t & confirmation_height_a) = 0;
	virtual bool confirmation_height_exists (badem::transaction const & transaction_a, badem::account const & account_a) const = 0;
	virtual void confirmation_height_del (badem::write_transaction const & transaction_a, badem::account const & account_a) = 0;
	virtual uint64_t confirmation_height_count (badem::transaction const & transaction_a) = 0;
	virtual badem::store_iterator<badem::account, uint64_t> confirmation_height_begin (badem::transaction const & transaction_a, badem::account const & account_a) = 0;
	virtual badem::store_iterator<badem::account, uint64_t> confirmation_height_begin (badem::transaction const & transaction_a) = 0;
	virtual badem::store_iterator<badem::account, uint64_t> confirmation_height_end () = 0;

	virtual uint64_t block_account_height (badem::transaction const & transaction_a, badem::block_hash const & hash_a) const = 0;
	virtual std::mutex & get_cache_mutex () = 0;

	virtual bool copy_db (boost::filesystem::path const & destination) = 0;

	/** Not applicable to all sub-classes */
	virtual void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds) = 0;

	virtual bool init_error () const = 0;

	/** Start read-write transaction */
	virtual badem::write_transaction tx_begin_write (std::vector<badem::tables> const & tables_to_lock = {}, std::vector<badem::tables> const & tables_no_lock = {}) = 0;

	/** Start read-only transaction */
	virtual badem::read_transaction tx_begin_read () = 0;
};

std::unique_ptr<badem::block_store> make_store (badem::logger_mt & logger, boost::filesystem::path const & path, bool open_read_only = false, bool add_db_postfix = false, badem::rocksdb_config const & rocksdb_config = badem::rocksdb_config{}, badem::txn_tracking_config const & txn_tracking_config_a = badem::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), int lmdb_max_dbs = 128, size_t batch_size = 512, bool backup_before_upgrade = false, bool rocksdb_backend = false);
}

namespace std
{
template <>
struct hash<::badem::tables>
{
	size_t operator() (::badem::tables const & table_a) const
	{
		return static_cast<size_t> (table_a);
	}
};
}
