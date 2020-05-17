#pragma once

#include <badem/crypto/blake2/blake2.h>
#include <badem/lib/blockbuilders.hpp>
#include <badem/lib/blocks.hpp>
#include <badem/lib/config.hpp>
#include <badem/lib/numbers.hpp>
#include <badem/lib/utility.hpp>
#include <badem/secure/epoch.hpp>
#include <badem/secure/utility.hpp>

#include <boost/iterator/transform_iterator.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/variant.hpp>

#include <unordered_map>

namespace boost
{
template <>
struct hash<::badem::uint256_union>
{
	size_t operator() (::badem::uint256_union const & value_a) const
	{
		return std::hash<::badem::uint256_union> () (value_a);
	}
};

template <>
struct hash<::badem::block_hash>
{
	size_t operator() (::badem::block_hash const & value_a) const
	{
		return std::hash<::badem::block_hash> () (value_a);
	}
};

template <>
struct hash<::badem::public_key>
{
	size_t operator() (::badem::public_key const & value_a) const
	{
		return std::hash<::badem::public_key> () (value_a);
	}
};
template <>
struct hash<::badem::uint512_union>
{
	size_t operator() (::badem::uint512_union const & value_a) const
	{
		return std::hash<::badem::uint512_union> () (value_a);
	}
};
template <>
struct hash<::badem::qualified_root>
{
	size_t operator() (::badem::qualified_root const & value_a) const
	{
		return std::hash<::badem::qualified_root> () (value_a);
	}
};
}
namespace badem
{
/**
 * A key pair. The private key is generated from the random pool, or passed in
 * as a hex string. The public key is derived using ed25519.
 */
class keypair
{
public:
	keypair ();
	keypair (std::string const &);
	keypair (badem::raw_key &&);
	badem::public_key pub;
	badem::raw_key prv;
};

/**
 * Latest information about an account
 */
class account_info final
{
public:
	account_info () = default;
	account_info (badem::block_hash const &, badem::account const &, badem::block_hash const &, badem::amount const &, uint64_t, uint64_t, epoch);
	bool deserialize (badem::stream &);
	bool operator== (badem::account_info const &) const;
	bool operator!= (badem::account_info const &) const;
	size_t db_size () const;
	badem::epoch epoch () const;
	badem::block_hash head{ 0 };
	badem::account representative{ 0 };
	badem::block_hash open_block{ 0 };
	badem::amount balance{ 0 };
	/** Seconds since posix epoch */
	uint64_t modified{ 0 };
	uint64_t block_count{ 0 };
	badem::epoch epoch_m{ badem::epoch::epoch_0 };
};

/**
 * Information on an uncollected send
 */
class pending_info final
{
public:
	pending_info () = default;
	pending_info (badem::account const &, badem::amount const &, badem::epoch);
	size_t db_size () const;
	bool deserialize (badem::stream &);
	bool operator== (badem::pending_info const &) const;
	badem::account source{ 0 };
	badem::amount amount{ 0 };
	badem::epoch epoch{ badem::epoch::epoch_0 };
};
class pending_key final
{
public:
	pending_key () = default;
	pending_key (badem::account const &, badem::block_hash const &);
	bool deserialize (badem::stream &);
	bool operator== (badem::pending_key const &) const;
	badem::account const & key () const;
	badem::account account{ 0 };
	badem::block_hash hash{ 0 };
};

class endpoint_key final
{
public:
	endpoint_key () = default;

	/*
	 * @param address_a This should be in network byte order
	 * @param port_a This should be in host byte order
	 */
	endpoint_key (const std::array<uint8_t, 16> & address_a, uint16_t port_a);

	/*
	 * @return The ipv6 address in network byte order
	 */
	const std::array<uint8_t, 16> & address_bytes () const;

