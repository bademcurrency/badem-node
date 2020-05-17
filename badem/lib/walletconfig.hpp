#pragma once

#include <badem/lib/errors.hpp>
#include <badem/lib/numbers.hpp>

#include <string>

namespace badem
{
class tomlconfig;

/** Configuration options for the Qt wallet */
class wallet_config final
{
public:
	wallet_config ();
	/** Update this instance by parsing the given wallet and account */
	badem::error parse (std::string const & wallet_a, std::string const & account_a);
	badem::error serialize_toml (badem::tomlconfig & toml_a) const;
	badem::error deserialize_toml (badem::tomlconfig & toml_a);
	badem::wallet_id wallet;
	badem::account account{ 0 };
};
}
