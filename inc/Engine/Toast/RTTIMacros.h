/// @file RTTIMacros.hpp
/// @author Xein
/// @date 09/11/25

#pragma once

/// @def REGISTER_TYPE(CLASS)
/// @breif RTTI macro
/// This macro should be used in ALL classes that inherit from object in order to provide RTTI
/// support that is not dependent on the compiler implementation
#define REGISTER_TYPE(CLASS)                            \
	[[nodiscard]]                                         \
	static const char* static_type() {                    \
		return #CLASS;                                      \
	}                                                     \
	[[nodiscard]]                                         \
	const char* type() const noexcept override {          \
		return static_type();                               \
	}                                                     \
	static inline Object::Registrar<CLASS> registrar_ {};

#define REGISTER_ABSTRACT(CLASS)               \
	[[nodiscard]]                                \
	static const char* static_type() {           \
		return #CLASS;                             \
	}                                            \
	[[nodiscard]]                                \
	const char* type() const noexcept override { \
		return static_type();                      \
	}