	/*
	 * @return The port in host byte order
	 */
	uint16_t port () const;

private:
	// Both stored internally in network byte order
	std::array<uint8_t, 16> address;
	uint16_t network_port{ 0 };
};

enum class no_value
{
	dummy
};

class unchecked_key final
{
public:
	unchecked_key () = default;
	unchecked_key (badem::block_hash const &, badem::block_hash const &);
	bool deserialize (badem::stream &);
	bool operator== (badem::unchecked_key const &) const;
	badem::block_hash const & key () const;
	badem::block_hash previous{ 0 };
	badem::block_hash hash{ 0 };
};

/**
 * Tag for block signature verification result
 */
enum class signature_verification : uint8_t
{
	unknown = 0,
	invalid = 1,
	valid = 2,
	valid_epoch = 3 // Valid for epoch blocks
};

/**
 * Information on an unchecked block
 */
class unchecked_info final
{
public:
	unchecked_info () = default;
	unchecked_info (std::shared_ptr<badem::block>, badem::account const &, uint64_t, badem::signature_verification = badem::signature_verification::unknown, bool = false);
	void serialize (badem::stream &) const;
	bool deserialize (badem::stream &);
	std::shared_ptr<badem::block> block;
	badem::account account{ 0 };
	/** Seconds since posix epoch */
	uint64_t modified{ 0 };
	badem::signature_verification verified{ badem::signature_verification::unknown };
	bool confirmed{ false };
};

class block_info final
{
public:
	block_info () = default;
	block_info (badem::account const &, badem::amount const &);
	badem::account account{ 0 };
	badem::amount balance{ 0 };
};
class block_counts final
{
public:
	size_t sum () const;
	size_t send{ 0 };
	size_t receive{ 0 };
	size_t open{ 0 };
	size_t change{ 0 };
	size_t state{ 0 };
};
using vote_blocks_vec_iter = std::vector<boost::variant<std::shared_ptr<badem::block>, badem::block_hash>>::const_iterator;
class iterate_vote_blocks_as_hash final
{
public:
	iterate_vote_blocks_as_hash () = default;
	badem::block_hash operator() (boost::variant<std::shared_ptr<badem::block>, badem::block_hash> const & item) const;
};
class vote final
{
public:
	vote () = default;
	vote (badem::vote const &);
	vote (bool &, badem::stream &, badem::block_uniquer * = nullptr);
	vote (bool &, badem::stream &, badem::block_type, badem::block_uniquer * = nullptr);
	vote (badem::account const &, badem::raw_key const &, uint64_t, std::shared_ptr<badem::block>);
	vote (badem::account const &, badem::raw_key const &, uint64_t, std::vector<badem::block_hash> const &);
	std::string hashes_string () const;
	badem::block_hash hash () const;
	badem::block_hash full_hash () const;
	bool operator== (badem::vote const &) const;
	bool operator!= (badem::vote const &) const;
	void serialize (badem::stream &, badem::block_type) const;
	void serialize (badem::stream &) const;
	void serialize_json (boost::property_tree::ptree & tree) const;
	bool deserialize (badem::stream &, badem::block_uniquer * = nullptr);
	bool validate () const;
	boost::transform_iterator<badem::iterate_vote_blocks_as_hash, badem::vote_blocks_vec_iter> begin () const;
	boost::transform_iterator<badem::iterate_vote_blocks_as_hash, badem::vote_blocks_vec_iter> end () const;
	std::string to_json () const;
	// Vote round sequence number
	uint64_t sequence;
	// The blocks, or block hashes, that this vote is for
	std::vector<boost::variant<std::shared_ptr<badem::block>, badem::block_hash>> blocks;
	// Account that's voting
	badem::account account;
	// Signature of sequence + block hashes
	badem::signature signature;
	static const std::string hash_prefix;
};
/**
 * This class serves to find and return unique variants of a vote in order to minimize memory usage
 */
class vote_uniquer final
{
public:
	using value_type = std::pair<const badem::block_hash, std::weak_ptr<badem::vote>>;

	vote_uniquer (badem::block_uniquer &);
	std::shared_ptr<badem::vote> unique (std::shared_ptr<badem::vote>);
	size_t size ();

private:
	badem::block_uniquer & uniquer;
	std::mutex mutex;
	std::unordered_map<std::remove_const_t<value_type::first_type>, value_type::second_type> votes;
	static unsigned constexpr cleanup_count = 2;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_uniquer & vote_uniquer, const std::string & name);

enum class vote_code
{
	invalid, // Vote is not signed correctly
	replay, // Vote does not have the highest sequence number, it's a replay
	vote // Vote has the highest sequence number
};

enum class process_result
{
	progress, // Hasn't been seen before, signed correctly
	bad_signature, // Signature was bad, forged or transmission error
	old, // Already seen and was valid
	negative_spend, // Malicious attempt to spend a negative amount
	fork, // Malicious fork based on previous
	unreceivable, // Source block doesn't exist, has already been received, or requires an account upgrade (epoch blocks)
	gap_previous, // Block marked as previous is unknown
	gap_source, // Block marked as source is unknown
	opened_burn_account, // The impossible happened, someone found the private key associated with the public key '0'.
	balance_mismatch, // Balance and amount delta don't match
	representative_mismatch, // Representative is changed when it is not allowed
	block_position // This block cannot follow the previous block
};
class process_return final
{
public:
	badem::process_result code;
	badem::account account;
	badem::amount amount;
	badem::account pending_account;
	boost::optional<bool> state_is_send;
	badem::signature_verification verified;
};
enum class tally_result
{
	vote,
	changed,
	confirm
};

class genesis final
{
public:
	genesis ();
	badem::block_hash hash () const;
	std::shared_ptr<badem::block> open;
};

class network_params;

/** Protocol versions whose value may depend on the active network */
class protocol_constants
{
public:
	protocol_constants (badem::badem_networks network_a);

