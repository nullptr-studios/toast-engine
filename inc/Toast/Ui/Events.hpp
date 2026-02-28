/**
 * @file Events.hpp
 * @author Dante Harper
 * @date 27/02/26
 *
 * @brief [TODO: Brief description of the file's purpose]
 */

#pragma once

#include <Toast/Event/Event.hpp>
#include <utility>

namespace ui {

struct LoadUrl : public event::Event<LoadUrl> {
	LoadUrl(std::string url) : url(std::move(url)) { }

	std::string url;
};

struct ExecuteJS : public event::Event<ExecuteJS> {
	ExecuteJS(std::string script) : script(std::move(script)) { }

	std::string script;
};

}
