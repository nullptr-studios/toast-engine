#include "window_events.hpp"

#include "toast/events/proto_event.hpp"
#include "window_events.pb.h"

namespace event {

template<>
struct ProtoTraits<ExitApplication> {
	using Proto = proto::events::ExitApplication;
	using Event = ExitApplication;

	static auto toProto(const Event&) -> Proto { return {}; }

	static auto fromProto(const Proto&) -> Event { return {}; }
};

TOAST_PROTO_EVENT(ExitApplication);

template<>
struct ProtoTraits<WindowClose> {
	using Proto = proto::events::WindowClose;
	using Event = WindowClose;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_window_id(e.window_id);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return Event(p.window_id()); }
};

TOAST_PROTO_EVENT(WindowClose);

template<>
struct ProtoTraits<WindowDrop> {
	using Proto = proto::events::WindowDrop;
	using Event = WindowDrop;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		auto* files = p.mutable_paths();
		files->Reserve(e.files.size());
		files->Assign(e.files.begin(), e.files.end());
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		return Event {
		  {p.paths().begin(), p.paths().end()}
		};
	}
};

TOAST_PROTO_EVENT(WindowDrop);

template<>
struct ProtoTraits<WindowKey> {
	using Proto = proto::events::WindowKey;
	using Event = WindowKey;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_key(e.key);
		p.set_scancode(e.scancode);
		p.set_actions(e.action);
		p.set_mods(e.mods);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return Event {p.key(), p.scancode(), p.actions(), p.mods()}; }
};

TOAST_PROTO_EVENT(WindowKey);

template<>
struct ProtoTraits<WindowChar> {
	using Proto = proto::events::WindowChar;
	using Event = WindowChar;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_key(e.key);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return Event {p.key()}; }
};

TOAST_PROTO_EVENT(WindowChar);

template<>
struct ProtoTraits<WindowMousePosition> {
	using Proto = proto::events::WindowMousePosition;
	using Event = WindowMousePosition;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_x(e.x);
		p.set_y(e.y);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return Event {p.x(), p.y()}; }
};

TOAST_PROTO_EVENT(WindowMousePosition);

template<>
struct ProtoTraits<WindowMouseButton> {
	using Proto = proto::events::WindowMouseButton;
	using Event = WindowMouseButton;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_button(e.button);
		p.set_action(e.action);
		p.set_mods(e.mods);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return Event {p.button(), p.action(), p.mods()}; }
};

TOAST_PROTO_EVENT(WindowMouseButton);

template<>
struct ProtoTraits<WindowMouseScroll> {
	using Proto = proto::events::WindowMouseScroll;
	using Event = WindowMouseScroll;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_x(e.x);
		p.set_y(e.y);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return Event {p.x(), p.y()}; }
};

TOAST_PROTO_EVENT(WindowMouseScroll);

template<>
struct ProtoTraits<WindowResize> {
	using Proto = proto::events::WindowResize;
	using Event = WindowResize;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_width(e.width);
		p.set_height(e.height);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return Event {p.width(), p.height()}; }
};

TOAST_PROTO_EVENT(WindowResize);

}
