#include <badem/crypto_lib/random_pool.hpp>
#include <badem/lib/tomlconfig.hpp>
#include <badem/lib/walletconfig.hpp>

badem::wallet_config::wallet_config ()
{
	badem::random_pool::generate_block (wallet.bytes.data (), wallet.bytes.size ());
	assert (!wallet.is_zero ());
}

badem::error badem::wallet_config::parse (std::string const & wallet_a, std::string const & account_a)
{
	badem::error error;
	if (wallet.decode_hex (wallet_a))
	{
		error.set ("Invalid wallet id");
	}
	else if (account.decode_account (account_a))
	{
		error.set ("Invalid account format");
	}
	return error;
}

badem::error badem::wallet_config::serialize_toml (badem::tomlconfig & toml) const
{
	std::string wallet_string;
	wallet.encode_hex (wallet_string);

	toml.put ("wallet", wallet_string, "Wallet identifier\ntype:string,hex");
	toml.put ("account", account.to_account (), "Current wallet account\ntype:string,account");
	return toml.get_error ();
}

badem::error badem::wallet_config::deserialize_toml (badem::tomlconfig & toml)
{
	std::string wallet_l;
	std::string account_l;

	toml.get<std::string> ("wallet", wallet_l);
	toml.get<std::string> ("account", account_l);

	if (wallet.decode_hex (wallet_l))
	{
		toml.get_error ().set ("Invalid wallet id. Did you open a node daemon config?");
	}
	else if (account.decode_account (account_l))
	{
		toml.get_error ().set ("Invalid account");
	}

	return toml.get_error ();
}
