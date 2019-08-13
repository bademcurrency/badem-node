#include <badem/node/common.hpp>

#include <gtest/gtest.h>

TEST (message, keepalive_serialization)
{
	badem::keepalive request1;
	std::vector<uint8_t> bytes;
	{
		badem::vectorstream stream (bytes);
		request1.serialize (stream);
	}
	auto error (false);
	badem::bufferstream stream (bytes.data (), bytes.size ());
	badem::message_header header (error, stream);
	ASSERT_FALSE (error);
	badem::keepalive request2 (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (request1, request2);
}

TEST (message, keepalive_deserialize)
{
	badem::keepalive message1;
	message1.peers[0] = badem::endpoint (boost::asio::ip::address_v6::loopback (), 10000);
	std::vector<uint8_t> bytes;
	{
		badem::vectorstream stream (bytes);
		message1.serialize (stream);
	}
	badem::bufferstream stream (bytes.data (), bytes.size ());
	auto error (false);
	badem::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (badem::message_type::keepalive, header.type);
	badem::keepalive message2 (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (message1.peers, message2.peers);
}

TEST (message, publish_serialization)
{
	badem::publish publish (std::make_shared<badem::send_block> (0, 1, 2, badem::keypair ().prv, 4, 5));
	ASSERT_EQ (badem::block_type::send, publish.header.block_type ());
	std::vector<uint8_t> bytes;
	{
		badem::vectorstream stream (bytes);
		publish.header.serialize (stream);
	}
	ASSERT_EQ (8, bytes.size ());
	ASSERT_EQ (0x52, bytes[0]);
	ASSERT_EQ (0x41, bytes[1]);
	ASSERT_EQ (badem::protocol_version, bytes[2]);
	ASSERT_EQ (badem::protocol_version, bytes[3]);
	ASSERT_EQ (badem::protocol_version_min, bytes[4]);
	ASSERT_EQ (static_cast<uint8_t> (badem::message_type::publish), bytes[5]);
	ASSERT_EQ (0x00, bytes[6]); // extensions
	ASSERT_EQ (static_cast<uint8_t> (badem::block_type::send), bytes[7]);
	badem::bufferstream stream (bytes.data (), bytes.size ());
	auto error (false);
	badem::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (badem::protocol_version_min, header.version_min);
	ASSERT_EQ (badem::protocol_version, header.version_using);
	ASSERT_EQ (badem::protocol_version, header.version_max);
	ASSERT_EQ (badem::message_type::publish, header.type);
}

TEST (message, confirm_ack_serialization)
{
	badem::keypair key1;
	auto vote (std::make_shared<badem::vote> (key1.pub, key1.prv, 0, std::make_shared<badem::send_block> (0, 1, 2, key1.prv, 4, 5)));
	badem::confirm_ack con1 (vote);
	std::vector<uint8_t> bytes;
	{
		badem::vectorstream stream1 (bytes);
		con1.serialize (stream1);
	}
	badem::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error (false);
	badem::message_header header (error, stream2);
	badem::confirm_ack con2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (con1, con2);
	ASSERT_EQ (header.block_type (), badem::block_type::send);
}

TEST (message, confirm_ack_hash_serialization)
{
	std::vector<badem::block_hash> hashes;
	for (auto i (hashes.size ()); i < 12; i++)
	{
		badem::keypair key1;
		badem::keypair previous;
		badem::state_block block (key1.pub, previous.pub, key1.pub, 2, 4, key1.prv, key1.pub, 5);
		hashes.push_back (block.hash ());
	}
	badem::keypair representative1;
	auto vote (std::make_shared<badem::vote> (representative1.pub, representative1.prv, 0, hashes));
	badem::confirm_ack con1 (vote);
	std::vector<uint8_t> bytes;
	{
		badem::vectorstream stream1 (bytes);
		con1.serialize (stream1);
	}
	badem::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error (false);
	badem::message_header header (error, stream2);
	badem::confirm_ack con2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (con1, con2);
	std::vector<badem::block_hash> vote_blocks;
	for (auto block : con2.vote->blocks)
	{
		vote_blocks.push_back (boost::get<badem::block_hash> (block));
	}
	ASSERT_EQ (hashes, vote_blocks);
	// Check overflow with 12 hashes
	ASSERT_EQ (header.count_get (), hashes.size ());
	ASSERT_EQ (header.block_type (), badem::block_type::not_a_block);
}

TEST (message, confirm_req_serialization)
{
	badem::keypair key1;
	badem::keypair key2;
	auto block (std::make_shared<badem::send_block> (0, key2.pub, 200, badem::keypair ().prv, 2, 3));
	badem::confirm_req req (block);
	std::vector<uint8_t> bytes;
	{
		badem::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	badem::bufferstream stream2 (bytes.data (), bytes.size ());
	badem::message_header header (error, stream2);
	badem::confirm_req req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (*req.block, *req2.block);
}

TEST (message, confirm_req_hash_serialization)
{
	badem::keypair key1;
	badem::keypair key2;
	badem::send_block block (1, key2.pub, 200, badem::keypair ().prv, 2, 3);
	badem::confirm_req req (block.hash (), block.root ());
	std::vector<uint8_t> bytes;
	{
		badem::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	badem::bufferstream stream2 (bytes.data (), bytes.size ());
	badem::message_header header (error, stream2);
	badem::confirm_req req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (req.roots_hashes, req2.roots_hashes);
	ASSERT_EQ (header.block_type (), badem::block_type::not_a_block);
	ASSERT_EQ (header.count_get (), req.roots_hashes.size ());
}

TEST (message, confirm_req_hash_batch_serialization)
{
	badem::keypair key;
	badem::keypair representative;
	std::vector<std::pair<badem::block_hash, badem::block_hash>> roots_hashes;
	badem::state_block open (key.pub, 0, representative.pub, 2, 4, key.prv, key.pub, 5);
	roots_hashes.push_back (std::make_pair (open.hash (), open.root ()));
	for (auto i (roots_hashes.size ()); i < 7; i++)
	{
		badem::keypair key1;
		badem::keypair previous;
		badem::state_block block (key1.pub, previous.pub, representative.pub, 2, 4, key1.prv, key1.pub, 5);
		roots_hashes.push_back (std::make_pair (block.hash (), block.root ()));
	}
	roots_hashes.push_back (std::make_pair (open.hash (), open.root ()));
	badem::confirm_req req (roots_hashes);
	std::vector<uint8_t> bytes;
	{
		badem::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	badem::bufferstream stream2 (bytes.data (), bytes.size ());
	badem::message_header header (error, stream2);
	badem::confirm_req req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (req.roots_hashes, req2.roots_hashes);
	ASSERT_EQ (req.roots_hashes, roots_hashes);
	ASSERT_EQ (req2.roots_hashes, roots_hashes);
	ASSERT_EQ (header.block_type (), badem::block_type::not_a_block);
	ASSERT_EQ (header.count_get (), req.roots_hashes.size ());
}