	/** Current protocol version */
	uint8_t protocol_version = 0x11;

	/** Minimum accepted protocol version */
	uint8_t protocol_version_min = 0x10;

	/** Do not bootstrap from nodes older than this version. */
	uint8_t protocol_version_bootstrap_min = 0x10;

	/** Do not lazy bootstrap from nodes older than this version. */
	uint8_t protocol_version_bootstrap_lazy_min = 0x10;

	/** Do not start TCP realtime network connections to nodes older than this version */
	uint8_t tcp_realtime_protocol_version_min = 0x11;
};

/** Genesis keys and ledger constants for network variants */
class ledger_constants
{
public:
	ledger_constants (badem::network_constants & network_constants);
	ledger_constants (badem::badem_networks network_a);
	badem::keypair zero_key;
	badem::keypair test_genesis_key;
	badem::account badem_test_account;
	badem::account badem_beta_account;
	badem::account badem_live_account;
	std::string badem_test_genesis;
	std::string badem_beta_genesis;
	std::string badem_live_genesis;
	badem::account genesis_account;
	std::string genesis_block;
	badem::uint128_t genesis_amount;
	badem::account burn_account;
	badem::epochs epochs;
};

/** Constants which depend on random values (this class should never be used globally due to CryptoPP globals potentially not being initialized) */
class random_constants
{
public:
	random_constants ();
	badem::account not_an_account;
	badem::uint128_union random_128;
};

/** Node related constants whose value depends on the active network */
class node_constants
{
public:
	node_constants (badem::network_constants & network_constants);
	std::chrono::seconds period;
	std::chrono::milliseconds half_period;
	/** Default maximum idle time for a socket before it's automatically closed */
	std::chrono::seconds idle_timeout;
	std::chrono::seconds cutoff;
	std::chrono::seconds syn_cookie_cutoff;
	std::chrono::minutes backup_interval;
	std::chrono::seconds search_pending_interval;
	std::chrono::seconds peer_interval;
	std::chrono::minutes unchecked_cleaning_interval;
	std::chrono::milliseconds process_confirmed_interval;

	/** The maximum amount of samples for a 2 week period on live or 3 days on beta */
	uint64_t max_weight_samples;
	uint64_t weight_period;
};

/** Voting related constants whose value depends on the active network */
class voting_constants
{
public:
	voting_constants (badem::network_constants & network_constants);
	size_t max_cache;
};

/** Port-mapping related constants whose value depends on the active network */
class portmapping_constants
{
public:
	portmapping_constants (badem::network_constants & network_constants);
	// Timeouts are primes so they infrequently happen at the same time
	int mapping_timeout;
	int check_timeout;
};

/** Bootstrap related constants whose value depends on the active network */
class bootstrap_constants
{
public:
	bootstrap_constants (badem::network_constants & network_constants);
	uint32_t lazy_max_pull_blocks;
	uint32_t lazy_min_pull_blocks;
	unsigned frontier_retry_limit;
	unsigned lazy_retry_limit;
	unsigned lazy_destinations_retry_limit;
};

/** Constants whose value depends on the active network */
class network_params
{
public:
	/** Populate values based on the current active network */
	network_params ();

	/** Populate values based on \p network_a */
	network_params (badem::badem_networks network_a);

	std::array<uint8_t, 2> header_magic_number;
	unsigned kdf_work;
	network_constants network;
	protocol_constants protocol;
	ledger_constants ledger;
	random_constants random;
	voting_constants voting;
	node_constants node;
	portmapping_constants portmapping;
	bootstrap_constants bootstrap;
};

badem::wallet_id random_wallet_id ();
}
