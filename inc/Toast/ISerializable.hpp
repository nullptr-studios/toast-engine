/**
 * @file ISerializable.hpp
 * @author Xein <xgonip@gmail.com>
 * @date 9/30/25
 * @brief [Brief description of the file's purpose]
 */

#pragma once
#include <nlohmann/json.hpp>

using json_t = nlohmann::ordered_json;

namespace toast {

class ISerializable {
public:
	virtual void Load(json_t j, bool force_create = true) = 0;
	virtual void SoftLoad() = 0;

	[[nodiscard]]
	virtual json_t Save() const = 0;
	virtual void SoftSave() const = 0;

#ifdef TOAST_EDITOR
	virtual void Inspector() = 0;
#endif
};

}
