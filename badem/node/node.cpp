#include <badem/crypto_lib/random_pool.hpp>
#include <badem/lib/timer.hpp>
#include <badem/lib/utility.hpp>
#include <badem/node/common.hpp>
#include <badem/node/node.hpp>
#include <badem/rpc/rpc.hpp>

#if BADEM_ROCKSDB
#include <badem/node/rocksdb/rocksdb.hpp>
#endif

#include <boost/polymorphic_cast.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <algorithm>
#include <cstdlib>
#include <future>
#include <numeric>
#include <sstream>

double constexpr badem::node::price_max;
double constexpr badem::node::free_cutoff;
size_t constexpr badem::block_arrival::arrival_size_min;
std::chrono::seconds constexpr badem::block_arrival::arrival_time_min;

namespace badem
{
extern unsigned char badem_bootstrap_weights_live[];
extern size_t badem_bootstrap_weights_live_size;
extern unsigned char badem_bootstrap_weights_beta[];
extern size_t badem_bootstrap_weights_beta_size;
}

void badem::node::keepalive (std::string const & address_a, uint16_t port_a)
{
	auto node_l (shared_from_this ());
	network.resolver.async_resolve (boost::asio::ip::udp::resolver::query (address_a, std::to_string (port_a)), [node_l, address_a, port_a](boost::system::error_code const & ec, boost::asio::ip::udp::resolver::iterator i_a) {
		if (!ec)
		{
			for (auto i (i_a), n (boost::asio::ip::udp::resolver::iterator{}); i != n; ++i)
			{
				auto endpoint (badem::transport::map_endpoint_to_v6 (i->endpoint ()));
				std::weak_ptr<badem::node> node_w (node_l);
				auto channel (node_l->network.find_channel (endpoint));
				if (!channel)
				{
					node_l->network.tcp_channels.start_tcp (endpoint, [node_w](std::shared_ptr<badem::transport::channel> channel_a) {
						if (auto node_l = node_w.lock ())
						{
							node_l->network.send_keepalive (channel_a);
						}
					});
				}
				else
				{
					node_l->network.send_keepalive (channel);
				}
			}
		}
		else
		{
			node_l->logger.try_log (boost::str (boost::format ("Error resolving address: %1%:%2%: %3%") % address_a % port_a % ec.message ()));
		}
	});
}

namespace badem
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (rep_crawler & rep_crawler, const std::string & name)
{
	size_t count = 0;
	{
		badem::lock_guard<std::mutex> guard (rep_crawler.active_mutex);
		count = rep_crawler.active.size ();
	}

	auto sizeof_element = sizeof (decltype (rep_crawler.active)::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "active", count, sizeof_element }));
	return composite;
}

std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_processor & block_processor, const std::string & name)
{
	size_t state_blocks_count = 0;
	size_t blocks_count = 0;
	size_t blocks_filter_count = 0;
	size_t forced_count = 0;
	size_t rolled_back_count = 0;

	{
		badem::lock_guard<std::mutex> guard (block_processor.mutex);
		state_blocks_count = block_processor.state_blocks.size ();
		blocks_count = block_processor.blocks.size ();
		blocks_filter_count = block_processor.blocks_filter.size ();
		forced_count = block_processor.forced.size ();
		rolled_back_count = block_processor.rolled_back.size ();
	}

	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "state_blocks", state_blocks_count, sizeof (decltype (block_processor.state_blocks)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "blocks", blocks_count, sizeof (decltype (block_processor.blocks)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "blocks_filter", blocks_filter_count, sizeof (decltype (block_processor.blocks_filter)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "forced", forced_count, sizeof (decltype (block_processor.forced)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "rolled_back", rolled_back_count, sizeof (decltype (block_processor.rolled_back)::value_type) }));
	composite->add_component (collect_seq_con_info (block_processor.generator, "generator"));
	return composite;
}
}

badem::node::node (boost::asio::io_context & io_ctx_a, uint16_t peering_port_a, boost::filesystem::path const & application_path_a, badem::alarm & alarm_a, badem::logging const & logging_a, badem::work_pool & work_a, badem::node_flags flags_a) :
node (io_ctx_a, application_path_a, alarm_a, badem::node_config (peering_port_a, logging_a), work_a, flags_a)
{
}

