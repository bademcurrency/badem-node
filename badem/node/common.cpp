#include <badem/lib/blocks.hpp>
#include <badem/lib/memory.hpp>
#include <badem/lib/work.hpp>
#include <badem/node/common.hpp>
#include <badem/node/election.hpp>
#include <badem/node/wallet.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/pool/pool_alloc.hpp>

std::bitset<16> constexpr badem::message_header::block_type_mask;
std::bitset<16> constexpr badem::message_header::count_mask;

badem::message_header::message_header (badem::message_type type_a) :
version_max (badem::protocol_version),
version_using (badem::protocol_version),
version_min (badem::protocol_version_min),
type (type_a)
{
}

badem::message_header::message_header (bool & error_a, badem::stream & stream_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void badem::message_header::serialize (badem::stream & stream_a) const
{
	static badem::network_params network_params;
	badem::write (stream_a, network_params.header_magic_number);
	badem::write (stream_a, version_max);
	badem::write (stream_a, version_using);
	badem::write (stream_a, version_min);
	badem::write (stream_a, type);
	badem::write (stream_a, static_cast<uint16_t> (extensions.to_ullong ()));
}

bool badem::message_header::deserialize (badem::stream & stream_a)
{
	auto error (false);
	try
	{
		static badem::network_params network_params;
		uint16_t extensions_l;
		std::array<uint8_t, 2> magic_number_l;
		read (stream_a, magic_number_l);
		if (magic_number_l != network_params.header_magic_number)
		{
			throw std::runtime_error ("Magic numbers do not match");
		}

		badem::read (stream_a, version_max);
		badem::read (stream_a, version_using);
		badem::read (stream_a, version_min);
		badem::read (stream_a, type);
		badem::read (stream_a, extensions_l);
		extensions = extensions_l;
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

badem::message::message (badem::message_type type_a) :
header (type_a)
{
}

badem::message::message (badem::message_header const & header_a) :
header (header_a)
{
}

badem::block_type badem::message_header::block_type () const
{
	return static_cast<badem::block_type> (((extensions & block_type_mask) >> 8).to_ullong ());
}

void badem::message_header::block_type_set (badem::block_type type_a)
{
	extensions &= ~block_type_mask;
	extensions |= std::bitset<16> (static_cast<unsigned long long> (type_a) << 8);
}

uint8_t badem::message_header::count_get () const
{
	return static_cast<uint8_t> (((extensions & count_mask) >> 12).to_ullong ());
}

void badem::message_header::count_set (uint8_t count_a)
{
	assert (count_a < 16);
	extensions &= ~count_mask;
	extensions |= std::bitset<16> (static_cast<unsigned long long> (count_a) << 12);
}

void badem::message_header::flag_set (uint8_t flag_a)
{
	// Flags from 8 are block_type & count
	assert (flag_a < 8);
	extensions.set (flag_a, true);
}

bool badem::message_header::bulk_pull_is_count_present () const
{
	auto result (false);
	if (type == badem::message_type::bulk_pull)
	{
		if (extensions.test (bulk_pull_count_present_flag))
		{
			result = true;
		}
	}
	return result;
}

bool badem::message_header::node_id_handshake_is_query () const
{
	auto result (false);
	if (type == badem::message_type::node_id_handshake)
	{
		if (extensions.test (node_id_handshake_query_flag))
		{
			result = true;
		}
	}
	return result;
}

bool badem::message_header::node_id_handshake_is_response () const
{
	auto result (false);
	if (type == badem::message_type::node_id_handshake)
	{
		if (extensions.test (node_id_handshake_response_flag))
		{
			result = true;
		}
	}
	return result;
}

size_t badem::message_header::payload_length_bytes () const
{
	switch (type)
	{
		case badem::message_type::bulk_pull:
		{
			return badem::bulk_pull::size + (bulk_pull_is_count_present () ? badem::bulk_pull::extended_parameters_size : 0);
		}
		case badem::message_type::bulk_push:
		{
			// bulk_push doesn't have a payload
			return 0;
		}
		case badem::message_type::frontier_req:
		{
			return badem::frontier_req::size;
		}
		case badem::message_type::bulk_pull_account:
		{
			return badem::bulk_pull_account::size;
		}
		case badem::message_type::keepalive:
		{
			return badem::keepalive::size;
		}
		case badem::message_type::publish:
		{
			return badem::block::size (block_type ());
		}
		case badem::message_type::confirm_ack:
		{
			return badem::confirm_ack::size (block_type (), count_get ());
		}
		case badem::message_type::confirm_req:
		{
			return badem::confirm_req::size (block_type (), count_get ());
		}
		case badem::message_type::node_id_handshake:
		{
			return badem::node_id_handshake::size (*this);
		}
		default:
		{
			assert (false);
			return 0;
		}
	}
}

// MTU - IP header - UDP header
const size_t badem::message_parser::max_safe_udp_message_size = 508;

std::string badem::message_parser::status_string ()
{
	switch (status)
	{
		case badem::message_parser::parse_status::success:
		{
			return "success";
		}
		case badem::message_parser::parse_status::insufficient_work:
		{
			return "insufficient_work";
		}
		case badem::message_parser::parse_status::invalid_header:
		{
			return "invalid_header";
		}
		case badem::message_parser::parse_status::invalid_message_type:
		{
			return "invalid_message_type";
		}
		case badem::message_parser::parse_status::invalid_keepalive_message:
		{
			return "invalid_keepalive_message";
		}
		case badem::message_parser::parse_status::invalid_publish_message:
		{
			return "invalid_publish_message";
		}
		case badem::message_parser::parse_status::invalid_confirm_req_message:
		{
			return "invalid_confirm_req_message";
		}
		case badem::message_parser::parse_status::invalid_confirm_ack_message:
		{
			return "invalid_confirm_ack_message";
		}
		case badem::message_parser::parse_status::invalid_node_id_handshake_message:
		{
			return "invalid_node_id_handshake_message";
		}
		case badem::message_parser::parse_status::outdated_version:
		{
			return "outdated_version";
		}
		case badem::message_parser::parse_status::invalid_magic:
		{
			return "invalid_magic";
		}
		case badem::message_parser::parse_status::invalid_network:
		{
			return "invalid_network";
		}
	}

	assert (false);

	return "[unknown parse_status]";
}

badem::message_parser::message_parser (badem::block_uniquer & block_uniquer_a, badem::vote_uniquer & vote_uniquer_a, badem::message_visitor & visitor_a, badem::work_pool & pool_a) :
block_uniquer (block_uniquer_a),
vote_uniquer (vote_uniquer_a),
visitor (visitor_a),
pool (pool_a),
status (parse_status::success)
{
}

void badem::message_parser::deserialize_buffer (uint8_t const * buffer_a, size_t size_a)
{
	static badem::network_constants network_constants;
	status = parse_status::success;
	auto error (false);
	if (size_a <= max_safe_udp_message_size)
	{
		// Guaranteed to be deliverable
		badem::bufferstream stream (buffer_a, size_a);
		badem::message_header header (error, stream);
		if (!error)
		{
			if (network_constants.is_beta_network () && header.version_using < badem::protocol_version_reasonable_min)
			{
				status = parse_status::outdated_version;
			}
			else if (header.version_using < badem::protocol_version_min)
			{
				status = parse_status::outdated_version;
			}
			else
			{
				switch (header.type)
				{
					case badem::message_type::keepalive:
					{
						deserialize_keepalive (stream, header);
						break;
					}
					case badem::message_type::publish:
					{
						deserialize_publish (stream, header);
						break;
					}
					case badem::message_type::confirm_req:
					{
						deserialize_confirm_req (stream, header);
						break;
					}
					case badem::message_type::confirm_ack:
					{
						deserialize_confirm_ack (stream, header);
						break;
					}
					case badem::message_type::node_id_handshake:
					{
						deserialize_node_id_handshake (stream, header);
						break;
					}
					default:
					{
						status = parse_status::invalid_message_type;
						break;
					}
				}
			}
		}
		else
		{
			status = parse_status::invalid_header;
		}
	}
}

void badem::message_parser::deserialize_keepalive (badem::stream & stream_a, badem::message_header const & header_a)
{
	auto error (false);
	badem::keepalive incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		visitor.keepalive (incoming);
	}
	else
	{
		status = parse_status::invalid_keepalive_message;
	}
}

void badem::message_parser::deserialize_publish (badem::stream & stream_a, badem::message_header const & header_a)
{
	auto error (false);
	badem::publish incoming (error, stream_a, header_a, &block_uniquer);
	if (!error && at_end (stream_a))
	{
		if (!badem::work_validate (*incoming.block))
		{
			visitor.publish (incoming);
		}
		else
		{
			status = parse_status::insufficient_work;
		}
	}
	else
	{
		status = parse_status::invalid_publish_message;
	}
}

void badem::message_parser::deserialize_confirm_req (badem::stream & stream_a, badem::message_header const & header_a)
{
	auto error (false);
	badem::confirm_req incoming (error, stream_a, header_a, &block_uniquer);
	if (!error && at_end (stream_a))
	{
		if (incoming.block == nullptr || !badem::work_validate (*incoming.block))
		{
			visitor.confirm_req (incoming);
		}
		else
		{
			status = parse_status::insufficient_work;
		}
	}
	else
	{
		status = parse_status::invalid_confirm_req_message;
	}
}

void badem::message_parser::deserialize_confirm_ack (badem::stream & stream_a, badem::message_header const & header_a)
{
	auto error (false);
	badem::confirm_ack incoming (error, stream_a, header_a, &vote_uniquer);
	if (!error && at_end (stream_a))
	{
		for (auto & vote_block : incoming.vote->blocks)
		{
			if (!vote_block.which ())
			{
				auto block (boost::get<std::shared_ptr<badem::block>> (vote_block));
				if (badem::work_validate (*block))
				{
					status = parse_status::insufficient_work;
					break;
				}
			}
		}
		if (status == parse_status::success)
		{
			visitor.confirm_ack (incoming);
		}
	}
	else
	{
		status = parse_status::invalid_confirm_ack_message;
	}
}

void badem::message_parser::deserialize_node_id_handshake (badem::stream & stream_a, badem::message_header const & header_a)
{
	bool error_l (false);
	badem::node_id_handshake incoming (error_l, stream_a, header_a);
	if (!error_l && at_end (stream_a))
	{
		visitor.node_id_handshake (incoming);
	}
	else
	{
		status = parse_status::invalid_node_id_handshake_message;
	}
}

bool badem::message_parser::at_end (badem::stream & stream_a)
{
	uint8_t junk;
	auto end (badem::try_read (stream_a, junk));
	return end;
}

badem::keepalive::keepalive () :
message (badem::message_type::keepalive)
{
	badem::endpoint endpoint (boost::asio::ip::address_v6{}, 0);
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
	{
		*i = endpoint;
	}
}

badem::keepalive::keepalive (bool & error_a, badem::stream & stream_a, badem::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void badem::keepalive::visit (badem::message_visitor & visitor_a) const
{
	visitor_a.keepalive (*this);
}

void badem::keepalive::serialize (badem::stream & stream_a) const
{
	header.serialize (stream_a);
	for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
	{
		assert (i->address ().is_v6 ());
		auto bytes (i->address ().to_v6 ().to_bytes ());
		write (stream_a, bytes);
		write (stream_a, i->port ());
	}
}

bool badem::keepalive::deserialize (badem::stream & stream_a)
{
	assert (header.type == badem::message_type::keepalive);
	auto error (false);
	for (auto i (peers.begin ()), j (peers.end ()); i != j && !error; ++i)
	{
		std::array<uint8_t, 16> address;
		uint16_t port;
		if (!try_read (stream_a, address) && !try_read (stream_a, port))
		{
			*i = badem::endpoint (boost::asio::ip::address_v6 (address), port);
		}
		else
		{
			error = true;
		}
	}
	return error;
}

bool badem::keepalive::operator== (badem::keepalive const & other_a) const
{
	return peers == other_a.peers;
}

badem::publish::publish (bool & error_a, badem::stream & stream_a, badem::message_header const & header_a, badem::block_uniquer * uniquer_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a, uniquer_a);
	}
}

badem::publish::publish (std::shared_ptr<badem::block> block_a) :
message (badem::message_type::publish),
block (block_a)
{
	header.block_type_set (block->type ());
}

void badem::publish::serialize (badem::stream & stream_a) const
{
	assert (block != nullptr);
	header.serialize (stream_a);
	block->serialize (stream_a);
}

bool badem::publish::deserialize (badem::stream & stream_a, badem::block_uniquer * uniquer_a)
{
	assert (header.type == badem::message_type::publish);
	block = badem::deserialize_block (stream_a, header.block_type (), uniquer_a);
	auto result (block == nullptr);
	return result;
}

void badem::publish::visit (badem::message_visitor & visitor_a) const
{
	visitor_a.publish (*this);
}

bool badem::publish::operator== (badem::publish const & other_a) const
{
	return *block == *other_a.block;
}

badem::confirm_req::confirm_req (bool & error_a, badem::stream & stream_a, badem::message_header const & header_a, badem::block_uniquer * uniquer_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a, uniquer_a);
	}
}

