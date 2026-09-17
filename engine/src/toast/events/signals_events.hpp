#pragma once

#include <cstdint>
#include <string>
#include <toast/events/event.hpp>
#include <toast/events/signal_types.hpp>
#include <toast/uid.hpp>
#include <vector>

namespace event {

struct SignalConnection {
	toast::UID target;
	std::string target_name;
	std::string target_type;
	std::string function;
	signals::ConnectionSource source = signals::ConnectionSource::unknown;
	bool forwards_args = true;
};

struct SignalStateEntry {
	std::string declaring_type;
	std::string signal;
	std::vector<SignalConnection> connections;
};

struct RequestSignalState : Event<RequestSignalState> {
	toast::UID node;
};

struct SignalState : Event<SignalState> {
	toast::UID node;
	std::vector<SignalStateEntry> signals;
	std::string error;
};

struct RequestSignalCallables : Event<RequestSignalCallables> {
	uint64_t request = 0;
	toast::UID source_node;
	std::string declaring_type;
	std::string signal;
	toast::UID target_node;
};

struct SignalParameter {
	std::string name;
	std::string type;
};

struct SignalCallable {
	std::string name;
	std::vector<SignalParameter> parameters;
	bool has_cpp = false;
	bool has_lua = false;
	bool compatible = false;
	bool already_connected = false;
	bool forwards_args = true;
	std::string disabled_reason;
};

struct SignalCallables : Event<SignalCallables> {
	uint64_t request = 0;
	toast::UID target_node;
	std::vector<SignalCallable> callables;
	std::string error;
};

struct AddSignalConnection : Event<AddSignalConnection> {
	toast::UID source_node;
	std::string declaring_type;
	std::string signal;
	toast::UID target_node;
	std::string function;
	bool forwards_args = true;
};

struct RemoveSignalConnection : Event<RemoveSignalConnection> {
	toast::UID source_node;
	std::string declaring_type;
	std::string signal;
	toast::UID target_node;
	std::string function;
};

struct ClearEditorSignalConnections : Event<ClearEditorSignalConnections> {
	toast::UID source_node;
	std::string declaring_type;
	std::string signal;
};

}