badem::node::node (boost::asio::io_context & io_ctx_a, boost::filesystem::path const & application_path_a, badem::alarm & alarm_a, badem::node_config const & config_a, badem::work_pool & work_a, badem::node_flags flags_a) :
io_ctx (io_ctx_a),
node_initialized_latch (1),
config (config_a),
stats (config.stat_config),
flags (flags_a),
alarm (alarm_a),
work (work_a),
distributed_work (*this),
logger (config_a.logging.min_time_between_log_output),
store_impl (badem::make_store (logger, application_path_a, flags.read_only, true, config_a.rocksdb_config, config_a.diagnostics_config.txn_tracking, config_a.block_processor_batch_max_time, config_a.lmdb_max_dbs, flags.sideband_batch_size, config_a.backup_before_upgrade, config_a.rocksdb_config.enable)),
store (*store_impl),
wallets_store_impl (std::make_unique<badem::mdb_wallets_store> (application_path_a / "wallets.ldb", config_a.lmdb_max_dbs)),
wallets_store (*wallets_store_impl),
gap_cache (*this),
ledger (store, stats, flags_a.cache_representative_weights_from_frontiers),
checker (config.signature_checker_threads),
network (*this, config.peering_port),
bootstrap_initiator (*this),
bootstrap (config.peering_port, *this),
application_path (application_path_a),
port_mapping (*this),
vote_processor (*this),
rep_crawler (*this),
warmed_up (0),
block_processor (*this, write_database_queue),
block_processor_thread ([this]() {
	badem::thread_role::set (badem::thread_role::name::block_processing);
	this->block_processor.process_blocks ();
}),
online_reps (*this, config.online_weight_minimum.number ()),
vote_uniquer (block_uniquer),
active (*this),
confirmation_height_processor (pending_confirmation_height, ledger, active, write_database_queue, config.conf_height_processor_batch_min_time, logger),
payment_observer_processor (observers.blocks),
wallets (wallets_store.init_error (), *this),
startup_time (std::chrono::steady_clock::now ())
{
	if (!init_error ())
	{
		if (config.websocket_config.enabled)
		{
			auto endpoint_l (badem::tcp_endpoint (config.websocket_config.address, config.websocket_config.port));
			websocket_server = std::make_shared<badem::websocket::listener> (*this, endpoint_l);
			this->websocket_server->run ();
		}

		wallets.observer = [this](bool active) {
			observers.wallet.notify (active);
		};
		network.channel_observer = [this](std::shared_ptr<badem::transport::channel> channel_a) {
			observers.endpoint.notify (channel_a);
		};
		network.disconnect_observer = [this]() {
			observers.disconnect.notify ();
		};
		if (!config.callback_address.empty ())
		{
			observers.blocks.add ([this](badem::election_status const & status_a, badem::account const & account_a, badem::amount const & amount_a, bool is_state_send_a) {
				auto block_a (status_a.winner);
				if ((status_a.type == badem::election_status_type::active_confirmed_quorum || status_a.type == badem::election_status_type::active_confirmation_height) && this->block_arrival.recent (block_a->hash ()))
				{
					auto node_l (shared_from_this ());
					background ([node_l, block_a, account_a, amount_a, is_state_send_a]() {
						boost::property_tree::ptree event;
						event.add ("account", account_a.to_account ());
						event.add ("hash", block_a->hash ().to_string ());
						std::string block_text;
						block_a->serialize_json (block_text);
						event.add ("block", block_text);
						event.add ("amount", amount_a.to_string_dec ());
						if (is_state_send_a)
						{
							event.add ("is_send", is_state_send_a);
							event.add ("subtype", "send");
						}
						// Subtype field
						else if (block_a->type () == badem::block_type::state)
						{
							if (block_a->link ().is_zero ())
							{
								event.add ("subtype", "change");
							}
							else if (amount_a == 0 && node_l->ledger.is_epoch_link (block_a->link ()))
							{
								event.add ("subtype", "epoch");
							}
							else
							{
								event.add ("subtype", "receive");
							}
						}
						std::stringstream ostream;
						boost::property_tree::write_json (ostream, event);
						ostream.flush ();
						auto body (std::make_shared<std::string> (ostream.str ()));
						auto address (node_l->config.callback_address);
						auto port (node_l->config.callback_port);
						auto target (std::make_shared<std::string> (node_l->config.callback_target));
						auto resolver (std::make_shared<boost::asio::ip::tcp::resolver> (node_l->io_ctx));
						resolver->async_resolve (boost::asio::ip::tcp::resolver::query (address, std::to_string (port)), [node_l, address, port, target, body, resolver](boost::system::error_code const & ec, boost::asio::ip::tcp::resolver::iterator i_a) {
							if (!ec)
							{
								node_l->do_rpc_callback (i_a, address, port, target, body, resolver);
							}
							else
							{
								if (node_l->config.logging.callback_logging ())
								{
									node_l->logger.always_log (boost::str (boost::format ("Error resolving callback: %1%:%2%: %3%") % address % port % ec.message ()));
								}
								node_l->stats.inc (badem::stat::type::error, badem::stat::detail::http_callback, badem::stat::dir::out);
							}
						});
					});
				}
			});
		}
		if (websocket_server)
		{
			observers.blocks.add ([this](badem::election_status const & status_a, badem::account const & account_a, badem::amount const & amount_a, bool is_state_send_a) {
				assert (status_a.type != badem::election_status_type::ongoing);

				if (this->websocket_server->any_subscriber (badem::websocket::topic::confirmation))
				{
					auto block_a (status_a.winner);
					std::string subtype;
					if (is_state_send_a)
					{
						subtype = "send";
					}
					else if (block_a->type () == badem::block_type::state)
					{
						if (block_a->link ().is_zero ())
						{
							subtype = "change";
						}
						else if (amount_a == 0 && this->ledger.is_epoch_link (block_a->link ()))
						{
							subtype = "epoch";
						}
						else
						{
							subtype = "receive";
						}
					}

					this->websocket_server->broadcast_confirmation (block_a, account_a, amount_a, subtype, status_a);
				}
			});

			observers.active_stopped.add ([this](badem::block_hash const & hash_a) {
				if (this->websocket_server->any_subscriber (badem::websocket::topic::stopped_election))
				{
					badem::websocket::message_builder builder;
					this->websocket_server->broadcast (builder.stopped_election (hash_a));
				}
			});

			observers.difficulty.add ([this](uint64_t active_difficulty) {
				if (this->websocket_server->any_subscriber (badem::websocket::topic::active_difficulty))
				{
					badem::websocket::message_builder builder;
					auto msg (builder.difficulty_changed (network_params.network.publish_threshold, active_difficulty));
					this->websocket_server->broadcast (msg);
				}
			});
		}
		// Add block confirmation type stats regardless of http-callback and websocket subscriptions
		observers.blocks.add ([this](badem::election_status const & status_a, badem::account const & account_a, badem::amount const & amount_a, bool is_state_send_a) {
			assert (status_a.type != badem::election_status_type::ongoing);
			switch (status_a.type)
			{
				case badem::election_status_type::active_confirmed_quorum:
					this->stats.inc (badem::stat::type::observer, badem::stat::detail::observer_confirmation_active_quorum, badem::stat::dir::out);
					break;
				case badem::election_status_type::active_confirmation_height:
					this->stats.inc (badem::stat::type::observer, badem::stat::detail::observer_confirmation_active_conf_height, badem::stat::dir::out);
					break;
				case badem::election_status_type::inactive_confirmation_height:
					this->stats.inc (badem::stat::type::observer, badem::stat::detail::observer_confirmation_inactive, badem::stat::dir::out);
					break;
				default:
					break;
			}
		});
		observers.endpoint.add ([this](std::shared_ptr<badem::transport::channel> channel_a) {
			if (channel_a->get_type () == badem::transport::transport_type::udp)
			{
				this->network.send_keepalive (channel_a);
			}
			else
			{
				this->network.send_keepalive_self (channel_a);
			}
		});
		observers.vote.add ([this](std::shared_ptr<badem::vote> vote_a, std::shared_ptr<badem::transport::channel> channel_a) {
			this->gap_cache.vote (vote_a);
			this->online_reps.observe (vote_a->account);
			badem::uint128_t rep_weight;
			{
				rep_weight = ledger.weight (vote_a->account);
			}
			if (rep_weight > minimum_principal_weight ())
			{
				bool rep_crawler_exists (false);
				for (auto hash : *vote_a)
				{
					if (this->rep_crawler.exists (hash))
					{
						rep_crawler_exists = true;
						break;
					}
				}
				if (rep_crawler_exists)
				{
					// We see a valid non-replay vote for a block we requested, this node is probably a representative
					if (this->rep_crawler.response (channel_a, vote_a->account, rep_weight))
					{
						logger.try_log (boost::str (boost::format ("Found a representative at %1%") % channel_a->to_string ()));
						// Rebroadcasting all active votes to new representative
						auto blocks (this->active.list_blocks (true));
						for (auto i (blocks.begin ()), n (blocks.end ()); i != n; ++i)
						{
							if (*i != nullptr)
							{
								badem::confirm_req req (*i);
								channel_a->send (req);
							}
						}
					}
				}
			}
		});
		if (websocket_server)
		{
			observers.vote.add ([this](std::shared_ptr<badem::vote> vote_a, std::shared_ptr<badem::transport::channel> channel_a) {
				if (this->websocket_server->any_subscriber (badem::websocket::topic::vote))
				{
					badem::websocket::message_builder builder;
					auto msg (builder.vote_received (vote_a));
					this->websocket_server->broadcast (msg);
				}
			});
		}
		// Cancelling local work generation
		observers.work_cancel.add ([this](badem::root const & root_a) {
			this->work.cancel (root_a);
			this->distributed_work.cancel (root_a);
		});

		logger.always_log ("Node starting, version: ", BADEM_VERSION_STRING);
		logger.always_log ("Build information: ", BUILD_INFO);

		auto network_label = network_params.network.get_current_network_as_string ();
		logger.always_log ("Active network: ", network_label);

		logger.always_log (boost::str (boost::format ("Work pool running %1% threads %2%") % work.threads.size () % (work.opencl ? "(1 for OpenCL)" : "")));
		logger.always_log (boost::str (boost::format ("%1% work peers configured") % config.work_peers.size ()));
		if (!work_generation_enabled ())
		{
			logger.always_log ("Work generation is disabled");
		}

		if (config.logging.node_lifetime_tracing ())
		{
			logger.always_log ("Constructing node");
		}

		logger.always_log (boost::str (boost::format ("Outbound Voting Bandwidth limited to %1% bytes per second") % config.bandwidth_limit));

		// First do a pass with a read to see if any writing needs doing, this saves needing to open a write lock (and potentially blocking)
		auto is_initialized (false);
		{
			auto transaction (store.tx_begin_read ());
			is_initialized = (store.latest_begin (transaction) != store.latest_end ());
		}

		badem::genesis genesis;
		if (!is_initialized)
		{
			release_assert (!flags.read_only);
			auto transaction (store.tx_begin_write ());
			// Store was empty meaning we just created it, add the genesis block
			store.initialize (transaction, genesis, ledger.rep_weights, ledger.cemented_count, ledger.block_count_cache);
		}

		if (!ledger.block_exists (genesis.hash ()))
		{
			std::stringstream ss;
			ss << "Genesis block not found. Make sure the node network ID is correct.";
			if (network_params.network.is_beta_network ())
			{
				ss << " Beta network may have reset, try clearing database files";
			}
			auto str = ss.str ();

			logger.always_log (str);
			std::cerr << str << std::endl;
			std::exit (1);
		}

		node_id = badem::keypair ();
		logger.always_log ("Node ID: ", node_id.pub.to_node_id ());

		if ((network_params.network.is_live_network () || network_params.network.is_beta_network ()) && !flags.inactive_node)
		{
			// Use bootstrap weights if initial bootstrap is not completed
			bool use_bootstrap_weight (false);
			const uint8_t * weight_buffer = network_params.network.is_live_network () ? badem_bootstrap_weights_live : badem_bootstrap_weights_beta;
			size_t weight_size = network_params.network.is_live_network () ? badem_bootstrap_weights_live_size : badem_bootstrap_weights_beta_size;
			badem::bufferstream weight_stream ((const uint8_t *)weight_buffer, weight_size);
			badem::uint128_union block_height;
			if (!badem::try_read (weight_stream, block_height))
			{
				auto max_blocks = (uint64_t)block_height.number ();
				use_bootstrap_weight = ledger.block_count_cache < max_blocks;
				if (use_bootstrap_weight)
				{
					ledger.bootstrap_weight_max_blocks = max_blocks;
					while (true)
					{
						badem::account account;
						if (badem::try_read (weight_stream, account.bytes))
						{
							break;
						}
						badem::amount weight;
						if (badem::try_read (weight_stream, weight.bytes))
						{
							break;
						}
						logger.always_log ("Using bootstrap rep weight: ", account.to_account (), " -> ", weight.format_balance (Mbdm_ratio, 0, true), " XRB");
						ledger.bootstrap_weights[account] = weight.number ();
					}
				}
			}
			// Drop unchecked blocks if initial bootstrap is completed
			if (!flags.disable_unchecked_drop && !use_bootstrap_weight && !flags.read_only)
			{
				auto transaction (store.tx_begin_write ());
				store.unchecked_clear (transaction);
				logger.always_log ("Dropping unchecked blocks");
			}
		}
	}
	node_initialized_latch.count_down ();
}