badem::confirm_req::confirm_req (std::shared_ptr<badem::block> block_a) :
message (badem::message_type::confirm_req),
block (block_a)
{
	header.block_type_set (block->type ());
}

badem::confirm_req::confirm_req (std::vector<std::pair<badem::block_hash, badem::block_hash>> const & roots_hashes_a) :
message (badem::message_type::confirm_req),
roots_hashes (roots_hashes_a)
{
	// not_a_block (1) block type for hashes + roots request
	header.block_type_set (badem::block_type::not_a_block);
	assert (roots_hashes.size () < 16);
	header.count_set (static_cast<uint8_t> (roots_hashes.size ()));
}

badem::confirm_req::confirm_req (badem::block_hash const & hash_a, badem::block_hash const & root_a) :
message (badem::message_type::confirm_req),
roots_hashes (std::vector<std::pair<badem::block_hash, badem::block_hash>> (1, std::make_pair (hash_a, root_a)))
{
	assert (!roots_hashes.empty ());
	// not_a_block (1) block type for hashes + roots request
	header.block_type_set (badem::block_type::not_a_block);
	assert (roots_hashes.size () < 16);
	header.count_set (static_cast<uint8_t> (roots_hashes.size ()));
}

void badem::confirm_req::visit (badem::message_visitor & visitor_a) const
{
	visitor_a.confirm_req (*this);
}

