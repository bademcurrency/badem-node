#pragma once

#include <badem/crypto/blake2/blake2.h>
#include <badem/lib/errors.hpp>
#include <badem/lib/numbers.hpp>
#include <badem/lib/utility.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <cassert>
#include <streambuf>
#include <unordered_map>

namespace badem
{
// We operate on streams of uint8_t by convention
using stream = std::basic_streambuf<uint8_t>;
// Read a raw byte stream the size of `T' and fill value.
template <typename T>
bool try_read (badem::stream & stream_a, T & value)
{
	static_assert (std::is_standard_layout<T>::value, "Can't stream read non-standard layout types");
	auto amount_read (stream_a.sgetn (reinterpret_cast<uint8_t *> (&value), sizeof (value)));
	return amount_read != sizeof (value);
}
// A wrapper of try_read which throws if there is an error
template <typename T>
void read (badem::stream & stream_a, T & value)
{
	auto error = try_read (stream_a, value);
	if (error)
	{
		throw std::runtime_error ("Failed to read type");
	}
}

template <typename T>
void write (badem::stream & stream_a, T const & value)
{
	static_assert (std::is_standard_layout<T>::value, "Can't stream write non-standard layout types");
	auto amount_written (stream_a.sputn (reinterpret_cast<uint8_t const *> (&value), sizeof (value)));
	(void)amount_written;
	assert (amount_written == sizeof (value));
}
class block_visitor;
enum class block_type : uint8_t
{
	invalid = 0,
	not_a_block = 1,
	send = 2,
	receive = 3,
	open = 4,
	change = 5,
	state = 6
};
class block
{
public:
	// Return a digest of the hashables in this block.
	badem::block_hash hash () const;
	// Return a digest of hashables and non-hashables in this block.
	badem::block_hash full_hash () const;
	std::string to_json () const;
	virtual void hash (blake2b_state &) const = 0;
	virtual uint64_t block_work () const = 0;
	virtual void block_work_set (uint64_t) = 0;
	virtual badem::account account () const;
	// Previous block in account's chain, zero for open block
	virtual badem::block_hash previous () const = 0;
	// Source block for open/receive blocks, zero otherwise.
	virtual badem::block_hash source () const;
	// Previous block or account number for open blocks
	virtual badem::block_hash root () const = 0;
	// Qualified root value based on previous() and root()
	virtual badem::qualified_root qualified_root () const;
	// Link field for state blocks, zero otherwise.
	virtual badem::block_hash link () const;
	virtual badem::account representative () const;
	virtual void serialize (badem::stream &) const = 0;
	virtual void serialize_json (std::string &) const = 0;
	virtual void serialize_json (boost::property_tree::ptree &) const = 0;
	virtual void visit (badem::block_visitor &) const = 0;
	virtual bool operator== (badem::block const &) const = 0;
	virtual badem::block_type type () const = 0;
	virtual badem::signature block_signature () const = 0;
	virtual void signature_set (badem::uint512_union const &) = 0;
	virtual ~block () = default;
	virtual bool valid_predecessor (badem::block const &) const = 0;
	static size_t size (badem::block_type);
};
class send_hashables
{
public:
	send_hashables () = default;
	send_hashables (badem::account const &, badem::block_hash const &, badem::amount const &);
	send_hashables (bool &, badem::stream &);
	send_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	badem::block_hash previous;
	badem::account destination;
	badem::amount balance;
	static size_t constexpr size = sizeof (previous) + sizeof (destination) + sizeof (balance);
};
class send_block : public badem::block
{
public:
	send_block () = default;
	send_block (badem::block_hash const &, badem::account const &, badem::amount const &, badem::raw_key const &, badem::public_key const &, uint64_t);
	send_block (bool &, badem::stream &);
	send_block (bool &, boost::property_tree::ptree const &);
	virtual ~send_block () = default;
	using badem::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	badem::block_hash previous () const override;
	badem::block_hash root () const override;
	void serialize (badem::stream &) const override;
	bool deserialize (badem::stream &);
	void serialize_json (std::string &) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (badem::block_visitor &) const override;
	badem::block_type type () const override;
	badem::signature block_signature () const override;
	void signature_set (badem::uint512_union const &) override;
	bool operator== (badem::block const &) const override;
	bool operator== (badem::send_block const &) const;
	bool valid_predecessor (badem::block const &) const override;
	send_hashables hashables;
	badem::signature signature;
	uint64_t work;
	static size_t constexpr size = badem::send_hashables::size + sizeof (signature) + sizeof (work);
};
class receive_hashables
{
public:
	receive_hashables () = default;
	receive_hashables (badem::block_hash const &, badem::block_hash const &);
	receive_hashables (bool &, badem::stream &);
	receive_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	badem::block_hash previous;
	badem::block_hash source;
	static size_t constexpr size = sizeof (previous) + sizeof (source);
};
class receive_block : public badem::block
{
public:
	receive_block () = default;
	receive_block (badem::block_hash const &, badem::block_hash const &, badem::raw_key const &, badem::public_key const &, uint64_t);
	receive_block (bool &, badem::stream &);
	receive_block (bool &, boost::property_tree::ptree const &);
	virtual ~receive_block () = default;
	using badem::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	badem::block_hash previous () const override;
	badem::block_hash source () const override;
	badem::block_hash root () const override;
	void serialize (badem::stream &) const override;
	bool deserialize (badem::stream &);
	void serialize_json (std::string &) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (badem::block_visitor &) const override;
	badem::block_type type () const override;
	badem::signature block_signature () const override;
	void signature_set (badem::uint512_union const &) override;
	bool operator== (badem::block const &) const override;
	bool operator== (badem::receive_block const &) const;
	bool valid_predecessor (badem::block const &) const override;
	receive_hashables hashables;
	badem::signature signature;
	uint64_t work;
	static size_t constexpr size = badem::receive_hashables::size + sizeof (signature) + sizeof (work);
};
class open_hashables
{
public:
	open_hashables () = default;
	open_hashables (badem::block_hash const &, badem::account const &, badem::account const &);
	open_hashables (bool &, badem::stream &);
	open_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	badem::block_hash source;
	badem::account representative;
	badem::account account;
	static size_t constexpr size = sizeof (source) + sizeof (representative) + sizeof (account);
};
class open_block : public badem::block
{
public:
	open_block () = default;
	open_block (badem::block_hash const &, badem::account const &, badem::account const &, badem::raw_key const &, badem::public_key const &, uint64_t);
	open_block (badem::block_hash const &, badem::account const &, badem::account const &, std::nullptr_t);
	open_block (bool &, badem::stream &);
	open_block (bool &, boost::property_tree::ptree const &);
	virtual ~open_block () = default;
	using badem::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	badem::block_hash previous () const override;
	badem::account account () const override;
	badem::block_hash source () const override;
	badem::block_hash root () const override;
	badem::account representative () const override;
	void serialize (badem::stream &) const override;
	bool deserialize (badem::stream &);
	void serialize_json (std::string &) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (badem::block_visitor &) const override;
	badem::block_type type () const override;
	badem::signature block_signature () const override;
	void signature_set (badem::uint512_union const &) override;
	bool operator== (badem::block const &) const override;
	bool operator== (badem::open_block const &) const;
	bool valid_predecessor (badem::block const &) const override;
	badem::open_hashables hashables;
	badem::signature signature;
	uint64_t work;
	static size_t constexpr size = badem::open_hashables::size + sizeof (signature) + sizeof (work);
};
class change_hashables
{
public:
	change_hashables () = default;
	change_hashables (badem::block_hash const &, badem::account const &);
	change_hashables (bool &, badem::stream &);
	change_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	badem::block_hash previous;
	badem::account representative;
	static size_t constexpr size = sizeof (previous) + sizeof (representative);
};
class change_block : public badem::block
{
public:
	change_block () = default;
	change_block (badem::block_hash const &, badem::account const &, badem::raw_key const &, badem::public_key const &, uint64_t);
	change_block (bool &, badem::stream &);
	change_block (bool &, boost::property_tree::ptree const &);
	virtual ~change_block () = default;
	using badem::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	badem::block_hash previous () const override;
	badem::block_hash root () const override;
	badem::account representative () const override;
	void serialize (badem::stream &) const override;
	bool deserialize (badem::stream &);
	void serialize_json (std::string &) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (badem::block_visitor &) const override;
	badem::block_type type () const override;
	badem::signature block_signature () const override;
	void signature_set (badem::uint512_union const &) override;
	bool operator== (badem::block const &) const override;
	bool operator== (badem::change_block const &) const;
	bool valid_predecessor (badem::block const &) const override;
	badem::change_hashables hashables;
	badem::signature signature;
	uint64_t work;
	static size_t constexpr size = badem::change_hashables::size + sizeof (signature) + sizeof (work);
};
class state_hashables
{
public:
	state_hashables () = default;
	state_hashables (badem::account const &, badem::block_hash const &, badem::account const &, badem::amount const &, badem::uint256_union const &);
	state_hashables (bool &, badem::stream &);
	state_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	// Account# / public key that operates this account
	// Uses:
	// Bulk signature validation in advance of further ledger processing
	// Arranging uncomitted transactions by account
	badem::account account;
	// Previous transaction in this chain
	badem::block_hash previous;
	// Representative of this account
	badem::account representative;
	// Current balance of this account
	// Allows lookup of account balance simply by looking at the head block
	badem::amount balance;
	// Link field contains source block_hash if receiving, destination account if sending
	badem::uint256_union link;
	// Serialized size
	static size_t constexpr size = sizeof (account) + sizeof (previous) + sizeof (representative) + sizeof (balance) + sizeof (link);
};
class state_block : public badem::block
{
public:
	state_block () = default;
	state_block (badem::account const &, badem::block_hash const &, badem::account const &, badem::amount const &, badem::uint256_union const &, badem::raw_key const &, badem::public_key const &, uint64_t);
	state_block (bool &, badem::stream &);
	state_block (bool &, boost::property_tree::ptree const &);
	virtual ~state_block () = default;
	using badem::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	badem::block_hash previous () const override;
	badem::account account () const override;
	badem::block_hash root () const override;
	badem::block_hash link () const override;
	badem::account representative () const override;
	void serialize (badem::stream &) const override;
	bool deserialize (badem::stream &);
	void serialize_json (std::string &) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (badem::block_visitor &) const override;
	badem::block_type type () const override;
	badem::signature block_signature () const override;
	void signature_set (badem::uint512_union const &) override;
	bool operator== (badem::block const &) const override;
	bool operator== (badem::state_block const &) const;
	bool valid_predecessor (badem::block const &) const override;
	badem::state_hashables hashables;
	badem::signature signature;
	uint64_t work;
	static size_t constexpr size = badem::state_hashables::size + sizeof (signature) + sizeof (work);
};
class block_visitor
{
public:
	virtual void send_block (badem::send_block const &) = 0;
	virtual void receive_block (badem::receive_block const &) = 0;
	virtual void open_block (badem::open_block const &) = 0;
	virtual void change_block (badem::change_block const &) = 0;
	virtual void state_block (badem::state_block const &) = 0;
	virtual ~block_visitor () = default;
};
/**
 * This class serves to find and return unique variants of a block in order to minimize memory usage
 */
class block_uniquer
{
public:
	using value_type = std::pair<const badem::uint256_union, std::weak_ptr<badem::block>>;

	std::shared_ptr<badem::block> unique (std::shared_ptr<badem::block>);
	size_t size ();

private:
	std::mutex mutex;
	std::unordered_map<std::remove_const_t<value_type::first_type>, value_type::second_type> blocks;
	static unsigned constexpr cleanup_count = 2;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_uniquer & block_uniquer, const std::string & name);

std::shared_ptr<badem::block> deserialize_block (badem::stream &);
std::shared_ptr<badem::block> deserialize_block (badem::stream &, badem::block_type, badem::block_uniquer * = nullptr);
std::shared_ptr<badem::block> deserialize_block_json (boost::property_tree::ptree const &, badem::block_uniquer * = nullptr);
void serialize_block (badem::stream &, badem::block const &);
void block_memory_pool_purge ();
}
