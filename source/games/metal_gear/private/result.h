#pragma once
#include <string>
#include <variant>

struct failure final
{
	std::string msg;
};

template<typename T = std::monostate>
	requires(!std::same_as<T, failure>)
class result final
{
public:
	result() : v_() {}
	result(T value) : v_(std::move(value)) {}
	result(failure f) : v_(std::move(f)) {}

	explicit operator bool() const { return v_.index() == 0; }

	T& value() { return std::get<0>(v_); }
	const T& value() const { return std::get<0>(v_); }
	const std::string& error() const { return std::get<1>(v_).msg; }

private:
	std::variant<T, failure> v_;
};