badem::node::~node ()
{
	if (config.logging.node_lifetime_tracing ())
	{
		logger.always_log ("Destructing node");
	}
	stop ();
}

void badem::node::do_rpc_callback (boost::asio::ip::tcp::resolver::iterator i_a, std::string const & address, uint16_t port, std::shared_ptr<std::string> target, std::shared_ptr<std::string> body, std::shared_ptr<boost::asio::ip::tcp::resolver> resolver)
{
	if (i_a != boost::asio::ip::tcp::resolver::iterator{})
	{
		auto node_l (shared_from_this ());
		auto sock (std::make_shared<boost::asio::ip::tcp::socket> (node_l->io_ctx));
		sock->async_connect (i_a->endpoint (), [node_l, target, body, sock, address, port, i_a, resolver](boost::system::error_code const & ec) mutable {
			if (!ec)
			{
				auto req (std::make_shared<boost::beast::http::request<boost::beast::http::string_body>> ());
				req->method (boost::beast::http::verb::post);
				req->target (*target);
				req->version (11);
				req->insert (boost::beast::http::field::host, address);
				req->insert (boost::beast::http::field::content_type, "application/json");
				req->body () = *body;
				req->prepare_payload ();
				boost::beast::http::async_write (*sock, *req, [node_l, sock, address, port, req, i_a, target, body, resolver](boost::system::error_code const & ec, size_t bytes_transferred) mutable {
					if (!ec)
					{
						auto sb (std::make_shared<boost::beast::flat_buffer> ());
						auto resp (std::make_shared<boost::beast::http::response<boost::beast::http::string_body>> ());
						boost::beast::http::async_read (*sock, *sb, *resp, [node_l, sb, resp, sock, address, port, i_a, target, body, resolver](boost::system::error_code const & ec, size_t bytes_transferred) mutable {
							if (!ec)
							{
								if (boost::beast::http::to_status_class (resp->result ()) == boost::beast::http::status_class::successful)
								{
									node_l->stats.inc (badem::stat::type::http_callback, badem::stat::detail::initiate, badem::stat::dir::out);
								}
								else
								{
									if (node_l->config.logging.callback_logging ())
									{
										node_l->logger.try_log (boost::str (boost::format ("Callback to %1%:%2% failed with status: %3%") % address % port % resp->result ()));
									}
									node_l->stats.inc (badem::stat::type::error, badem::stat::detail::http_callback, badem::stat::dir::out);
								}
							}
							else
							{
								if (node_l->config.logging.callback_logging ())
								{
									node_l->logger.try_log (boost::str (boost::format ("Unable complete callback: %1%:%2%: %3%") % address % port % ec.message ()));
								}
								node_l->stats.inc (badem::stat::type::error, badem::stat::detail::http_callback, badem::stat::dir::out);
							};
						});
					}
					else
					{
						if (node_l->config.logging.callback_logging ())
						{
							node_l->logger.try_log (boost::str (boost::format ("Unable to send callback: %1%:%2%: %3%") % address % port % ec.message ()));
						}
						node_l->stats.inc (badem::stat::type::error, badem::stat::detail::http_callback, badem::stat::dir::out);
					}
				});
			}
			else
			{
				if (node_l->config.logging.callback_logging ())
				{
					node_l->logger.try_log (boost::str (boost::format ("Unable to connect to callback address: %1%:%2%: %3%") % address % port % ec.message ()));
				}
				node_l->stats.inc (badem::stat::type::error, badem::stat::detail::http_callback, badem::stat::dir::out);
				++i_a;
				node_l->do_rpc_callback (i_a, address, port, target, body, resolver);
			}
		});
	}
}