void badem::confirm_req::serialize (badem::stream & stream_a) const
{
	header.serialize (stream_a);
	if (header.block_type () == badem::block_type::not_a_block)
	{
		assert (!roots_hashes.empty ());
		// Write hashes & roots
		for (auto & root_hash : roots_hashes)
		{
			write (stream_a, root_hash.first);
			write (stream_a, root_hash.second);
		}
	}
	else
	{
		assert (block != nullptr);
		block->serialize (stream_a);
	}
}

bool badem::confirm_req::deserialize (badem::stream & stream_a, badem::block_uniquer * uniquer_a)
{
	bool result (false);
	assert (header.type == badem::message_type::confirm_req);
	try
	{
		if (header.block_type () == badem::block_type::not_a_block)
		{
			uint8_t count (header.count_get ());
			for (auto i (0); i != count && !result; ++i)
			{
				badem::block_hash block_hash (0);
				badem::block_hash root (0);
				read (stream_a, block_hash);
				if (!block_hash.is_zero ())
				{
					read (stream_a, root);
					if (!root.is_zero ())
					{
						roots_hashes.push_back (std::make_pair (block_hash, root));
					}
				}
			}

			result = roots_hashes.empty () || (roots_hashes.size () != count);
		}
		else
		{
			block = badem::deserialize_block (stream_a, header.block_type (), uniquer_a);
			result = block == nullptr;
		}
	}
	catch (const std::runtime_error &)
	{
		result = true;
	}

	return result;
}

