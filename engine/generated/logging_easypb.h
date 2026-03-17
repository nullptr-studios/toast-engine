// Minimal EasyProtoBuf-based logging definitions
// Provides a small compatible API used by logger.cpp to replace Google Protobuf gencode

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <cstring>
#include <algorithm>
#include <easypb.hpp>

namespace logging {

enum class LogData_Severity : int {
	TRACE = 0,
	INFO = 1,
	WARNING = 2,
	ERROR = 3,
	CRITICAL = 4
};

struct LogData {
	uint64_t timestamp = 0;
	LogData_Severity severity = LogData_Severity::TRACE;
	std::string filepath;
	uint32_t line_number = 0;
	std::string sink;
	std::string message;

	LogData() = default;
	LogData(const LogData&) = default;
	LogData(LogData&&) noexcept = default;
	LogData& operator=(const LogData&) = default;
	LogData& operator=(LogData&&) noexcept = default;

	void set_timestamp(uint64_t v) { timestamp = v; }
	void set_filepath(std::string_view v) { filepath.assign(v.data(), v.size()); }
	void set_line_number(uint32_t v) { line_number = v; }
	void set_severity(LogData_Severity s) { severity = s; }
	void set_sink(std::string_view v) { sink.assign(v.data(), v.size()); }
	void set_message(std::string_view v) { message.assign(v.data(), v.size()); }

	// Encode into an easypb::Encoder
	void encode(easypb::Encoder &pb) const {
		pb.put_uint64(1, timestamp);
		pb.put_enum(2, static_cast<int>(severity));
		pb.put_string(3, filepath);
		pb.put_uint32(4, line_number);
		pb.put_string(5, sink);
		pb.put_string(6, message);
	}
};

struct LogBatch {
	std::vector<LogData> logs;

	LogData* add_logs() {
		logs.emplace_back();
		return &logs.back();
	}

	size_t ByteSizeLong() const {
		auto s = easypb::encode(*this);
		return s.size();
	}

	void SerializeToArray(void* data, size_t size) const {
		auto s = easypb::encode(*this);
		size_t to_copy = std::min(size, s.size());
		std::memcpy(data, s.data(), to_copy);
	}

	void encode(easypb::Encoder &pb) const {
		pb.put_repeated_message(1, logs);
	}
};

} // namespace logging
