#pragma once

#include <badem/node/node.hpp>

#include <boost/thread.hpp>

#include <QtGui>
#include <QtWidgets>
#include <set>

namespace badem_qt
{
static const QString saved_ratio_key = "settings/ratio";
class wallet;
class eventloop_processor : public QObject
{
public:
	bool event (QEvent *) override;
};
class eventloop_event : public QEvent
{
public:
	eventloop_event (std::function<void()> const &);
	std::function<void()> action;
};
class settings
{
public:
	settings (badem_qt::wallet &);
	void refresh_representative ();
	void activate ();
	void update_locked (bool, bool);
	QWidget * window;
	QVBoxLayout * layout;
	QLineEdit * password;
	QPushButton * lock_toggle;
	QFrame * sep1;
	QLineEdit * new_password;
	QLineEdit * retype_password;
	QPushButton * change;
	QFrame * sep2;
	QLabel * representative;
	QLabel * current_representative;
	QLineEdit * new_representative;
	QPushButton * change_rep;
	QPushButton * back;
	badem_qt::wallet & wallet;
};
class advanced_actions
{
public:
	advanced_actions (badem_qt::wallet &);
	QWidget * window;
	QVBoxLayout * layout;
	QPushButton * show_ledger;
	QPushButton * show_peers;
	QPushButton * search_for_receivables;
	QPushButton * bootstrap;
	QPushButton * wallet_refresh;
	QPushButton * create_block;
	QPushButton * enter_block;
	QPushButton * block_viewer;
	QPushButton * account_viewer;
	QPushButton * stats_viewer;
	QWidget * scale_window;
	QHBoxLayout * scale_layout;
	QLabel * scale_label;
	QButtonGroup * ratio_group;
	QRadioButton * mbadem_unit;
	QRadioButton * kbadem_unit;
	QRadioButton * badem_unit;
	QRadioButton * raw_unit;
	QPushButton * back;

	QWidget * ledger_window;
	QVBoxLayout * ledger_layout;
	QStandardItemModel * ledger_model;
	QTableView * ledger_view;
	QPushButton * ledger_refresh;
	QPushButton * ledger_back;

	QWidget * peers_window;
	QVBoxLayout * peers_layout;
	QStandardItemModel * peers_model;
	QTableView * peers_view;
	QHBoxLayout * peer_summary_layout;
	QLabel * bootstrap_label;
	QLabel * peer_count_label;
	QLineEdit * bootstrap_line;
	QPushButton * peers_bootstrap;
	QPushButton * peers_refresh;
	QPushButton * peers_back;

