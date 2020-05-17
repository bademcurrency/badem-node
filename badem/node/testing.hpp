#pragma once

#include <badem/lib/errors.hpp>
#include <badem/lib/utility.hpp>
#include <badem/node/node.hpp>

#include <chrono>

namespace badem
{
/** Test-system related error codes */
enum class error_system
{
	generic = 1,
	deadline_expired
};
class system final
{
public:
	system ();
	system (uint16_t, uint16_t, badem::transport::transport_type = badem::transport::transport_type::tcp);
	~system ();
	void generate_activity (badem::node &, std::vector<badem::account> &);
	void generate_mass_activity (uint32_t, badem::node &);
	void generate_usage_traffic (uint32_t, uint32_t, size_t);
	void generate_usage_traffic (uint32_t, uint32_t);
	badem::account get_random_account (std::vector<badem::account> &);
	badem::uint128_t get_random_amount (badem::transaction const &, badem::node &, badem::account const &);
	void generate_rollback (badem::node &, std::vector<badem::account> &);
	void generate_change_known (badem::node &, std::vector<badem::account> &);
	void generate_change_unknown (badem::node &, std::vector<badem::account> &);
	void generate_receive (badem::node &);
	void generate_send_new (badem::node &, std::vector<badem::account> &);
	void generate_send_existing (badem::node &, std::vector<badem::account> &);
	std::shared_ptr<badem::wallet> wallet (size_t);
	badem::account account (badem::transaction const &, size_t);
	/**
	 * Polls, sleep if there's no work to be done (default 50ms), then check the deadline
	 * @returns 0 or badem::deadline_expired
	 */
	std::error_code poll (const std::chrono::nanoseconds & sleep_time = std::chrono::milliseconds (50));
	void stop ();
	void deadline_set (const std::chrono::duration<double, std::milli> & delta);
	std::shared_ptr<badem::node> add_node (badem::node_config const &, badem::node_flags = badem::node_flags (), badem::transport::transport_type = badem::transport::transport_type::tcp);
	boost::asio::io_context io_ctx;
	badem::alarm alarm{ io_ctx };
	std::vector<std::shared_ptr<badem::node>> nodes;
	badem::logging logging;
	badem::work_pool work{ 1 };
	std::chrono::time_point<std::chrono::steady_clock, std::chrono::duration<double>> deadline{ std::chrono::steady_clock::time_point::max () };
	double deadline_scaling_factor{ 1.0 };
};
}
REGISTER_ERROR_CODES (badem, error_system);