bool badem::confirm_req::operator== (badem::confirm_req const & other_a) const
{
	bool equal (false);
	if (block != nullptr && other_a.block != nullptr)
	{
		equal = *block == *other_a.block;
	}
	else if (!roots_hashes.empty () && !other_a.roots_hashes.empty ())
	{
		equal = roots_hashes == other_a.roots_hashes;
	}
	return equal;
}

std::string badem::confirm_req::roots_string () const
{
	std::string result;
	for (auto & root_hash : roots_hashes)
	{
		result += root_hash.first.to_string ();
		result += ":";
		result += root_hash.second.to_string ();
		result += ", ";
	}
	return result;
}

size_t badem::confirm_req::size (badem::block_type type_a, size_t count)
{
	size_t result (0);
	if (type_a != badem::block_type::invalid && type_a != badem::block_type::not_a_block)
	{
		result = badem::block::size (type_a);
	}
	else if (type_a == badem::block_type::not_a_block)
	{
		result = count * (sizeof (badem::uint256_union) + sizeof (badem::block_hash));
	}
	return result;
}

badem::confirm_ack::confirm_ack (bool & error_a, badem::stream & stream_a, badem::message_header const & header_a, badem::vote_uniquer * uniquer_a) :
message (header_a),
vote (badem::make_shared<badem::vote> (error_a, stream_a, header.block_type ()))
{
	if (!error_a && uniquer_a)
	{
		vote = uniquer_a->unique (vote);
	}
}

