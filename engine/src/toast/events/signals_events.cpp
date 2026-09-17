#include <generated/signals.pb.h>
#include <toast/events/proto_event.hpp>
#include <toast/events/signals_events.hpp>

namespace event {

static auto toProtoSource(signals::ConnectionSource source) -> proto::events::SignalConnectionSource {
	return static_cast<proto::events::SignalConnectionSource>(source);
}

static auto fromProtoSource(proto::events::SignalConnectionSource source) -> signals::ConnectionSource {
	return static_cast<signals::ConnectionSource>(source);
}

template<>
struct ProtoTraits<RequestSignalState> {
	using Proto = proto::events::RequestSignalState;
	using Event = RequestSignalState;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_node(e.node);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.node = toast::UID::fromString(p.node());
		return e;
	}
};

TOAST_PROTO_EVENT(RequestSignalState);

template<>
struct ProtoTraits<SignalState> {
	using Proto = proto::events::SignalState;
	using Event = SignalState;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_node(e.node);
		p.set_error(e.error);
		for (const auto& signal : e.signals) {
			auto* ps = p.add_signals();
			ps->set_declaring_type(signal.declaring_type);
			ps->set_signal(signal.signal);
			for (const auto& connection : signal.connections) {
				auto* pc = ps->add_connections();
				pc->set_target_uid(connection.target);
				pc->set_target_name(connection.target_name);
				pc->set_target_type(connection.target_type);
				pc->set_function(connection.function);
				pc->set_source(toProtoSource(connection.source));
				pc->set_forwards_args(connection.forwards_args);
			}
		}
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.node = toast::UID::fromString(p.node());
		e.error = p.error();
		for (const auto& ps : p.signals()) {
			auto& signal = e.signals.emplace_back();
			signal.declaring_type = ps.declaring_type();
			signal.signal = ps.signal();
			for (const auto& pc : ps.connections()) {
				signal.connections.push_back(
				    {toast::UID::fromString(pc.target_uid()),
				     pc.target_name(),
				     pc.target_type(),
				     pc.function(),
				     fromProtoSource(pc.source()),
				     pc.forwards_args()}
				);
			}
		}
		return e;
	}
};

TOAST_PROTO_EVENT(SignalState);

template<>
struct ProtoTraits<RequestSignalCallables> {
	using Proto = proto::events::RequestSignalCallables;
	using Event = RequestSignalCallables;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_request(e.request);
		p.set_source_node(e.source_node);
		p.set_declaring_type(e.declaring_type);
		p.set_signal(e.signal);
		p.set_target_node(e.target_node);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.request = p.request();
		e.source_node = toast::UID::fromString(p.source_node());
		e.declaring_type = p.declaring_type();
		e.signal = p.signal();
		e.target_node = toast::UID::fromString(p.target_node());
		return e;
	}
};

TOAST_PROTO_EVENT(RequestSignalCallables);

template<>
struct ProtoTraits<SignalCallables> {
	using Proto = proto::events::SignalCallables;
	using Event = SignalCallables;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_request(e.request);
		p.set_target_node(e.target_node);
		p.set_error(e.error);
		for (const auto& callable : e.callables) {
			auto* pc = p.add_callables();
			pc->set_name(callable.name);
			pc->set_has_cpp(callable.has_cpp);
			pc->set_has_lua(callable.has_lua);
			pc->set_compatible(callable.compatible);
			pc->set_already_connected(callable.already_connected);
			pc->set_forwards_args(callable.forwards_args);
			pc->set_disabled_reason(callable.disabled_reason);
			for (const auto& parameter : callable.parameters) {
				auto* pp = pc->add_parameters();
				pp->set_name(parameter.name);
				pp->set_type(parameter.type);
			}
		}
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.request = p.request();
		e.target_node = toast::UID::fromString(p.target_node());
		e.error = p.error();
		for (const auto& pc : p.callables()) {
			auto& callable = e.callables.emplace_back();
			callable.name = pc.name();
			callable.has_cpp = pc.has_cpp();
			callable.has_lua = pc.has_lua();
			callable.compatible = pc.compatible();
			callable.already_connected = pc.already_connected();
			callable.forwards_args = pc.forwards_args();
			callable.disabled_reason = pc.disabled_reason();
			for (const auto& pp : pc.parameters()) {
				callable.parameters.push_back({pp.name(), pp.type()});
			}
		}
		return e;
	}
};

TOAST_PROTO_EVENT(SignalCallables);

template<>
struct ProtoTraits<AddSignalConnection> {
	using Proto = proto::events::AddSignalConnection;
	using Event = AddSignalConnection;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_source_node(e.source_node);
		p.set_declaring_type(e.declaring_type);
		p.set_signal(e.signal);
		p.set_target_node(e.target_node);
		p.set_function(e.function);
		p.set_forwards_args(e.forwards_args);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.source_node = toast::UID::fromString(p.source_node());
		e.declaring_type = p.declaring_type();
		e.signal = p.signal();
		e.target_node = toast::UID::fromString(p.target_node());
		e.function = p.function();
		e.forwards_args = p.forwards_args();
		return e;
	}
};

TOAST_PROTO_EVENT(AddSignalConnection);

template<>
struct ProtoTraits<RemoveSignalConnection> {
	using Proto = proto::events::RemoveSignalConnection;
	using Event = RemoveSignalConnection;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_source_node(e.source_node);
		p.set_declaring_type(e.declaring_type);
		p.set_signal(e.signal);
		p.set_target_node(e.target_node);
		p.set_function(e.function);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.source_node = toast::UID::fromString(p.source_node());
		e.declaring_type = p.declaring_type();
		e.signal = p.signal();
		e.target_node = toast::UID::fromString(p.target_node());
		e.function = p.function();
		return e;
	}
};

TOAST_PROTO_EVENT(RemoveSignalConnection);

template<>
struct ProtoTraits<ClearEditorSignalConnections> {
	using Proto = proto::events::ClearEditorSignalConnections;
	using Event = ClearEditorSignalConnections;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_source_node(e.source_node);
		p.set_declaring_type(e.declaring_type);
		p.set_signal(e.signal);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.source_node = toast::UID::fromString(p.source_node());
		e.declaring_type = p.declaring_type();
		e.signal = p.signal();
		return e;
	}
};

TOAST_PROTO_EVENT(ClearEditorSignalConnections);

}