bool badem::node::copy_with_compaction (boost::filesystem::path const & destination)
{
	return store.copy_db (destination);
}

void badem::node::process_fork (badem::transaction const & transaction_a, std::shared_ptr<badem::block> block_a)
{
	auto root (block_a->root ());
	if (!store.block_exists (transaction_a, block_a->type (), block_a->hash ()) && store.root_exists (transaction_a, block_a->root ()))
	{
		std::shared_ptr<badem::block> ledger_block (ledger.forked_block (transaction_a, *block_a));
		if (ledger_block && !block_confirmed_or_being_confirmed (transaction_a, ledger_block->hash ()))
		{
			std::weak_ptr<badem::node> this_w (shared_from_this ());
			if (!active.start (ledger_block, false, [this_w, root](std::shared_ptr<badem::block>) {
				    if (auto this_l = this_w.lock ())
				    {
					    auto attempt (this_l->bootstrap_initiator.current_attempt ());
					    if (attempt && attempt->mode == badem::bootstrap_mode::legacy)
					    {
						    auto transaction (this_l->store.tx_begin_read ());
						    auto account (this_l->ledger.store.frontier_get (transaction, root));
						    if (!account.is_zero ())
						    {
							    attempt->requeue_pull (badem::pull_info (account, root, root));
						    }
						    else if (this_l->ledger.store.account_exists (transaction, root))
						    {
							    attempt->requeue_pull (badem::pull_info (root, badem::block_hash (0), badem::block_hash (0)));
						    }
					    }
				    }
			    }))
			{
				logger.always_log (boost::str (boost::format ("Resolving fork between our block: %1% and block %2% both with root %3%") % ledger_block->hash ().to_string () % block_a->hash ().to_string () % block_a->root ().to_string ()));
				network.broadcast_confirm_req (ledger_block);
			}
		}
	}
}

namespace badem
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (node & node, const std::string & name)
{
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (collect_seq_con_info (node.alarm, "alarm"));
	composite->add_component (collect_seq_con_info (node.work, "work"));
	composite->add_component (collect_seq_con_info (node.gap_cache, "gap_cache"));
	composite->add_component (collect_seq_con_info (node.ledger, "ledger"));
	composite->add_component (collect_seq_con_info (node.active, "active"));
	composite->add_component (collect_seq_con_info (node.bootstrap_initiator, "bootstrap_initiator"));
	composite->add_component (collect_seq_con_info (node.bootstrap, "bootstrap"));
	composite->add_component (node.network.tcp_channels.collect_seq_con_info ("tcp_channels"));
	composite->add_component (node.network.udp_channels.collect_seq_con_info ("udp_channels"));
	composite->add_component (node.network.syn_cookies.collect_seq_con_info ("syn_cookies"));
	composite->add_component (collect_seq_con_info (node.observers, "observers"));
	composite->add_component (collect_seq_con_info (node.wallets, "wallets"));
	composite->add_component (collect_seq_con_info (node.vote_processor, "vote_processor"));
	composite->add_component (collect_seq_con_info (node.rep_crawler, "rep_crawler"));
	composite->add_component (collect_seq_con_info (node.block_processor, "block_processor"));
	composite->add_component (collect_seq_con_info (node.block_arrival, "block_arrival"));
	composite->add_component (collect_seq_con_info (node.online_reps, "online_reps"));
	composite->add_component (collect_seq_con_info (node.votes_cache, "votes_cache"));
	composite->add_component (collect_seq_con_info (node.block_uniquer, "block_uniquer"));
	composite->add_component (collect_seq_con_info (node.vote_uniquer, "vote_uniquer"));
	composite->add_component (collect_seq_con_info (node.confirmation_height_processor, "confirmation_height_processor"));
	composite->add_component (collect_seq_con_info (node.pending_confirmation_height, "pending_confirmation_height"));
	composite->add_component (collect_seq_con_info (node.worker, "worker"));
	composite->add_component (collect_seq_con_info (node.distributed_work, "distributed_work"));
	return composite;
}
}

void badem::node::process_active (std::shared_ptr<badem::block> incoming)
{
	block_arrival.add (incoming->hash ());
	block_processor.add (incoming, badem::seconds_since_epoch ());
}