badem::confirm_ack::confirm_ack (std::shared_ptr<badem::vote> vote_a) :
message (badem::message_type::confirm_ack),
vote (vote_a)
{
	assert (!vote_a->blocks.empty ());
	auto & first_vote_block (vote_a->blocks[0]);
	if (first_vote_block.which ())
	{
		header.block_type_set (badem::block_type::not_a_block);
		assert (vote_a->blocks.size () < 16);
		header.count_set (static_cast<uint8_t> (vote_a->blocks.size ()));
	}
	else
	{
		header.block_type_set (boost::get<std::shared_ptr<badem::block>> (first_vote_block)->type ());
	}
}

void badem::confirm_ack::serialize (badem::stream & stream_a) const
{
	assert (header.block_type () == badem::block_type::not_a_block || header.block_type () == badem::block_type::send || header.block_type () == badem::block_type::receive || header.block_type () == badem::block_type::open || header.block_type () == badem::block_type::change || header.block_type () == badem::block_type::state);
	header.serialize (stream_a);
	vote->serialize (stream_a, header.block_type ());
}

bool badem::confirm_ack::operator== (badem::confirm_ack const & other_a) const
{
	auto result (*vote == *other_a.vote);
	return result;
}

void badem::confirm_ack::visit (badem::message_visitor & visitor_a) const
{
	visitor_a.confirm_ack (*this);
}

size_t badem::confirm_ack::size (badem::block_type type_a, size_t count)
{
	size_t result (sizeof (badem::account) + sizeof (badem::signature) + sizeof (uint64_t));
	if (type_a != badem::block_type::invalid && type_a != badem::block_type::not_a_block)
	{
		result += badem::block::size (type_a);
	}
	else if (type_a == badem::block_type::not_a_block)
	{
		result += count * sizeof (badem::block_hash);
	}
	return result;
}

badem::frontier_req::frontier_req () :
message (badem::message_type::frontier_req)
{
}

