#pragma once

#include <badem/boost/asio.hpp>
#include <badem/lib/config.hpp>
#include <badem/lib/errors.hpp>

namespace badem
{
class jsonconfig;
namespace websocket
{
	/** websocket configuration */
	class config final
	{
	public:
		config ();
		badem::error deserialize_json (badem::jsonconfig & json_a);
		badem::error serialize_json (badem::jsonconfig & json) const;
		badem::network_constants network_constants;
		bool enabled{ false };
		uint16_t port;
		boost::asio::ip::address_v6 address{ boost::asio::ip::address_v6::loopback () };
	};
}
}