badem::process_return badem::node::process (badem::block const & block_a)
{
	auto transaction (store.tx_begin_write ({ tables::accounts, tables::cached_counts, tables::change_blocks, tables::frontiers, tables::open_blocks, tables::pending, tables::receive_blocks, tables::representation, tables::send_blocks, tables::state_blocks }, { tables::confirmation_height }));
	auto result (ledger.process (transaction, block_a));
	return result;
}

badem::process_return badem::node::process_local (std::shared_ptr<badem::block> block_a, bool const work_watcher_a)
{
	// Add block hash as recently arrived to trigger automatic rebroadcast and election
	block_arrival.add (block_a->hash ());
	// Set current time to trigger automatic rebroadcast and election
	badem::unchecked_info info (block_a, block_a->account (), badem::seconds_since_epoch (), badem::signature_verification::unknown);
	// Notify block processor to release write lock
	block_processor.wait_write ();
	// Process block
	auto transaction (store.tx_begin_write ());
	return block_processor.process_one (transaction, info, work_watcher_a);
}

void badem::node::start ()
{
	network.start ();
	add_initial_peers ();
	if (!flags.disable_legacy_bootstrap)
	{
		ongoing_bootstrap ();
	}
	if (!flags.disable_unchecked_cleanup)
	{
		auto this_l (shared ());
		worker.push_task ([this_l]() {
			this_l->ongoing_unchecked_cleanup ();
		});
	}
	ongoing_store_flush ();
	if (!flags.disable_rep_crawler)
	{
		rep_crawler.start ();
	}
	ongoing_rep_calculation ();
	ongoing_peer_store ();
	ongoing_online_weight_calculation_queue ();
	if (config.tcp_incoming_connections_max > 0)
	{
		bootstrap.start ();
	}
	if (!flags.disable_backup)
	{
		backup_wallet ();
	}
	search_pending ();
	if (!flags.disable_wallet_bootstrap)
	{
		// Delay to start wallet lazy bootstrap
		auto this_l (shared ());
		alarm.add (std::chrono::steady_clock::now () + std::chrono::minutes (1), [this_l]() {
			this_l->bootstrap_wallet ();
		});
	}
	if (config.external_address == boost::asio::ip::address_v6{}.any ())
	{
		port_mapping.start ();
	}
}

void badem::node::stop ()
{
	if (!stopped.exchange (true))
	{
		logger.always_log ("Node stopping");
		write_database_queue.stop ();
		// Cancels ongoing work generation tasks, which may be blocking other threads
		// No tasks may wait for work generation in I/O threads, or termination signal capturing will be unable to call node::stop()
		distributed_work.stop ();
		block_processor.stop ();
		if (block_processor_thread.joinable ())
		{
			block_processor_thread.join ();
		}
		vote_processor.stop ();
		confirmation_height_processor.stop ();
		active.stop ();
		network.stop ();
		if (websocket_server)
		{
			websocket_server->stop ();
		}
		bootstrap_initiator.stop ();
		bootstrap.stop ();
		port_mapping.stop ();
		checker.stop ();
		wallets.stop ();
		stats.stop ();
		worker.stop ();
		// work pool is not stopped on purpose due to testing setup
	}
}

void badem::node::keepalive_preconfigured (std::vector<std::string> const & peers_a)
{
	for (auto i (peers_a.begin ()), n (peers_a.end ()); i != n; ++i)
	{
		keepalive (*i, network_params.network.default_node_port);
	}
}

badem::block_hash badem::node::latest (badem::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.latest (transaction, account_a);
}

badem::uint128_t badem::node::balance (badem::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.account_balance (transaction, account_a);
}

std::shared_ptr<badem::block> badem::node::block (badem::block_hash const & hash_a)
{
	auto transaction (store.tx_begin_read ());
	return store.block_get (transaction, hash_a);
}

std::pair<badem::uint128_t, badem::uint128_t> badem::node::balance_pending (badem::account const & account_a)
{
	std::pair<badem::uint128_t, badem::uint128_t> result;
	auto transaction (store.tx_begin_read ());
	result.first = ledger.account_balance (transaction, account_a);
	result.second = ledger.account_pending (transaction, account_a);
	return result;
}

badem::uint128_t badem::node::weight (badem::account const & account_a)
{
	return ledger.weight (account_a);
}

badem::block_hash badem::node::rep_block (badem::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	badem::account_info info;
	badem::block_hash result (0);
	if (!store.account_get (transaction, account_a, info))
	{
		result = ledger.representative (transaction, info.head);
	}
	return result;
}

badem::uint128_t badem::node::minimum_principal_weight ()
{
	return minimum_principal_weight (online_reps.online_stake ());
}

badem::uint128_t badem::node::minimum_principal_weight (badem::uint128_t const & online_stake)
{
	return online_stake / network_params.network.principal_weight_factor;
}

void badem::node::ongoing_rep_calculation ()
{
	auto now (std::chrono::steady_clock::now ());
	vote_processor.calculate_weights ();
	std::weak_ptr<badem::node> node_w (shared_from_this ());
	alarm.add (now + std::chrono::minutes (10), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_rep_calculation ();
		}
	});
}

void badem::node::ongoing_bootstrap ()
{
	auto next_wakeup (300);
	if (warmed_up < 3)
	{
		// Re-attempt bootstrapping more aggressively on startup
		next_wakeup = 5;
		if (!bootstrap_initiator.in_progress () && !network.empty ())
		{
			++warmed_up;
		}
	}
	bootstrap_initiator.bootstrap ();
	std::weak_ptr<badem::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (next_wakeup), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_bootstrap ();
		}
	});
}

void badem::node::ongoing_store_flush ()
{
	{
		auto transaction (store.tx_begin_write ({ tables::vote }));
		store.flush (transaction);
	}
	std::weak_ptr<badem::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (5), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->worker.push_task ([node_l]() {
				node_l->ongoing_store_flush ();
			});
		}
	});
}

void badem::node::ongoing_peer_store ()
{
	bool stored (network.tcp_channels.store_all (true));
	network.udp_channels.store_all (!stored);
	std::weak_ptr<badem::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + network_params.node.peer_interval, [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->worker.push_task ([node_l]() {
				node_l->ongoing_peer_store ();
			});
		}
	});
}