	badem_qt::wallet & wallet;

private:
	void refresh_ledger ();
	void refresh_peers ();
	void refresh_stats ();
};
class block_entry
{
public:
	block_entry (badem_qt::wallet &);
	QWidget * window;
	QVBoxLayout * layout;
	QPlainTextEdit * block;
	QLabel * status;
	QPushButton * process;
	QPushButton * back;
	badem_qt::wallet & wallet;
};
class block_creation
{
public:
	block_creation (badem_qt::wallet &);
	void deactivate_all ();
	void activate_send ();
	void activate_receive ();
	void activate_change ();
	void activate_open ();
	void create_send ();
	void create_receive ();
	void create_change ();
	void create_open ();
	QWidget * window;
	QVBoxLayout * layout;
	QButtonGroup * group;
	QHBoxLayout * button_layout;
	QRadioButton * send;
	QRadioButton * receive;
	QRadioButton * change;
	QRadioButton * open;
	QLabel * account_label;
	QLineEdit * account;
	QLabel * source_label;
	QLineEdit * source;
	QLabel * amount_label;
	QLineEdit * amount;
	QLabel * destination_label;
	QLineEdit * destination;
	QLabel * representative_label;
	QLineEdit * representative;
	QPlainTextEdit * block;
	QLabel * status;
	QPushButton * create;
	QPushButton * back;
	badem_qt::wallet & wallet;
};
class self_pane
{
public:
	self_pane (badem_qt::wallet &, badem::account const &);
	void set_balance_text (std::pair<badem::uint128_t, badem::uint128_t>);
	QWidget * window;
	QVBoxLayout * layout;
	QHBoxLayout * self_layout;
	QWidget * self_window;
	QLabel * your_account_label;
	QLabel * version;
	QWidget * account_window;
	QHBoxLayout * account_layout;
	QLineEdit * account_text;
	QPushButton * copy_button;
	QWidget * balance_window;
	QHBoxLayout * balance_layout;
	QLabel * balance_label;
	badem_qt::wallet & wallet;
};
class accounts
{
public:
	accounts (badem_qt::wallet &);
	void refresh ();
	void refresh_wallet_balance ();
	QLabel * wallet_balance_label;
	QWidget * window;
	QVBoxLayout * layout;
	QStandardItemModel * model;
	QTableView * view;
	QPushButton * use_account;
	QPushButton * create_account;
	QPushButton * import_wallet;
	QPushButton * backup_seed;
	QFrame * separator;
	QLineEdit * account_key_line;
	QPushButton * account_key_button;
	QPushButton * back;
	badem_qt::wallet & wallet;
};
class import
{
public:
	import (badem_qt::wallet &);
	QWidget * window;
	QVBoxLayout * layout;
	QLabel * seed_label;
	QLineEdit * seed;
	QLabel * clear_label;
	QLineEdit * clear_line;
	QPushButton * import_seed;
	QFrame * separator;
	QLabel * filename_label;
	QLineEdit * filename;
	QLabel * password_label;
	QLineEdit * password;
	QPushButton * perform;
	QPushButton * back;
	badem_qt::wallet & wallet;
};
class history
{
public:
	history (badem::ledger &, badem::account const &, badem_qt::wallet &);
	void refresh ();
	QWidget * window;
	QVBoxLayout * layout;
	QStandardItemModel * model;
	QTableView * view;
	QWidget * tx_window;
	QHBoxLayout * tx_layout;
	QLabel * tx_label;
	QSpinBox * tx_count;
	badem::ledger & ledger;
	badem::account const & account;
	badem_qt::wallet & wallet;
};
class block_viewer
{
public:
	block_viewer (badem_qt::wallet &);
	void rebroadcast_action (badem::uint256_union const &);
	QWidget * window;
	QVBoxLayout * layout;
	QLabel * hash_label;
	QLineEdit * hash;
	QLabel * block_label;
	QPlainTextEdit * block;
	QLabel * successor_label;
	QLineEdit * successor;
	QPushButton * retrieve;
	QPushButton * rebroadcast;
	QPushButton * back;
	badem_qt::wallet & wallet;
};
class account_viewer
{
public:
	account_viewer (badem_qt::wallet &);
	QWidget * window;
	QVBoxLayout * layout;
	QLabel * account_label;
	QLineEdit * account_line;
	QPushButton * refresh;
	QWidget * balance_window;
	QHBoxLayout * balance_layout;
	QLabel * balance_label;
	badem_qt::history history;
	QPushButton * back;
	badem::account account;
	badem_qt::wallet & wallet;
};
class stats_viewer
{
public:
	stats_viewer (badem_qt::wallet &);
	QWidget * window;
	QVBoxLayout * layout;
	QPushButton * refresh;
	QPushButton * clear;
	QStandardItemModel * model;
	QTableView * view;
	QPushButton * back;
	badem_qt::wallet & wallet;
	void refresh_stats ();
};
enum class status_types
{
	not_a_status,
	disconnected,
	working,
	locked,
	vulnerable,
	active,
	synchronizing,
	nominal
};
class status
{
public:
	status (badem_qt::wallet &);
	void erase (badem_qt::status_types);
	void insert (badem_qt::status_types);
	void set_text ();
	std::string text ();
	std::string color ();
	std::set<badem_qt::status_types> active;
	badem_qt::wallet & wallet;
};
class wallet : public std::enable_shared_from_this<badem_qt::wallet>
{
public:
	wallet (QApplication &, badem_qt::eventloop_processor &, badem::node &, std::shared_ptr<badem::wallet>, badem::account &);
	void start ();
	void refresh ();
	void update_connected ();
	void empty_password ();
	void change_rendering_ratio (badem::uint128_t const &);
	std::string format_balance (badem::uint128_t const &) const;
	badem::uint128_t rendering_ratio;
	badem::node & node;
	std::shared_ptr<badem::wallet> wallet_m;
	badem::account & account;
	badem_qt::eventloop_processor & processor;
	badem_qt::history history;
	badem_qt::accounts accounts;
	badem_qt::self_pane self;
	badem_qt::settings settings;
	badem_qt::advanced_actions advanced;
	badem_qt::block_creation block_creation;
	badem_qt::block_entry block_entry;
	badem_qt::block_viewer block_viewer;
	badem_qt::account_viewer account_viewer;
	badem_qt::stats_viewer stats_viewer;
	badem_qt::import import;

	QApplication & application;
	QLabel * status;
	QStackedWidget * main_stack;

	QWidget * client_window;
	QVBoxLayout * client_layout;

	QWidget * entry_window;
	QVBoxLayout * entry_window_layout;
	QFrame * separator;
	QLabel * account_history_label;
	QPushButton * send_blocks;
	QPushButton * settings_button;
	QPushButton * accounts_button;
	QPushButton * show_advanced;

	QWidget * send_blocks_window;
	QVBoxLayout * send_blocks_layout;
	QLabel * send_account_label;
	QLineEdit * send_account;
	QLabel * send_count_label;
	QLineEdit * send_count;
	QPushButton * send_blocks_send;
	QPushButton * send_blocks_back;

	badem_qt::status active_status;
	void pop_main_stack ();
	void push_main_stack (QWidget *);
	void ongoing_refresh ();
	std::atomic<bool> needs_balance_refresh;
	std::atomic<bool> needs_deterministic_restore;
};
}
