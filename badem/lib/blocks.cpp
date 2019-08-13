#include <badem/crypto_lib/random_pool.hpp>
#include <badem/lib/blocks.hpp>
#include <badem/lib/memory.hpp>
#include <badem/lib/numbers.hpp>
#include <badem/lib/utility.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/pool/pool_alloc.hpp>

/** Compare blocks, first by type, then content. This is an optimization over dynamic_cast, which is very slow on some platforms. */
namespace
{
template <typename T>
bool blocks_equal (T const & first, badem::block const & second)
{
	static_assert (std::is_base_of<badem::block, T>::value, "Input parameter is not a block type");
	return (first.type () == second.type ()) && (static_cast<T const &> (second)) == first;
}

template <typename block>
std::shared_ptr<block> deserialize_block (badem::stream & stream_a)
{
	auto error (false);
	auto result = badem::make_shared<block> (error, stream_a);
	if (error)
	{
		result = nullptr;
	}

	return result;
}
}

void badem::block_memory_pool_purge ()
{
	badem::purge_singleton_pool_memory<badem::open_block> ();
	badem::purge_singleton_pool_memory<badem::state_block> ();
	badem::purge_singleton_pool_memory<badem::send_block> ();
	badem::purge_singleton_pool_memory<badem::change_block> ();
}

std::string badem::block::to_json () const
{
	std::string result;
	serialize_json (result);
	return result;
}

size_t badem::block::size (badem::block_type type_a)
{
	size_t result (0);
	switch (type_a)
	{
		case badem::block_type::invalid:
		case badem::block_type::not_a_block:
			assert (false);
			break;
		case badem::block_type::send:
			result = badem::send_block::size;
			break;
		case badem::block_type::receive:
			result = badem::receive_block::size;
			break;
		case badem::block_type::change:
			result = badem::change_block::size;
			break;
		case badem::block_type::open:
			result = badem::open_block::size;
			break;
		case badem::block_type::state:
			result = badem::state_block::size;
			break;
	}
	return result;
}

badem::block_hash badem::block::hash () const
{
	badem::uint256_union result;
	blake2b_state hash_l;
	auto status (blake2b_init (&hash_l, sizeof (result.bytes)));
	assert (status == 0);
	hash (hash_l);
	status = blake2b_final (&hash_l, result.bytes.data (), sizeof (result.bytes));
	assert (status == 0);
	return result;
}

badem::block_hash badem::block::full_hash () const
{
	badem::block_hash result;
	blake2b_state state;
	blake2b_init (&state, sizeof (result.bytes));
	blake2b_update (&state, hash ().bytes.data (), sizeof (hash ()));
	auto signature (block_signature ());
	blake2b_update (&state, signature.bytes.data (), sizeof (signature));
	auto work (block_work ());
	blake2b_update (&state, &work, sizeof (work));
	blake2b_final (&state, result.bytes.data (), sizeof (result.bytes));
	return result;
}

badem::account badem::block::representative () const
{
	return 0;
}

badem::block_hash badem::block::source () const
{
	return 0;
}

badem::block_hash badem::block::link () const
{
	return 0;
}

badem::account badem::block::account () const
{
	return 0;
}

badem::qualified_root badem::block::qualified_root () const
{
	return badem::qualified_root (previous (), root ());
}

void badem::send_block::visit (badem::block_visitor & visitor_a) const
{
	visitor_a.send_block (*this);
}

void badem::send_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t badem::send_block::block_work () const
{
	return work;
}

void badem::send_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

badem::send_hashables::send_hashables (badem::block_hash const & previous_a, badem::account const & destination_a, badem::amount const & balance_a) :
previous (previous_a),
destination (destination_a),
balance (balance_a)
{
}

badem::send_hashables::send_hashables (bool & error_a, badem::stream & stream_a)
{
	try
	{
		badem::read (stream_a, previous.bytes);
		badem::read (stream_a, destination.bytes);
		badem::read (stream_a, balance.bytes);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

badem::send_hashables::send_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto destination_l (tree_a.get<std::string> ("destination"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = destination.decode_account (destination_l);
			if (!error_a)
			{
				error_a = balance.decode_hex (balance_l);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void badem::send_hashables::hash (blake2b_state & hash_a) const
{
	auto status (blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes)));
	assert (status == 0);
	status = blake2b_update (&hash_a, destination.bytes.data (), sizeof (destination.bytes));
	assert (status == 0);
	status = blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
	assert (status == 0);
}

void badem::send_block::serialize (badem::stream & stream_a) const
{
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.destination.bytes);
	write (stream_a, hashables.balance.bytes);
	write (stream_a, signature.bytes);
	write (stream_a, work);
}

bool badem::send_block::deserialize (badem::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.previous.bytes);
		read (stream_a, hashables.destination.bytes);
		read (stream_a, hashables.balance.bytes);
		read (stream_a, signature.bytes);
		read (stream_a, work);
	}
	catch (std::exception const &)
	{
		error = true;
	}

	return error;
}