void badem::node::backup_wallet ()
{
	auto transaction (wallets.tx_begin_read ());
	for (auto i (wallets.items.begin ()), n (wallets.items.end ()); i != n; ++i)
	{
		boost::system::error_code error_chmod;
		auto backup_path (application_path / "backup");

		boost::filesystem::create_directories (backup_path);
		badem::set_secure_perm_directory (backup_path, error_chmod);
		i->second->store.write_backup (transaction, backup_path / (i->first.to_string () + ".json"));
	}
	auto this_l (shared ());
	alarm.add (std::chrono::steady_clock::now () + network_params.node.backup_interval, [this_l]() {
		this_l->backup_wallet ();
	});
}

void badem::node::search_pending ()
{
	// Reload wallets from disk
	wallets.reload ();
	// Search pending
	wallets.search_pending_all ();
	auto this_l (shared ());
	alarm.add (std::chrono::steady_clock::now () + network_params.node.search_pending_interval, [this_l]() {
		this_l->worker.push_task ([this_l]() {
			this_l->search_pending ();
		});
	});
}

void badem::node::bootstrap_wallet ()
{
	std::deque<badem::account> accounts;
	{
		badem::lock_guard<std::mutex> lock (wallets.mutex);
		auto transaction (wallets.tx_begin_read ());
		for (auto i (wallets.items.begin ()), n (wallets.items.end ()); i != n && accounts.size () < 128; ++i)
		{
			auto & wallet (*i->second);
			badem::lock_guard<std::recursive_mutex> wallet_lock (wallet.store.mutex);
			for (auto j (wallet.store.begin (transaction)), m (wallet.store.end ()); j != m && accounts.size () < 128; ++j)
			{
				badem::account account (j->first);
				accounts.push_back (account);
			}
		}
	}
	bootstrap_initiator.bootstrap_wallet (accounts);
}

void badem::node::unchecked_cleanup ()
{
	std::deque<badem::unchecked_key> cleaning_list;
	auto attempt (bootstrap_initiator.current_attempt ());
	bool long_attempt (attempt != nullptr && std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now () - attempt->attempt_start).count () > config.unchecked_cutoff_time.count ());
	// Collect old unchecked keys
	if (!flags.disable_unchecked_cleanup && ledger.block_count_cache >= ledger.bootstrap_weight_max_blocks && !long_attempt)
	{
		auto now (badem::seconds_since_epoch ());
		auto transaction (store.tx_begin_read ());
		// Max 1M records to clean, max 2 minutes reading to prevent slow i/o systems issues
		for (auto i (store.unchecked_begin (transaction)), n (store.unchecked_end ()); i != n && cleaning_list.size () < 1024 * 1024 && badem::seconds_since_epoch () - now < 120; ++i)
		{
			badem::unchecked_key const & key (i->first);
			badem::unchecked_info const & info (i->second);
			if ((now - info.modified) > static_cast<uint64_t> (config.unchecked_cutoff_time.count ()))
			{
				cleaning_list.push_back (key);
			}
		}
	}
	if (!cleaning_list.empty ())
	{
		logger.always_log (boost::str (boost::format ("Deleting %1% old unchecked blocks") % cleaning_list.size ()));
	}
	// Delete old unchecked keys in batches
	while (!cleaning_list.empty ())
	{
		size_t deleted_count (0);
		auto transaction (store.tx_begin_write ());
		while (deleted_count++ < 2 * 1024 && !cleaning_list.empty ())
		{
			auto key (cleaning_list.front ());
			cleaning_list.pop_front ();
			store.unchecked_del (transaction, key);
		}
	}
}

void badem::node::ongoing_unchecked_cleanup ()
{
	unchecked_cleanup ();
	auto this_l (shared ());
	alarm.add (std::chrono::steady_clock::now () + network_params.node.unchecked_cleaning_interval, [this_l]() {
		this_l->worker.push_task ([this_l]() {
			this_l->ongoing_unchecked_cleanup ();
		});
	});
}

int badem::node::price (badem::uint128_t const & balance_a, int amount_a)
{
	assert (balance_a >= amount_a * badem::Gbdm_ratio);
	auto balance_l (balance_a);
	double result (0.0);
	for (auto i (0); i < amount_a; ++i)
	{
		balance_l -= badem::Gbdm_ratio;
		auto balance_scaled ((balance_l / badem::Mbdm_ratio).convert_to<double> ());
		auto units (balance_scaled / 1000.0);
		auto unit_price (((free_cutoff - units) / free_cutoff) * price_max);
		result += std::min (std::max (0.0, unit_price), price_max);
	}
	return static_cast<int> (result * 100.0);
}

bool badem::node::local_work_generation_enabled () const
{
	return config.work_threads > 0 || work.opencl;
}

bool badem::node::work_generation_enabled () const
{
	return work_generation_enabled (config.work_peers);
}

bool badem::node::work_generation_enabled (std::vector<std::pair<std::string, uint16_t>> const & peers_a) const
{
	return !peers_a.empty () || local_work_generation_enabled ();
}

boost::optional<uint64_t> badem::node::work_generate_blocking (badem::block & block_a)
{
	return work_generate_blocking (block_a, network_params.network.publish_threshold);
}

boost::optional<uint64_t> badem::node::work_generate_blocking (badem::block & block_a, uint64_t difficulty_a)
{
	auto opt_work_l (work_generate_blocking (block_a.root (), difficulty_a, block_a.account ()));
	if (opt_work_l.is_initialized ())
	{
		block_a.block_work_set (*opt_work_l);
	}
	return opt_work_l;
}

void badem::node::work_generate (badem::root const & root_a, std::function<void(boost::optional<uint64_t>)> callback_a, boost::optional<badem::account> const & account_a)
{
	work_generate (root_a, callback_a, network_params.network.publish_threshold, account_a);
}

void badem::node::work_generate (badem::root const & root_a, std::function<void(boost::optional<uint64_t>)> callback_a, uint64_t difficulty_a, boost::optional<badem::account> const & account_a, bool secondary_work_peers_a)
{
	auto const & peers_l (secondary_work_peers_a ? config.secondary_work_peers : config.work_peers);
	if (distributed_work.make (root_a, peers_l, callback_a, difficulty_a, account_a))
	{
		// Error in creating the job (either stopped or work generation is not possible)
		callback_a (boost::none);
	}
}

boost::optional<uint64_t> badem::node::work_generate_blocking (badem::root const & root_a, boost::optional<badem::account> const & account_a)
{
	return work_generate_blocking (root_a, network_params.network.publish_threshold, account_a);
}