badem::frontier_req::frontier_req (bool & error_a, badem::stream & stream_a, badem::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void badem::frontier_req::serialize (badem::stream & stream_a) const
{
	header.serialize (stream_a);
	write (stream_a, start.bytes);
	write (stream_a, age);
	write (stream_a, count);
}

bool badem::frontier_req::deserialize (badem::stream & stream_a)
{
	assert (header.type == badem::message_type::frontier_req);
	auto error (false);
	try
	{
		badem::read (stream_a, start.bytes);
		badem::read (stream_a, age);
		badem::read (stream_a, count);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void badem::frontier_req::visit (badem::message_visitor & visitor_a) const
{
	visitor_a.frontier_req (*this);
}

bool badem::frontier_req::operator== (badem::frontier_req const & other_a) const
{
	return start == other_a.start && age == other_a.age && count == other_a.count;
}

badem::bulk_pull::bulk_pull () :
message (badem::message_type::bulk_pull),
count (0)
{
}

badem::bulk_pull::bulk_pull (bool & error_a, badem::stream & stream_a, badem::message_header const & header_a) :
message (header_a),
count (0)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void badem::bulk_pull::visit (badem::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull (*this);
}

void badem::bulk_pull::serialize (badem::stream & stream_a) const
{
	/*
	 * Ensure the "count_present" flag is set if there
	 * is a limit specifed.  Additionally, do not allow
	 * the "count_present" flag with a value of 0, since
	 * that is a sentinel which we use to mean "all blocks"
	 * and that is the behavior of not having the flag set
	 * so it is wasteful to do this.
	 */
	assert ((count == 0 && !is_count_present ()) || (count != 0 && is_count_present ()));

	header.serialize (stream_a);
	write (stream_a, start);
	write (stream_a, end);

	if (is_count_present ())
	{
		std::array<uint8_t, extended_parameters_size> count_buffer{ { 0 } };
		decltype (count) count_little_endian;
		static_assert (sizeof (count_little_endian) < (count_buffer.size () - 1), "count must fit within buffer");

		count_little_endian = boost::endian::native_to_little (count);
		memcpy (count_buffer.data () + 1, &count_little_endian, sizeof (count_little_endian));

		write (stream_a, count_buffer);
	}
}

bool badem::bulk_pull::deserialize (badem::stream & stream_a)
{
	assert (header.type == badem::message_type::bulk_pull);
	auto error (false);
	try
	{
		badem::read (stream_a, start);
		badem::read (stream_a, end);

		if (is_count_present ())
		{
			std::array<uint8_t, extended_parameters_size> extended_parameters_buffers;
			static_assert (sizeof (count) < (extended_parameters_buffers.size () - 1), "count must fit within buffer");

			badem::read (stream_a, extended_parameters_buffers);
			if (extended_parameters_buffers.front () != 0)
			{
				error = true;
			}
			else
			{
				memcpy (&count, extended_parameters_buffers.data () + 1, sizeof (count));
				boost::endian::little_to_native_inplace (count);
			}
		}
		else
		{
			count = 0;
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool badem::bulk_pull::is_count_present () const
{
	return header.extensions.test (count_present_flag);
}

void badem::bulk_pull::set_count_present (bool value_a)
{
	header.extensions.set (count_present_flag, value_a);
}

badem::bulk_pull_account::bulk_pull_account () :
message (badem::message_type::bulk_pull_account)
{
}

badem::bulk_pull_account::bulk_pull_account (bool & error_a, badem::stream & stream_a, badem::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void badem::bulk_pull_account::visit (badem::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull_account (*this);
}

void badem::bulk_pull_account::serialize (badem::stream & stream_a) const
{
	header.serialize (stream_a);
	write (stream_a, account);
	write (stream_a, minimum_amount);
	write (stream_a, flags);
}

bool badem::bulk_pull_account::deserialize (badem::stream & stream_a)
{
	assert (header.type == badem::message_type::bulk_pull_account);
	auto error (false);
	try
	{
		badem::read (stream_a, account);
		badem::read (stream_a, minimum_amount);
		badem::read (stream_a, flags);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

badem::bulk_push::bulk_push () :
message (badem::message_type::bulk_push)
{
}

badem::bulk_push::bulk_push (badem::message_header const & header_a) :
message (header_a)
{
}

bool badem::bulk_push::deserialize (badem::stream & stream_a)
{
	assert (header.type == badem::message_type::bulk_push);
	return false;
}

void badem::bulk_push::serialize (badem::stream & stream_a) const
{
	header.serialize (stream_a);
}

void badem::bulk_push::visit (badem::message_visitor & visitor_a) const
{
	visitor_a.bulk_push (*this);
}

badem::node_id_handshake::node_id_handshake (bool & error_a, badem::stream & stream_a, badem::message_header const & header_a) :
message (header_a),
query (boost::none),
response (boost::none)
{
	error_a = deserialize (stream_a);
}

badem::node_id_handshake::node_id_handshake (boost::optional<badem::uint256_union> query, boost::optional<std::pair<badem::account, badem::signature>> response) :
message (badem::message_type::node_id_handshake),
query (query),
response (response)
{
	if (query)
	{
		header.flag_set (badem::message_header::node_id_handshake_query_flag);
	}
	if (response)
	{
		header.flag_set (badem::message_header::node_id_handshake_response_flag);
	}
}

void badem::node_id_handshake::serialize (badem::stream & stream_a) const
{
	header.serialize (stream_a);
	if (query)
	{
		write (stream_a, *query);
	}
	if (response)
	{
		write (stream_a, response->first);
		write (stream_a, response->second);
	}
}

bool badem::node_id_handshake::deserialize (badem::stream & stream_a)
{
	assert (header.type == badem::message_type::node_id_handshake);
	auto error (false);
	try
	{
		if (header.node_id_handshake_is_query ())
		{
			badem::uint256_union query_hash;
			read (stream_a, query_hash);
			query = query_hash;
		}

		if (header.node_id_handshake_is_response ())
		{
			badem::account response_account;
			read (stream_a, response_account);
			badem::signature response_signature;
			read (stream_a, response_signature);
			response = std::make_pair (response_account, response_signature);
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool badem::node_id_handshake::operator== (badem::node_id_handshake const & other_a) const
{
	auto result (*query == *other_a.query && *response == *other_a.response);
	return result;
}

void badem::node_id_handshake::visit (badem::message_visitor & visitor_a) const
{
	visitor_a.node_id_handshake (*this);
}

size_t badem::node_id_handshake::size () const
{
	return size (header);
}

size_t badem::node_id_handshake::size (badem::message_header const & header_a)
{
	size_t result (0);
	if (header_a.node_id_handshake_is_query ())
	{
		result = sizeof (badem::uint256_union);
	}
	if (header_a.node_id_handshake_is_response ())
	{
		result += sizeof (badem::account) + sizeof (badem::signature);
	}
	return result;
}

badem::message_visitor::~message_visitor ()
{
}

bool badem::parse_port (std::string const & string_a, uint16_t & port_a)
{
	bool result = false;
	try
	{
		port_a = boost::lexical_cast<uint16_t> (string_a);
	}
	catch (...)
	{
		result = true;
	}
	return result;
}

bool badem::parse_address_port (std::string const & string, boost::asio::ip::address & address_a, uint16_t & port_a)
{
	auto result (false);
	auto port_position (string.rfind (':'));
	if (port_position != std::string::npos && port_position > 0)
	{
		std::string port_string (string.substr (port_position + 1));
		try
		{
			uint16_t port;
			result = parse_port (port_string, port);
			if (!result)
			{
				boost::system::error_code ec;
				auto address (boost::asio::ip::address_v6::from_string (string.substr (0, port_position), ec));
				if (!ec)
				{
					address_a = address;
					port_a = port;
				}
				else
				{
					result = true;
				}
			}
			else
			{
				result = true;
			}
		}
		catch (...)
		{
			result = true;
		}
	}
	else
	{
		result = true;
	}
	return result;
}

bool badem::parse_endpoint (std::string const & string, badem::endpoint & endpoint_a)
{
	boost::asio::ip::address address;
	uint16_t port;
	auto result (parse_address_port (string, address, port));
	if (!result)
	{
		endpoint_a = badem::endpoint (address, port);
	}
	return result;
}

bool badem::parse_tcp_endpoint (std::string const & string, badem::tcp_endpoint & endpoint_a)
{
	boost::asio::ip::address address;
	uint16_t port;
	auto result (parse_address_port (string, address, port));
	if (!result)
	{
		endpoint_a = badem::tcp_endpoint (address, port);
	}
	return result;
}

badem::node_singleton_memory_pool_purge_guard::node_singleton_memory_pool_purge_guard () :
cleanup_guard ({ badem::block_memory_pool_purge, badem::purge_singleton_pool_memory<badem::vote>, badem::purge_singleton_pool_memory<badem::election> })
{
}
