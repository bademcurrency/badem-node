#pragma once

#include <badem/boost/asio.hpp>
#include <badem/lib/config.hpp>

#include <miniupnp/miniupnpc/miniupnpc.h>

#include <mutex>

namespace badem
{
class node;

/** Collected protocol information */
class mapping_protocol
{
public:
	/** Protocol name; TPC or UDP */
	char const * name;
	int remaining;
	boost::asio::ip::address_v4 external_address;
	uint16_t external_port;
};

/** UPnP port mapping */
class port_mapping
{
public:
	port_mapping (badem::node &);
	void start ();
	void stop ();
	void refresh_devices ();
	badem::endpoint external_address ();

private:
	/** Add port mappings for the node port (not RPC). Refresh when the lease ends. */
	void refresh_mapping ();
	/** Refresh occasionally in case router loses mapping */
	void check_mapping_loop ();
	int check_mapping ();
	std::mutex mutex;
	badem::node & node;
	/** List of all UPnP devices */
	UPNPDev * devices;
	/** UPnP collected url information */
	UPNPUrls urls;
	/** UPnP state */
	IGDdatas data;
	badem::network_params network_params;
	boost::asio::ip::address_v4 address;
	std::array<mapping_protocol, 2> protocols;
	uint64_t check_count;
	bool on;
};
}