boost::optional<uint64_t> badem::node::work_generate_blocking (badem::root const & root_a, uint64_t difficulty_a, boost::optional<badem::account> const & account_a)
{
	std::promise<boost::optional<uint64_t>> promise;
	// clang-format off
	work_generate (root_a, [&promise](boost::optional<uint64_t> opt_work_a) {
		promise.set_value (opt_work_a);
	},
	difficulty_a, account_a);
	// clang-format on
	return promise.get_future ().get ();
}

void badem::node::add_initial_peers ()
{
	auto transaction (store.tx_begin_read ());
	for (auto i (store.peers_begin (transaction)), n (store.peers_end ()); i != n; ++i)
	{
		badem::endpoint endpoint (boost::asio::ip::address_v6 (i->first.address_bytes ()), i->first.port ());
		if (!network.reachout (endpoint, config.allow_local_peers))
		{
			std::weak_ptr<badem::node> node_w (shared_from_this ());
			network.tcp_channels.start_tcp (endpoint, [node_w](std::shared_ptr<badem::transport::channel> channel_a) {
				if (auto node_l = node_w.lock ())
				{
					node_l->network.send_keepalive (channel_a);
					if (!node_l->flags.disable_rep_crawler)
					{
						node_l->rep_crawler.query (channel_a);
					}
				}
			});
		}
	}
}

void badem::node::block_confirm (std::shared_ptr<badem::block> block_a)
{
	active.start (block_a, false);
	network.broadcast_confirm_req (block_a);
	// Calculate votes for local representatives
	if (config.enable_voting && active.active (*block_a))
	{
		block_processor.generator.add (block_a->hash ());
	}
}

bool badem::node::block_confirmed_or_being_confirmed (badem::transaction const & transaction_a, badem::block_hash const & hash_a)
{
	return ledger.block_confirmed (transaction_a, hash_a) || pending_confirmation_height.is_processing_block (hash_a);
}

badem::uint128_t badem::node::delta () const
{
	auto result ((online_reps.online_stake () / 100) * config.online_weight_quorum);
	return result;
}

void badem::node::ongoing_online_weight_calculation_queue ()
{
	std::weak_ptr<badem::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + (std::chrono::seconds (network_params.node.weight_period)), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->worker.push_task ([node_l]() {
				node_l->ongoing_online_weight_calculation ();
			});
		}
	});
}

bool badem::node::online () const
{
	return rep_crawler.total_weight () > (std::max (config.online_weight_minimum.number (), delta ()));
}

void badem::node::ongoing_online_weight_calculation ()
{
	online_reps.sample ();
	ongoing_online_weight_calculation_queue ();
}

namespace
{
class confirmed_visitor : public badem::block_visitor
{
public:
	confirmed_visitor (badem::transaction const & transaction_a, badem::node & node_a, std::shared_ptr<badem::block> const & block_a, badem::block_hash const & hash_a) :
	transaction (transaction_a),
	node (node_a),
	block (block_a),
	hash (hash_a)
	{
	}
	virtual ~confirmed_visitor () = default;
	void scan_receivable (badem::account const & account_a)
	{
		for (auto i (node.wallets.items.begin ()), n (node.wallets.items.end ()); i != n; ++i)
		{
			auto const & wallet (i->second);
			auto transaction_l (node.wallets.tx_begin_read ());
			if (wallet->store.exists (transaction_l, account_a))
			{
				badem::account representative;
				badem::pending_info pending;
				representative = wallet->store.representative (transaction_l);
				auto error (node.store.pending_get (transaction, badem::pending_key (account_a, hash), pending));
				if (!error)
				{
					auto node_l (node.shared ());
					auto amount (pending.amount.number ());
					wallet->receive_async (block, representative, amount, [](std::shared_ptr<badem::block>) {});
				}
				else
				{
					if (!node.store.block_exists (transaction, hash))
					{
						node.logger.try_log (boost::str (boost::format ("Confirmed block is missing:  %1%") % hash.to_string ()));
						assert (false && "Confirmed block is missing");
					}
					else
					{
						node.logger.try_log (boost::str (boost::format ("Block %1% has already been received") % hash.to_string ()));
					}
				}
			}
		}
	}
	void state_block (badem::state_block const & block_a) override
	{
		scan_receivable (block_a.hashables.link);
	}
	void send_block (badem::send_block const & block_a) override
	{
		scan_receivable (block_a.hashables.destination);
	}
	void receive_block (badem::receive_block const &) override
	{
	}
	void open_block (badem::open_block const &) override
	{
	}
	void change_block (badem::change_block const &) override
	{
	}
	badem::transaction const & transaction;
	badem::node & node;
	std::shared_ptr<badem::block> block;
	badem::block_hash const & hash;
};
}

void badem::node::receive_confirmed (badem::transaction const & transaction_a, std::shared_ptr<badem::block> block_a, badem::block_hash const & hash_a)
{
	confirmed_visitor visitor (transaction_a, *this, block_a, hash_a);
	block_a->visit (visitor);
}

void badem::node::process_confirmed_data (badem::transaction const & transaction_a, std::shared_ptr<badem::block> block_a, badem::block_hash const & hash_a, badem::block_sideband const & sideband_a, badem::account & account_a, badem::uint128_t & amount_a, bool & is_state_send_a, badem::account & pending_account_a)
{
	// Faster account calculation
	account_a = block_a->account ();
	if (account_a.is_zero ())
	{
		account_a = sideband_a.account;
	}
	// Faster amount calculation
	auto previous (block_a->previous ());
	auto previous_balance (ledger.balance (transaction_a, previous));
	auto block_balance (store.block_balance_calculated (block_a, sideband_a));
	if (hash_a != ledger.network_params.ledger.genesis_account)
	{
		amount_a = block_balance > previous_balance ? block_balance - previous_balance : previous_balance - block_balance;
	}
	else
	{
		amount_a = ledger.network_params.ledger.genesis_amount;
	}
	if (auto state = dynamic_cast<badem::state_block *> (block_a.get ()))
	{
		if (state->hashables.balance < previous_balance)
		{
			is_state_send_a = true;
		}
		pending_account_a = state->hashables.link;
	}
	if (auto send = dynamic_cast<badem::send_block *> (block_a.get ()))
	{
		pending_account_a = send->hashables.destination;
	}
}

