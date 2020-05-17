#include <badem/lib/jsonconfig.hpp>
#include <badem/lib/tomlconfig.hpp>
#include <badem/node/websocketconfig.hpp>

badem::websocket::config::config () :
port (network_constants.default_websocket_port)
{
}

badem::error badem::websocket::config::serialize_toml (badem::tomlconfig & toml) const
{
	toml.put ("enable", enabled, "Enable or disable WebSocket server.\ntype:bool");
	toml.put ("address", address.to_string (), "WebSocket server bind address.\ntype:string,ip");
	toml.put ("port", port, "WebSocket server listening port.\ntype:uint16");
	return toml.get_error ();
}

badem::error badem::websocket::config::deserialize_toml (badem::tomlconfig & toml)
{
	toml.get<bool> ("enable", enabled);
	toml.get<boost::asio::ip::address_v6> ("address", address);
	toml.get<uint16_t> ("port", port);
	return toml.get_error ();
}

badem::error badem::websocket::config::serialize_json (badem::jsonconfig & json) const
{
	json.put ("enable", enabled);
	json.put ("address", address.to_string ());
	json.put ("port", port);
	return json.get_error ();
}

badem::error badem::websocket::config::deserialize_json (badem::jsonconfig & json)
{
	json.get<bool> ("enable", enabled);
	json.get_required<boost::asio::ip::address_v6> ("address", address);
	json.get<uint16_t> ("port", port);
	return json.get_error ();
}