void badem::send_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

void badem::send_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "send");
	std::string previous;
	hashables.previous.encode_hex (previous);
	tree.put ("previous", previous);
	tree.put ("destination", hashables.destination.to_account ());
	std::string balance;
	hashables.balance.encode_hex (balance);
	tree.put ("balance", balance);
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", badem::to_string_hex (work));
	tree.put ("signature", signature_l);
}

bool badem::send_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "send");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto destination_l (tree_a.get<std::string> ("destination"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.previous.decode_hex (previous_l);
		if (!error)
		{
			error = hashables.destination.decode_account (destination_l);
			if (!error)
			{
				error = hashables.balance.decode_hex (balance_l);
				if (!error)
				{
					error = badem::from_string_hex (work_l, work);
					if (!error)
					{
						error = signature.decode_hex (signature_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

badem::send_block::send_block (badem::block_hash const & previous_a, badem::account const & destination_a, badem::amount const & balance_a, badem::raw_key const & prv_a, badem::public_key const & pub_a, uint64_t work_a) :
hashables (previous_a, destination_a, balance_a),
signature (badem::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

badem::send_block::send_block (bool & error_a, badem::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			badem::read (stream_a, signature.bytes);
			badem::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

badem::send_block::send_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = signature.decode_hex (signature_l);
			if (!error_a)
			{
				error_a = badem::from_string_hex (work_l, work);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

bool badem::send_block::operator== (badem::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool badem::send_block::valid_predecessor (badem::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case badem::block_type::send:
		case badem::block_type::receive:
		case badem::block_type::open:
		case badem::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

badem::block_type badem::send_block::type () const
{
	return badem::block_type::send;
}

bool badem::send_block::operator== (badem::send_block const & other_a) const
{
	auto result (hashables.destination == other_a.hashables.destination && hashables.previous == other_a.hashables.previous && hashables.balance == other_a.hashables.balance && work == other_a.work && signature == other_a.signature);
	return result;
}

badem::block_hash badem::send_block::previous () const
{
	return hashables.previous;
}

badem::block_hash badem::send_block::root () const
{
	return hashables.previous;
}

badem::signature badem::send_block::block_signature () const
{
	return signature;
}

void badem::send_block::signature_set (badem::uint512_union const & signature_a)
{
	signature = signature_a;
}

badem::open_hashables::open_hashables (badem::block_hash const & source_a, badem::account const & representative_a, badem::account const & account_a) :
source (source_a),
representative (representative_a),
account (account_a)
{
}

badem::open_hashables::open_hashables (bool & error_a, badem::stream & stream_a)
{
	try
	{
		badem::read (stream_a, source.bytes);
		badem::read (stream_a, representative.bytes);
		badem::read (stream_a, account.bytes);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

badem::open_hashables::open_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto source_l (tree_a.get<std::string> ("source"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto account_l (tree_a.get<std::string> ("account"));
		error_a = source.decode_hex (source_l);
		if (!error_a)
		{
			error_a = representative.decode_account (representative_l);
			if (!error_a)
			{
				error_a = account.decode_account (account_l);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void badem::open_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, source.bytes.data (), sizeof (source.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
	blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
}

badem::open_block::open_block (badem::block_hash const & source_a, badem::account const & representative_a, badem::account const & account_a, badem::raw_key const & prv_a, badem::public_key const & pub_a, uint64_t work_a) :
hashables (source_a, representative_a, account_a),
signature (badem::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
	assert (!representative_a.is_zero ());
}

badem::open_block::open_block (badem::block_hash const & source_a, badem::account const & representative_a, badem::account const & account_a, std::nullptr_t) :
hashables (source_a, representative_a, account_a),
work (0)
{
	signature.clear ();
}

badem::open_block::open_block (bool & error_a, badem::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			badem::read (stream_a, signature);
			badem::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

badem::open_block::open_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto work_l (tree_a.get<std::string> ("work"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			error_a = badem::from_string_hex (work_l, work);
			if (!error_a)
			{
				error_a = signature.decode_hex (signature_l);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void badem::open_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t badem::open_block::block_work () const
{
	return work;
}

void badem::open_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

badem::block_hash badem::open_block::previous () const
{
	badem::block_hash result (0);
	return result;
}

badem::account badem::open_block::account () const
{
	return hashables.account;
}

void badem::open_block::serialize (badem::stream & stream_a) const
{
	write (stream_a, hashables.source);
	write (stream_a, hashables.representative);
	write (stream_a, hashables.account);
	write (stream_a, signature);
	write (stream_a, work);
}

bool badem::open_block::deserialize (badem::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.source);
		read (stream_a, hashables.representative);
		read (stream_a, hashables.account);
		read (stream_a, signature);
		read (stream_a, work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void badem::open_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

void badem::open_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "open");
	tree.put ("source", hashables.source.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("account", hashables.account.to_account ());
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", badem::to_string_hex (work));
	tree.put ("signature", signature_l);
}

bool badem::open_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "open");
		auto source_l (tree_a.get<std::string> ("source"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto account_l (tree_a.get<std::string> ("account"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.source.decode_hex (source_l);
		if (!error)
		{
			error = hashables.representative.decode_hex (representative_l);
			if (!error)
			{
				error = hashables.account.decode_hex (account_l);
				if (!error)
				{
					error = badem::from_string_hex (work_l, work);
					if (!error)
					{
						error = signature.decode_hex (signature_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void badem::open_block::visit (badem::block_visitor & visitor_a) const
{
	visitor_a.open_block (*this);
}

badem::block_type badem::open_block::type () const
{
	return badem::block_type::open;
}

bool badem::open_block::operator== (badem::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool badem::open_block::operator== (badem::open_block const & other_a) const
{
	return hashables.source == other_a.hashables.source && hashables.representative == other_a.hashables.representative && hashables.account == other_a.hashables.account && work == other_a.work && signature == other_a.signature;
}

bool badem::open_block::valid_predecessor (badem::block const & block_a) const
{
	return false;
}

badem::block_hash badem::open_block::source () const
{
	return hashables.source;
}

badem::block_hash badem::open_block::root () const
{
	return hashables.account;
}

badem::account badem::open_block::representative () const
{
	return hashables.representative;
}

badem::signature badem::open_block::block_signature () const
{
	return signature;
}

void badem::open_block::signature_set (badem::uint512_union const & signature_a)
{
	signature = signature_a;
}

badem::change_hashables::change_hashables (badem::block_hash const & previous_a, badem::account const & representative_a) :
previous (previous_a),
representative (representative_a)
{
}

badem::change_hashables::change_hashables (bool & error_a, badem::stream & stream_a)
{
	try
	{
		badem::read (stream_a, previous);
		badem::read (stream_a, representative);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

badem::change_hashables::change_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = representative.decode_account (representative_l);
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void badem::change_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
}

badem::change_block::change_block (badem::block_hash const & previous_a, badem::account const & representative_a, badem::raw_key const & prv_a, badem::public_key const & pub_a, uint64_t work_a) :
hashables (previous_a, representative_a),
signature (badem::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

badem::change_block::change_block (bool & error_a, badem::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			badem::read (stream_a, signature);
			badem::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

badem::change_block::change_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto work_l (tree_a.get<std::string> ("work"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			error_a = badem::from_string_hex (work_l, work);
			if (!error_a)
			{
				error_a = signature.decode_hex (signature_l);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void badem::change_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t badem::change_block::block_work () const
{
	return work;
}

void badem::change_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

badem::block_hash badem::change_block::previous () const
{
	return hashables.previous;
}

void badem::change_block::serialize (badem::stream & stream_a) const
{
	write (stream_a, hashables.previous);
	write (stream_a, hashables.representative);
	write (stream_a, signature);
	write (stream_a, work);
}

bool badem::change_block::deserialize (badem::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.previous);
		read (stream_a, hashables.representative);
		read (stream_a, signature);
		read (stream_a, work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void badem::change_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

void badem::change_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "change");
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("work", badem::to_string_hex (work));
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("signature", signature_l);
}

bool badem::change_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "change");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.previous.decode_hex (previous_l);
		if (!error)
		{
			error = hashables.representative.decode_hex (representative_l);
			if (!error)
			{
				error = badem::from_string_hex (work_l, work);
				if (!error)
				{
					error = signature.decode_hex (signature_l);
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void badem::change_block::visit (badem::block_visitor & visitor_a) const
{
	visitor_a.change_block (*this);
}

badem::block_type badem::change_block::type () const
{
	return badem::block_type::change;
}

bool badem::change_block::operator== (badem::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool badem::change_block::operator== (badem::change_block const & other_a) const
{
	return hashables.previous == other_a.hashables.previous && hashables.representative == other_a.hashables.representative && work == other_a.work && signature == other_a.signature;
}

bool badem::change_block::valid_predecessor (badem::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case badem::block_type::send:
		case badem::block_type::receive:
		case badem::block_type::open:
		case badem::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

badem::block_hash badem::change_block::root () const
{
	return hashables.previous;
}

badem::account badem::change_block::representative () const
{
	return hashables.representative;
}

badem::signature badem::change_block::block_signature () const
{
	return signature;
}

void badem::change_block::signature_set (badem::uint512_union const & signature_a)
{
	signature = signature_a;
}

badem::state_hashables::state_hashables (badem::account const & account_a, badem::block_hash const & previous_a, badem::account const & representative_a, badem::amount const & balance_a, badem::uint256_union const & link_a) :
account (account_a),
previous (previous_a),
representative (representative_a),
balance (balance_a),
link (link_a)
{
}

badem::state_hashables::state_hashables (bool & error_a, badem::stream & stream_a)
{
	try
	{
		badem::read (stream_a, account);
		badem::read (stream_a, previous);
		badem::read (stream_a, representative);
		badem::read (stream_a, balance);
		badem::read (stream_a, link);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

badem::state_hashables::state_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto account_l (tree_a.get<std::string> ("account"));
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto link_l (tree_a.get<std::string> ("link"));
		error_a = account.decode_account (account_l);
		if (!error_a)
		{
			error_a = previous.decode_hex (previous_l);
			if (!error_a)
			{
				error_a = representative.decode_account (representative_l);
				if (!error_a)
				{
					error_a = balance.decode_dec (balance_l);
					if (!error_a)
					{
						error_a = link.decode_account (link_l) && link.decode_hex (link_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void badem::state_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
	blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
	blake2b_update (&hash_a, link.bytes.data (), sizeof (link.bytes));
}

badem::state_block::state_block (badem::account const & account_a, badem::block_hash const & previous_a, badem::account const & representative_a, badem::amount const & balance_a, badem::uint256_union const & link_a, badem::raw_key const & prv_a, badem::public_key const & pub_a, uint64_t work_a) :
hashables (account_a, previous_a, representative_a, balance_a, link_a),
signature (badem::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

badem::state_block::state_block (bool & error_a, badem::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			badem::read (stream_a, signature);
			badem::read (stream_a, work);
			boost::endian::big_to_native_inplace (work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

badem::state_block::state_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto type_l (tree_a.get<std::string> ("type"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = type_l != "state";
			if (!error_a)
			{
				error_a = badem::from_string_hex (work_l, work);
				if (!error_a)
				{
					error_a = signature.decode_hex (signature_l);
				}
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void badem::state_block::hash (blake2b_state & hash_a) const
{
	badem::uint256_union preamble (static_cast<uint64_t> (badem::block_type::state));
	blake2b_update (&hash_a, preamble.bytes.data (), preamble.bytes.size ());
	hashables.hash (hash_a);
}

uint64_t badem::state_block::block_work () const
{
	return work;
}

void badem::state_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

badem::block_hash badem::state_block::previous () const
{
	return hashables.previous;
}

badem::account badem::state_block::account () const
{
	return hashables.account;
}

void badem::state_block::serialize (badem::stream & stream_a) const
{
	write (stream_a, hashables.account);
	write (stream_a, hashables.previous);
	write (stream_a, hashables.representative);
	write (stream_a, hashables.balance);
	write (stream_a, hashables.link);
	write (stream_a, signature);
	write (stream_a, boost::endian::native_to_big (work));
}

bool badem::state_block::deserialize (badem::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.account);
		read (stream_a, hashables.previous);
		read (stream_a, hashables.representative);
		read (stream_a, hashables.balance);
		read (stream_a, hashables.link);
		read (stream_a, signature);
		read (stream_a, work);
		boost::endian::big_to_native_inplace (work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void badem::state_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

void badem::state_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "state");
	tree.put ("account", hashables.account.to_account ());
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("balance", hashables.balance.to_string_dec ());
	tree.put ("link", hashables.link.to_string ());
	tree.put ("link_as_account", hashables.link.to_account ());
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("signature", signature_l);
	tree.put ("work", badem::to_string_hex (work));
}

bool badem::state_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "state");
		auto account_l (tree_a.get<std::string> ("account"));
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto link_l (tree_a.get<std::string> ("link"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.account.decode_account (account_l);
		if (!error)
		{
			error = hashables.previous.decode_hex (previous_l);
			if (!error)
			{
				error = hashables.representative.decode_account (representative_l);
				if (!error)
				{
					error = hashables.balance.decode_dec (balance_l);
					if (!error)
					{
						error = hashables.link.decode_account (link_l) && hashables.link.decode_hex (link_l);
						if (!error)
						{
							error = badem::from_string_hex (work_l, work);
							if (!error)
							{
								error = signature.decode_hex (signature_l);
							}
						}
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void badem::state_block::visit (badem::block_visitor & visitor_a) const
{
	visitor_a.state_block (*this);
}

badem::block_type badem::state_block::type () const
{
	return badem::block_type::state;
}

bool badem::state_block::operator== (badem::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool badem::state_block::operator== (badem::state_block const & other_a) const
{
	return hashables.account == other_a.hashables.account && hashables.previous == other_a.hashables.previous && hashables.representative == other_a.hashables.representative && hashables.balance == other_a.hashables.balance && hashables.link == other_a.hashables.link && signature == other_a.signature && work == other_a.work;
}

bool badem::state_block::valid_predecessor (badem::block const & block_a) const
{
	return true;
}

badem::block_hash badem::state_block::root () const
{
	return !hashables.previous.is_zero () ? hashables.previous : hashables.account;
}

badem::block_hash badem::state_block::link () const
{
	return hashables.link;
}

badem::account badem::state_block::representative () const
{
	return hashables.representative;
}

badem::signature badem::state_block::block_signature () const
{
	return signature;
}

void badem::state_block::signature_set (badem::uint512_union const & signature_a)
{
	signature = signature_a;
}

std::shared_ptr<badem::block> badem::deserialize_block_json (boost::property_tree::ptree const & tree_a, badem::block_uniquer * uniquer_a)
{
	std::shared_ptr<badem::block> result;
	try
	{
		auto type (tree_a.get<std::string> ("type"));
		if (type == "receive")
		{
			bool error (false);
			std::unique_ptr<badem::receive_block> obj (new badem::receive_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
		else if (type == "send")
		{
			bool error (false);
			std::unique_ptr<badem::send_block> obj (new badem::send_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
		else if (type == "open")
		{
			bool error (false);
			std::unique_ptr<badem::open_block> obj (new badem::open_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
		else if (type == "change")
		{
			bool error (false);
			std::unique_ptr<badem::change_block> obj (new badem::change_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
		else if (type == "state")
		{
			bool error (false);
			std::unique_ptr<badem::state_block> obj (new badem::state_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
	}
	catch (std::runtime_error const &)
	{
	}
	if (uniquer_a != nullptr)
	{
		result = uniquer_a->unique (result);
	}
	return result;
}

std::shared_ptr<badem::block> badem::deserialize_block (badem::stream & stream_a)
{
	badem::block_type type;
	auto error (try_read (stream_a, type));
	std::shared_ptr<badem::block> result;
	if (!error)
	{
		result = badem::deserialize_block (stream_a, type);
	}
	return result;
}

std::shared_ptr<badem::block> badem::deserialize_block (badem::stream & stream_a, badem::block_type type_a, badem::block_uniquer * uniquer_a)
{
	std::shared_ptr<badem::block> result;
	switch (type_a)
	{
		case badem::block_type::receive:
		{
			result = ::deserialize_block<badem::receive_block> (stream_a);
			break;
		}
		case badem::block_type::send:
		{
			result = ::deserialize_block<badem::send_block> (stream_a);
			break;
		}
		case badem::block_type::open:
		{
			result = ::deserialize_block<badem::open_block> (stream_a);
			break;
		}
		case badem::block_type::change:
		{
			result = ::deserialize_block<badem::change_block> (stream_a);
			break;
		}
		case badem::block_type::state:
		{
			result = ::deserialize_block<badem::state_block> (stream_a);
			break;
		}
		default:
			assert (false);
			break;
	}
	if (uniquer_a != nullptr)
	{
		result = uniquer_a->unique (result);
	}
	return result;
}

void badem::receive_block::visit (badem::block_visitor & visitor_a) const
{
	visitor_a.receive_block (*this);
}

bool badem::receive_block::operator== (badem::receive_block const & other_a) const
{
	auto result (hashables.previous == other_a.hashables.previous && hashables.source == other_a.hashables.source && work == other_a.work && signature == other_a.signature);
	return result;
}

void badem::receive_block::serialize (badem::stream & stream_a) const
{
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.source.bytes);
	write (stream_a, signature.bytes);
	write (stream_a, work);
}

bool badem::receive_block::deserialize (badem::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.previous.bytes);
		read (stream_a, hashables.source.bytes);
		read (stream_a, signature.bytes);
		read (stream_a, work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void badem::receive_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

void badem::receive_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "receive");
	std::string previous;
	hashables.previous.encode_hex (previous);
	tree.put ("previous", previous);
	std::string source;
	hashables.source.encode_hex (source);
	tree.put ("source", source);
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", badem::to_string_hex (work));
	tree.put ("signature", signature_l);
}

bool badem::receive_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "receive");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto source_l (tree_a.get<std::string> ("source"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.previous.decode_hex (previous_l);
		if (!error)
		{
			error = hashables.source.decode_hex (source_l);
			if (!error)
			{
				error = badem::from_string_hex (work_l, work);
				if (!error)
				{
					error = signature.decode_hex (signature_l);
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

badem::receive_block::receive_block (badem::block_hash const & previous_a, badem::block_hash const & source_a, badem::raw_key const & prv_a, badem::public_key const & pub_a, uint64_t work_a) :
hashables (previous_a, source_a),
signature (badem::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

badem::receive_block::receive_block (bool & error_a, badem::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			badem::read (stream_a, signature);
			badem::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

badem::receive_block::receive_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = signature.decode_hex (signature_l);
			if (!error_a)
			{
				error_a = badem::from_string_hex (work_l, work);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void badem::receive_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t badem::receive_block::block_work () const
{
	return work;
}

void badem::receive_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

bool badem::receive_block::operator== (badem::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool badem::receive_block::valid_predecessor (badem::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case badem::block_type::send:
		case badem::block_type::receive:
		case badem::block_type::open:
		case badem::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

badem::block_hash badem::receive_block::previous () const
{
	return hashables.previous;
}

badem::block_hash badem::receive_block::source () const
{
	return hashables.source;
}

badem::block_hash badem::receive_block::root () const
{
	return hashables.previous;
}

badem::signature badem::receive_block::block_signature () const
{
	return signature;
}

void badem::receive_block::signature_set (badem::uint512_union const & signature_a)
{
	signature = signature_a;
}

badem::block_type badem::receive_block::type () const
{
	return badem::block_type::receive;
}

badem::receive_hashables::receive_hashables (badem::block_hash const & previous_a, badem::block_hash const & source_a) :
previous (previous_a),
source (source_a)
{
}

badem::receive_hashables::receive_hashables (bool & error_a, badem::stream & stream_a)
{
	try
	{
		badem::read (stream_a, previous.bytes);
		badem::read (stream_a, source.bytes);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

badem::receive_hashables::receive_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto source_l (tree_a.get<std::string> ("source"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = source.decode_hex (source_l);
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void badem::receive_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, source.bytes.data (), sizeof (source.bytes));
}

std::shared_ptr<badem::block> badem::block_uniquer::unique (std::shared_ptr<badem::block> block_a)
{
	auto result (block_a);
	if (result != nullptr)
	{
		badem::uint256_union key (block_a->full_hash ());
		std::lock_guard<std::mutex> lock (mutex);
		auto & existing (blocks[key]);
		if (auto block_l = existing.lock ())
		{
			result = block_l;
		}
		else
		{
			existing = block_a;
		}
		release_assert (std::numeric_limits<CryptoPP::word32>::max () > blocks.size ());
		for (auto i (0); i < cleanup_count && !blocks.empty (); ++i)
		{
			auto random_offset (badem::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (blocks.size () - 1)));
			auto existing (std::next (blocks.begin (), random_offset));
			if (existing == blocks.end ())
			{
				existing = blocks.begin ();
			}
			if (existing != blocks.end ())
			{
				if (auto block_l = existing->second.lock ())
				{
					// Still live
				}
				else
				{
					blocks.erase (existing);
				}
			}
		}
	}
	return result;
}

size_t badem::block_uniquer::size ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return blocks.size ();
}

namespace badem
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_uniquer & block_uniquer, const std::string & name)
{
	auto count = block_uniquer.size ();
	auto sizeof_element = sizeof (block_uniquer::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "blocks", count, sizeof_element }));
	return composite;
}
}