void badem::node::process_confirmed (badem::election_status const & status_a, uint8_t iteration)
{
	if (status_a.type == badem::election_status_type::active_confirmed_quorum)
	{
		auto block_a (status_a.winner);
		auto hash (block_a->hash ());
		auto transaction (store.tx_begin_read ());
		if (store.block_get (transaction, hash) != nullptr)
		{
			confirmation_height_processor.add (hash);
		}
		// Limit to 0.5 * 20 = 10 seconds (more than max block_processor::process_batch finish time)
		else if (iteration < 20)
		{
			iteration++;
			std::weak_ptr<badem::node> node_w (shared ());
			alarm.add (std::chrono::steady_clock::now () + network_params.node.process_confirmed_interval, [node_w, status_a, iteration]() {
				if (auto node_l = node_w.lock ())
				{
					node_l->process_confirmed (status_a, iteration);
				}
			});
		}
	}
}

bool badem::block_arrival::add (badem::block_hash const & hash_a)
{
	badem::lock_guard<std::mutex> lock (mutex);
	auto now (std::chrono::steady_clock::now ());
	auto inserted (arrival.insert (badem::block_arrival_info{ now, hash_a }));
	auto result (!inserted.second);
	return result;
}

bool badem::block_arrival::recent (badem::block_hash const & hash_a)
{
	badem::lock_guard<std::mutex> lock (mutex);
	auto now (std::chrono::steady_clock::now ());
	while (arrival.size () > arrival_size_min && arrival.begin ()->arrival + arrival_time_min < now)
	{
		arrival.erase (arrival.begin ());
	}
	return arrival.get<1> ().find (hash_a) != arrival.get<1> ().end ();
}

namespace badem
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_arrival & block_arrival, const std::string & name)
{
	size_t count = 0;
	{
		badem::lock_guard<std::mutex> guard (block_arrival.mutex);
		count = block_arrival.arrival.size ();
	}

	auto sizeof_element = sizeof (decltype (block_arrival.arrival)::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "arrival", count, sizeof_element }));
	return composite;
}
}

std::shared_ptr<badem::node> badem::node::shared ()
{
	return shared_from_this ();
}

bool badem::node::validate_block_by_previous (badem::transaction const & transaction, std::shared_ptr<badem::block> block_a)
{
	bool result (false);
	badem::root account;
	if (!block_a->previous ().is_zero ())
	{
		if (store.block_exists (transaction, block_a->previous ()))
		{
			account = ledger.account (transaction, block_a->previous ());
		}
		else
		{
			result = true;
		}
	}
	else
	{
		account = block_a->root ();
	}
	if (!result && block_a->type () == badem::block_type::state)
	{
		std::shared_ptr<badem::state_block> block_l (std::static_pointer_cast<badem::state_block> (block_a));
		badem::amount prev_balance (0);
		if (!block_l->hashables.previous.is_zero ())
		{
			if (store.block_exists (transaction, block_l->hashables.previous))
			{
				prev_balance = ledger.balance (transaction, block_l->hashables.previous);
			}
			else
			{
				result = true;
			}
		}
		if (!result)
		{
			if (block_l->hashables.balance == prev_balance && ledger.is_epoch_link (block_l->hashables.link))
			{
				account = ledger.epoch_signer (block_l->link ());
			}
		}
	}
	if (!result && (account.is_zero () || badem::validate_message (account, block_a->hash (), block_a->block_signature ())))
	{
		result = true;
	}
	return result;
}

int badem::node::store_version ()
{
	auto transaction (store.tx_begin_read ());
	return store.version_get (transaction);
}

bool badem::node::init_error () const
{
	return store.init_error () || wallets_store.init_error ();
}

badem::inactive_node::inactive_node (boost::filesystem::path const & path_a, uint16_t peering_port_a, badem::node_flags const & node_flags) :
path (path_a),
io_context (std::make_shared<boost::asio::io_context> ()),
alarm (*io_context),
work (1),
peering_port (peering_port_a)
{
	boost::system::error_code error_chmod;

	/*
	 * @warning May throw a filesystem exception
	 */
	boost::filesystem::create_directories (path);
	badem::set_secure_perm_directory (path, error_chmod);
	logging.max_size = std::numeric_limits<std::uintmax_t>::max ();
	logging.init (path);
	node = std::make_shared<badem::node> (*io_context, peering_port, path, alarm, logging, work, node_flags);
	node->active.stop ();
}

badem::inactive_node::~inactive_node ()
{
	node->stop ();
}

badem::node_flags const & badem::inactive_node_flag_defaults ()
{
	static badem::node_flags node_flags;
	node_flags.inactive_node = true;
	node_flags.read_only = true;
	node_flags.cache_representative_weights_from_frontiers = false;
	node_flags.cache_cemented_count_from_frontiers = false;
	return node_flags;
}

std::unique_ptr<badem::block_store> badem::make_store (badem::logger_mt & logger, boost::filesystem::path const & path, bool read_only, bool add_db_postfix, badem::rocksdb_config const & rocksdb_config, badem::txn_tracking_config const & txn_tracking_config_a, std::chrono::milliseconds block_processor_batch_max_time_a, int lmdb_max_dbs, size_t batch_size, bool backup_before_upgrade, bool use_rocksdb_backend)
{
#if BADEM_ROCKSDB
	auto make_rocksdb = [&logger, add_db_postfix, &path, &rocksdb_config, read_only]() {
		return std::make_unique<badem::rocksdb_store> (logger, add_db_postfix ? path / "rocksdb" : path, rocksdb_config, read_only);
	};
#endif

	if (use_rocksdb_backend)
	{
#if BADEM_ROCKSDB
		return make_rocksdb ();
#else
		logger.always_log (std::error_code (badem::error_config::rocksdb_enabled_but_not_supported).message ());
		release_assert (false);
		return nullptr;
#endif
	}
	else
	{
#if BADEM_ROCKSDB
		/** To use RocksDB in tests make sure the node is built with the cmake variable -DBADEM_ROCKSDB=ON and the environment variable TEST_USE_ROCKSDB=1 is set */
		static badem::network_constants network_constants;
		auto use_rocksdb_str = std::getenv ("TEST_USE_ROCKSDB");
		if (use_rocksdb_str && (boost::lexical_cast<int> (use_rocksdb_str) == 1) && network_constants.is_test_network ())
		{
			return make_rocksdb ();
		}
#endif
	}

	return std::make_unique<badem::mdb_store> (logger, add_db_postfix ? path / "data.ldb" : path, txn_tracking_config_a, block_processor_batch_max_time_a, lmdb_max_dbs, batch_size, backup_before_upgrade);
}
