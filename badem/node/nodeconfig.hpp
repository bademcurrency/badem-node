#pragma once

#include <badem/lib/config.hpp>
#include <badem/lib/errors.hpp>
#include <badem/lib/jsonconfig.hpp>
#include <badem/lib/numbers.hpp>
#include <badem/lib/stats.hpp>
#include <badem/node/diagnosticsconfig.hpp>
#include <badem/node/ipcconfig.hpp>
#include <badem/node/logging.hpp>
#include <badem/node/websocketconfig.hpp>
#include <badem/secure/common.hpp>

#include <chrono>
#include <vector>

namespace badem
{
/**
 * Node configuration
 */
class node_config
{
public:
	node_config ();
	node_config (uint16_t, badem::logging const &);
	badem::error serialize_json (badem::jsonconfig &) const;
	badem::error deserialize_json (bool &, badem::jsonconfig &);
	bool upgrade_json (unsigned, badem::jsonconfig &);
	badem::account random_representative ();
	badem::network_params network_params;
	uint16_t peering_port{ 0 };
	badem::logging logging;
	std::vector<std::pair<std::string, uint16_t>> work_peers;
	std::vector<std::string> preconfigured_peers;
	std::vector<badem::account> preconfigured_representatives;
	unsigned bootstrap_fraction_numerator{ 1 };
	badem::amount receive_minimum{ badem::RAW_ratio };
	badem::amount vote_minimum{ badem::kBDM_ratio };
	std::chrono::milliseconds vote_generator_delay{ std::chrono::milliseconds (100) };
	unsigned vote_generator_threshold{ 3 };
	badem::amount online_weight_minimum{ 60000 * badem::kBDM_ratio };
	unsigned online_weight_quorum{ 50 };
	unsigned password_fanout{ 1024 };
	unsigned io_threads{ std::max<unsigned> (4, boost::thread::hardware_concurrency ()) };
	unsigned network_threads{ std::max<unsigned> (4, boost::thread::hardware_concurrency ()) };
	unsigned work_threads{ std::max<unsigned> (4, boost::thread::hardware_concurrency ()) };
	unsigned signature_checker_threads{ (boost::thread::hardware_concurrency () != 0) ? boost::thread::hardware_concurrency () - 1 : 0 }; /* The calling thread does checks as well so remove it from the number of threads used */
	bool enable_voting{ false };
	unsigned bootstrap_connections{ 4 };
	unsigned bootstrap_connections_max{ 64 };
	badem::websocket::config websocket_config;
	badem::diagnostics_config diagnostics_config;
	size_t confirmation_history_size{ 2048 };
	std::string callback_address;
	uint16_t callback_port{ 0 };
	std::string callback_target;
	int lmdb_max_dbs{ 128 };
	bool allow_local_peers{ !network_params.network.is_live_network () }; // disable by default for live network
	badem::stat_config stat_config;
	badem::ipc::ipc_config ipc_config;
	badem::uint256_union epoch_block_link;
	badem::account epoch_block_signer;
	boost::asio::ip::address_v6 external_address{ boost::asio::ip::address_v6{} };
	uint16_t external_port{ 0 };
	std::chrono::milliseconds block_processor_batch_max_time{ std::chrono::milliseconds (5000) };
	std::chrono::seconds unchecked_cutoff_time{ std::chrono::seconds (4 * 60 * 60) }; // 4 hours
	/** Timeout for initiated async operations */
	std::chrono::seconds tcp_io_timeout{ (network_params.network.is_test_network () && !is_sanitizer_build) ? std::chrono::seconds (5) : std::chrono::seconds (15) };
	std::chrono::nanoseconds pow_sleep_interval{ 0 };
	size_t active_elections_size{ 50000 };
	/** Default maximum incoming TCP connections, including realtime network & bootstrap */
	unsigned tcp_incoming_connections_max{ 1024 };
	bool use_memory_pools{ true };
	static std::chrono::seconds constexpr keepalive_period = std::chrono::seconds (60);
	static std::chrono::seconds constexpr keepalive_cutoff = keepalive_period * 5;
	static std::chrono::minutes constexpr wallet_backup_interval = std::chrono::minutes (5);
	size_t bandwidth_limit{ 5 * 1024 * 1024 }; // 5Mb/s
	std::chrono::milliseconds conf_height_processor_batch_min_time{ 50 };
	static unsigned json_version ()
	{
		return 18;
	}
};

class node_flags final
{
public:
	bool disable_backup{ false };
	bool disable_lazy_bootstrap{ false };
	bool disable_legacy_bootstrap{ false };
	bool disable_wallet_bootstrap{ false };
	bool disable_bootstrap_listener{ false };
	bool disable_tcp_realtime{ false };
	bool disable_udp{ false };
	bool disable_unchecked_cleanup{ false };
	bool disable_unchecked_drop{ true };
	bool fast_bootstrap{ false };
	bool delay_frontier_confirmation_height_updating{ false };
	bool read_only{ false };
	size_t sideband_batch_size{ 512 };
	size_t block_processor_batch_size{ 0 };
	size_t block_processor_full_size{ 65536 };
	size_t block_processor_verification_size{ 0 };
};
}
