#include "test_registry.hpp"
#include "toast/ui/document_preprocess.hpp"
#include "toast/ui/ui_binds.hpp"

#include <RmlUi/Core/Variant.h>
#include <cassert>

TOAST_TEST_NAMED("UI", "ui/03-bind_store", test_ui_03_bind_store) {
	ui::UIBindStore store;

	ui::DocumentScan first;
	first.binds = {"health", "checked", "removed"};
	first.bool_binds = {"checked"};
	store.reconcile(first);

	store.set("health", Rml::Variant(Rml::String("75%")));
	store.set("checked", Rml::Variant(true));
	store.set("removed", Rml::Variant(42));

	ui::DocumentScan reloaded;
	reloaded.binds = {"health", "checked", "new_flag"};
	reloaded.bool_binds = {"checked", "new_flag"};
	store.reconcile(reloaded);

	assert(store.get("health").Get<Rml::String>() == "75%");
	assert(store.get("checked").Get<bool>());
	assert(!store.has("removed"));
	assert(store.get("new_flag").GetType() == Rml::Variant::BOOL);
	assert(!store.get("new_flag").Get<bool>());

	ui::DocumentScan changed_type;
	changed_type.binds = {"health"};
	changed_type.bool_binds = {"health"};
	store.reconcile(changed_type);
	assert(store.get("health").GetType() == Rml::Variant::BOOL);
	assert(!store.get("health").Get<bool>());
	assert(!store.has("checked"));
}
