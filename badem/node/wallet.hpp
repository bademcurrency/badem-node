#pragma once

#include <badem/lib/config.hpp>
#include <badem/node/lmdb/lmdb.hpp>
#include <badem/node/lmdb/wallet_value.hpp>
#include <badem/node/openclwork.hpp>
#include <badem/secure/blockstore.hpp>
#include <badem/secure/common.hpp>

#include <boost/thread/thread.hpp>

#include <mutex>
#include <unordered_set>

namespace badem
{
class node;
class node_config;
class wallets;
// The fan spreads a key out over the heap to decrease the likelihood of it being recovered by memory inspection
class fan final
{
public:
	fan (badem::uint256_union const &, size_t);
	void value (badem::raw_key &);
	void value_set (badem::raw_key const &);
	std::vector<std::unique_ptr<badem::uint256_union>> values;

private:
	std::mutex mutex;
	void value_get (badem::raw_key &);
};
class kdf final
{
public:
	void phs (badem::raw_key &, std::string const &, badem::uint256_union const &);
	std::mutex mutex;
};
enum class key_type
{
	not_a_type,
	unknown,
	adhoc,
	deterministic
};
class wallet_store final
{
public:
	wallet_store (bool &, badem::kdf &, badem::transaction &, badem::account, unsigned, std::string const &);
	wallet_store (bool &, badem::kdf &, badem::transaction &, badem::account, unsigned, std::string const &, std::string const &);
	std::vector<badem::account> accounts (badem::transaction const &);
	void initialize (badem::transaction const &, bool &, std::string const &);
	badem::uint256_union check (badem::transaction const &);
	bool rekey (badem::transaction const &, std::string const &);
	bool valid_password (badem::transaction const &);
	bool valid_public_key (badem::public_key const &);
	bool attempt_password (badem::transaction const &, std::string const &);
	void wallet_key (badem::raw_key &, badem::transaction const &);
	void seed (badem::raw_key &, badem::transaction const &);
	void seed_set (badem::transaction const &, badem::raw_key const &);
	badem::key_type key_type (badem::wallet_value const &);
	badem::public_key deterministic_insert (badem::transaction const &);
	badem::public_key deterministic_insert (badem::transaction const &, uint32_t const);
	badem::private_key deterministic_key (badem::transaction const &, uint32_t);
	uint32_t deterministic_index_get (badem::transaction const &);
	void deterministic_index_set (badem::transaction const &, uint32_t);
	void deterministic_clear (badem::transaction const &);
	badem::uint256_union salt (badem::transaction const &);
	bool is_representative (badem::transaction const &);
	badem::account representative (badem::transaction const &);
	void representative_set (badem::transaction const &, badem::account const &);
	badem::public_key insert_adhoc (badem::transaction const &, badem::raw_key const &);
	bool insert_watch (badem::transaction const &, badem::account const &);
	void erase (badem::transaction const &, badem::account const &);
	badem::wallet_value entry_get_raw (badem::transaction const &, badem::account const &);
	void entry_put_raw (badem::transaction const &, badem::account const &, badem::wallet_value const &);
	bool fetch (badem::transaction const &, badem::account const &, badem::raw_key &);
	bool exists (badem::transaction const &, badem::account const &);
	void destroy (badem::transaction const &);
	badem::store_iterator<badem::account, badem::wallet_value> find (badem::transaction const &, badem::account const &);
	badem::store_iterator<badem::account, badem::wallet_value> begin (badem::transaction const &, badem::account const &);
	badem::store_iterator<badem::account, badem::wallet_value> begin (badem::transaction const &);
	badem::store_iterator<badem::account, badem::wallet_value> end ();
	void derive_key (badem::raw_key &, badem::transaction const &, std::string const &);
	void serialize_json (badem::transaction const &, std::string &);
	void write_backup (badem::transaction const &, boost::filesystem::path const &);
	bool move (badem::transaction const &, badem::wallet_store &, std::vector<badem::public_key> const &);
	bool import (badem::transaction const &, badem::wallet_store &);
	bool work_get (badem::transaction const &, badem::public_key const &, uint64_t &);
	void work_put (badem::transaction const &, badem::public_key const &, uint64_t);
	unsigned version (badem::transaction const &);
	void version_put (badem::transaction const &, unsigned);
	void upgrade_v1_v2 (badem::transaction const &);
	void upgrade_v2_v3 (badem::transaction const &);
	void upgrade_v3_v4 (badem::transaction const &);
	badem::fan password;
	badem::fan wallet_key_mem;
	static unsigned const version_1 = 1;
	static unsigned const version_2 = 2;
	static unsigned const version_3 = 3;
	static unsigned const version_4 = 4;
	static unsigned constexpr version_current = version_4;
	static badem::account const version_special;
	static badem::account const wallet_key_special;
	static badem::account const salt_special;
	static badem::account const check_special;
	static badem::account const representative_special;
	static badem::account const seed_special;
	static badem::account const deterministic_index_special;
	static size_t const check_iv_index;
	static size_t const seed_iv_index;
	static int const special_count;
	badem::kdf & kdf;
	MDB_dbi handle{ 0 };
	std::recursive_mutex mutex;

private:
	MDB_txn * tx (badem::transaction const &) const;
};
// A wallet is a set of account keys encrypted by a common encryption key
class wallet final : public std::enable_shared_from_this<badem::wallet>
{
public:
	std::shared_ptr<badem::block> change_action (badem::account const &, badem::account const &, uint64_t = 0, bool = true);
	std::shared_ptr<badem::block> receive_action (badem::block const &, badem::account const &, badem::uint128_union const &, uint64_t = 0, bool = true);
	std::shared_ptr<badem::block> send_action (badem::account const &, badem::account const &, badem::uint128_t const &, uint64_t = 0, bool = true, boost::optional<std::string> = {});
	bool action_complete (std::shared_ptr<badem::block> const &, badem::account const &, bool const);
	wallet (bool &, badem::transaction &, badem::wallets &, std::string const &);
	wallet (bool &, badem::transaction &, badem::wallets &, std::string const &, std::string const &);
	void enter_initial_password ();
	bool enter_password (badem::transaction const &, std::string const &);
	badem::public_key insert_adhoc (badem::raw_key const &, bool = true);
	badem::public_key insert_adhoc (badem::transaction const &, badem::raw_key const &, bool = true);
	bool insert_watch (badem::transaction const &, badem::public_key const &);
	badem::public_key deterministic_insert (badem::transaction const &, bool = true);
	badem::public_key deterministic_insert (uint32_t, bool = true);
	badem::public_key deterministic_insert (bool = true);
	bool exists (badem::public_key const &);
	bool import (std::string const &, std::string const &);
	void serialize (std::string &);
	bool change_sync (badem::account const &, badem::account const &);
	void change_async (badem::account const &, badem::account const &, std::function<void(std::shared_ptr<badem::block>)> const &, uint64_t = 0, bool = true);
	bool receive_sync (std::shared_ptr<badem::block>, badem::account const &, badem::uint128_t const &);
	void receive_async (std::shared_ptr<badem::block>, badem::account const &, badem::uint128_t const &, std::function<void(std::shared_ptr<badem::block>)> const &, uint64_t = 0, bool = true);
	badem::block_hash send_sync (badem::account const &, badem::account const &, badem::uint128_t const &);
	void send_async (badem::account const &, badem::account const &, badem::uint128_t const &, std::function<void(std::shared_ptr<badem::block>)> const &, uint64_t = 0, bool = true, boost::optional<std::string> = {});
	void work_cache_blocking (badem::account const &, badem::root const &);
	void work_update (badem::transaction const &, badem::account const &, badem::root const &, uint64_t);
	void work_ensure (badem::account const &, badem::root const &);
	bool search_pending ();
	void init_free_accounts (badem::transaction const &);
	uint32_t deterministic_check (badem::transaction const & transaction_a, uint32_t index);
	/** Changes the wallet seed and returns the first account */
	badem::public_key change_seed (badem::transaction const & transaction_a, badem::raw_key const & prv_a, uint32_t count = 0);
	void deterministic_restore (badem::transaction const & transaction_a);
	bool live ();
	badem::network_params network_params;
	std::unordered_set<badem::account> free_accounts;
	std::function<void(bool, bool)> lock_observer;
	badem::wallet_store store;
	badem::wallets & wallets;
	std::mutex representatives_mutex;
	std::unordered_set<badem::account> representatives;
};

class work_watcher final : public std::enable_shared_from_this<badem::work_watcher>
{
public:
	work_watcher (badem::node &);
	~work_watcher ();
	void stop ();
	void add (std::shared_ptr<badem::block>);
	void update (badem::qualified_root const &, std::shared_ptr<badem::state_block>);
	void watching (badem::qualified_root const &, std::shared_ptr<badem::state_block>);
	void remove (std::shared_ptr<badem::block>);
	bool is_watched (badem::qualified_root const &);
	size_t size ();
	std::mutex mutex;
	badem::node & node;
	std::unordered_map<badem::qualified_root, std::shared_ptr<badem::state_block>> watched;
	std::atomic<bool> stopped;
};
/**
 * The wallets set is all the wallets a node controls.
 * A node may contain multiple wallets independently encrypted and operated.
 */
class wallets final
{
public:
	wallets (bool, badem::node &);
	~wallets ();
	std::shared_ptr<badem::wallet> open (badem::wallet_id const &);
	std::shared_ptr<badem::wallet> create (badem::wallet_id const &);
	bool search_pending (badem::wallet_id const &);
	void search_pending_all ();
	void destroy (badem::wallet_id const &);
	void reload ();
	void do_wallet_actions ();
	void queue_wallet_action (badem::uint128_t const &, std::shared_ptr<badem::wallet>, std::function<void(badem::wallet &)> const &);
	void foreach_representative (std::function<void(badem::public_key const &, badem::raw_key const &)> const &);
	bool exists (badem::transaction const &, badem::public_key const &);
	void stop ();
	void clear_send_ids (badem::transaction const &);
	bool check_rep (badem::account const &, badem::uint128_t const &);
	void compute_reps ();
	void ongoing_compute_reps ();
	void split_if_needed (badem::transaction &, badem::block_store &);
	void move_table (std::string const &, MDB_txn *, MDB_txn *);
	badem::network_params network_params;
	std::function<void(bool)> observer;
	std::unordered_map<badem::wallet_id, std::shared_ptr<badem::wallet>> items;
	std::multimap<badem::uint128_t, std::pair<std::shared_ptr<badem::wallet>, std::function<void(badem::wallet &)>>, std::greater<badem::uint128_t>> actions;
	std::mutex mutex;
	std::mutex action_mutex;
	badem::condition_variable condition;
	badem::kdf kdf;
	MDB_dbi handle;
	MDB_dbi send_action_ids;
	badem::node & node;
	badem::mdb_env & env;
	std::atomic<bool> stopped;
	std::shared_ptr<badem::work_watcher> watcher;
	boost::thread thread;
	static badem::uint128_t const generate_priority;
	static badem::uint128_t const high_priority;
	std::atomic<uint64_t> reps_count{ 0 };
	std::atomic<uint64_t> half_principal_reps_count{ 0 }; // Representatives with at least 50% of principal representative requirements

	/** Start read-write transaction */
	badem::write_transaction tx_begin_write ();

	/** Start read-only transaction */
	badem::read_transaction tx_begin_read ();
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (wallets & wallets, const std::string & name);

class wallets_store
{
public:
	virtual ~wallets_store () = default;
	virtual bool init_error () const = 0;
};
class mdb_wallets_store final : public wallets_store
{
public:
	mdb_wallets_store (boost::filesystem::path const &, int lmdb_max_dbs = 128);
	badem::mdb_env environment;
	bool init_error () const override;
	bool error{ false };
};
}
