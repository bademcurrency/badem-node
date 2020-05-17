#include <badem/lib/config.hpp>
#include <badem/lib/rpcconfig.hpp>
#include <badem/lib/tomlconfig.hpp>
#include <badem/node/node_pow_server_config.hpp>

badem::error badem::node_pow_server_config::serialize_toml (badem::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable or disable starting Badem PoW Server as a child process.\ntype:bool");
	toml.put ("nano_pow_server_path", pow_server_path, "Path to the nano_pow_server executable.\ntype:string,path");
	return toml.get_error ();
}

badem::error badem::node_pow_server_config::deserialize_toml (badem::tomlconfig & toml)
{
	toml.get_optional<bool> ("enable", enable);
	toml.get_optional<std::string> ("nano_pow_server_path", pow_server_path);

	return toml.get_error ();
}
