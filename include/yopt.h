#ifndef YOPT_H
#define YOPT_H

#if defined _WIN32
#include <Windows.h>
#endif
#include <string>
#include <string_view>
#include <map>
#include <unordered_set>
#include <vector>
#include <charconv>
#include <optional>

/// configuration
#define YOPT_CMD_MAX_LENGTH 4096


namespace yopt {

inline constexpr size_t max_length = YOPT_CMD_MAX_LENGTH;

namespace detail {
	inline std::optional<std::string> wstrtoutf8(const std::wstring_view & s);
} //ns detail


template <typename CharT>
class options {
public:
	using char_type = CharT;

	options(int argc, const CharT ** argv) {
		/// start from 1 - skip program name
		for (int i = 1; i < argc; i++) {
			parse(argv[i], true);
		} 
	}

	options(const CharT * cmd_line) {
		parse(cmd_line);
	}

	[[nodiscard]] bool has_opt(std::string_view key) const {
		return find_opt(key) != std::end(opts);
	}

	[[nodiscard]] inline std::optional<std::basic_string_view<CharT>> get_string(std::string_view key) const noexcept {
		const auto it = find_opt(key);
		if (it == std::end(opts))
			return std::nullopt;
		return it->second;
	}

	[[nodiscard]] inline std::basic_string_view<CharT> get_string(std::string_view key, std::basic_string_view<CharT> default_value) const noexcept {
		const auto v = get_string(key);
		if (!v)
			return default_value;
		return *v;
	}

	[[nodiscard]] inline std::basic_string_view<CharT> get_required_string(std::string_view key) const {
		const auto v = get_string(key);
		if (!v.has_value()) {
			throw std::out_of_range("option not provided");
		}
		return v.value();
	}

	[[nodiscard]] bool get_bool(std::string_view key, bool default_value = false) const {
		const auto v = get_string(key);
		if (!v)
			return default_value;
		const auto & s = v.value();
		if (s.empty())
			return true;

		static const std::unordered_set<std::basic_string<CharT>> true_values = {
			make_string("TRUE"), make_string("true"), make_string("T"),
			make_string("YES"), make_string("yes"), make_string("Y"), make_string("y"),
			make_string("1")
		};

		/// initialization from rodata example
		//static constexpr CharT true_value0[] = {'T','R','U','E'};

		//static const std::unordered_set<std::basic_string_view<CharT>> true_values {
		//	make_string_view(true_value0)
		//};

		static const std::unordered_set<std::basic_string<CharT>> false_values = {
			make_string("FALSE"), make_string("false"), make_string("F"),
			make_string("NO"), make_string("no"), make_string("N"), make_string("n"),
			make_string("0")
		};

		if (true_values.find(std::basic_string<CharT>{s}) != std::end(true_values)) {
			return true;
		} else if (false_values.find(std::basic_string<CharT>{s}) != std::end(false_values)) {
			return false;
		}

		throw std::invalid_argument("boolean option argument not recognized");
	}

	[[nodiscard]] inline std::optional<int> get_int(std::string_view key) const noexcept {
		const auto v = get_string(key);
		if (!v)
			return std::nullopt;
		const auto & sv = v.value();
		int value = 0;
		std::from_chars_result r;
		if constexpr (std::is_same_v<CharT, wchar_t>) {
			const std::string s = wstrtoutf8(sv);
			r = std::from_chars(s.data(), s.data() + s.size(), value);
		} else {
			r = std::from_chars(sv.data(), sv.data() + sv.size(), value);
		}
		if (r.ec == std::errc())
			return value;
		return std::nullopt;
	}

	[[nodiscard]] int get_int(std::string_view key, int default_value) const noexcept {
		const auto opt_value = get_int(key);
		return opt_value.value_or(default_value);
	}

	/// free standing argument at index
	[[nodiscard]] inline const std::basic_string_view<CharT> arg(size_t index) const {
		return a.at(index);
	}

	[[nodiscard]] inline auto arg_count() const noexcept {
		return a.size();
	}

	[[nodiscard]] inline const std::vector<std::basic_string_view<CharT>> & args() const noexcept {
		return a;
	}

private:
	enum class parse_state { none, key_prefix, long_key_prefix, key, value, quoted_value };

	std::vector<std::basic_string_view<CharT>> a; /// free standing values
	std::map<std::basic_string_view<CharT>, std::basic_string_view<CharT>> opts; /// parsed key values

	void parse(const CharT * s, bool single_value = false) {
		auto ps = parse_state::none;

		const CharT * c = s;
		const CharT * token_start = c;

		std::basic_string_view<CharT> key;

		size_t len = 0;
		while (len < max_length) {
			switch (ps) {
				case parse_state::none:
					if (is_whitespace(*c)) {
						/// skip
					} else if (is_dash(*c)) {
						ps = parse_state::key_prefix;
						if (key.size() > 0)
							opts[key];
					} else if (is_quote(*c)) {
						ps = parse_state::quoted_value;
						token_start = c;
					} else {
						ps = parse_state::value;
						token_start = c;
					}
					break;
				case parse_state::key_prefix:
					if (is_dash(*c)) {
						ps = parse_state::long_key_prefix;
					} else if (is_whitespace(*c)) {
						ps = parse_state::none;
					} else {
						ps = parse_state::key;
						token_start = c;
					}
					break;
				case parse_state::long_key_prefix:
					if (is_whitespace(*c)) {
						ps = parse_state::none;
					} else {
						ps = parse_state::key;
						token_start = c;
					}
					break;
				case parse_state::key:
					if (is_whitespace(*c)) {
						ps = parse_state::none;
						if (c > token_start) {
							opts[{token_start, c}];
						}
					} else if (is_equal_sign(*c)) {
						ps = parse_state::value;
						key = {token_start, c};
						token_start = c + 1;
					}
					break;
				case parse_state::value:
					if (is_quote(*c) && token_start == c) {
						ps = parse_state::quoted_value;
					} else if (is_whitespace(*c) && !single_value) {
						ps = parse_state::none;
						if (key.size() > 0) {
							opts[key] = {token_start, c};
						} else {
							a.emplace_back(token_start, c);
						}
						key = {};
					}
					break;
				case parse_state::quoted_value:
					if (is_quote(*c)) {
						ps = parse_state::none;
						if (key.size() > 0) {
							opts[key] = {token_start + 1, c};
						} else {
							a.emplace_back(token_start + 1, c);
						}
						key = {};
					}
			}
			if (is_eol(*c)) {
				/// handle trailing tokens
				if (ps == parse_state::key && token_start < c) {
					key = {token_start, c};
					opts[key];
				} else if (ps == parse_state::value) {
					if (key.size() > 0) {
						opts[key] = {token_start, c};
					} else if (c > token_start) { /// store only non empty free standing arguments
						a.emplace_back(token_start, c);
					}
				} else if (ps == parse_state::quoted_value) {
					if (key.size() > 0) {
						opts[key] = {token_start + 1, c};
					} else {
						a.emplace_back(token_start + 1, c);
					}
				}
				break;
			}
			c++;
			len++;
		}
	}

	static constexpr bool is_eol(CharT c) {
		return c == '\0';
	}

	static constexpr bool is_whitespace(CharT c) {
		return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n');
	}

	static constexpr bool is_dash(CharT c) {
		return (c == '-');
	}

	static constexpr bool is_quote(CharT c) {
		return (c == '"');
	}

	static constexpr bool is_equal_sign(CharT c) {
		return (c == '=');
	}

	static inline constexpr std::basic_string<CharT> make_string(auto * s) {
		size_t len = 0;
		const auto * const str = s;
		while (*s++) {
			++len;
		}
		std::basic_string<CharT> result(len, 0);
		for (size_t i = 0; i < len; ++i) {
			result[i] = str[i];
		}
		return result;
	}

	auto find_opt(std::string_view key) const;
};

template <>
auto options<wchar_t>::find_opt(std::string_view key) const {
	const std::wstring wkey{std::begin(key), std::end(key)};
	return opts.find(wkey);
}

template <>
auto options<char>::find_opt(std::string_view key) const {
	return opts.find(key);
}


template <typename CharT>
inline std::basic_string_view<CharT> strip_quotes(const std::basic_string_view<CharT> & s) {
	auto b = cbegin(s);
	if (*b == '"' && b != end(s))
		++b;
	auto e = cend(s);
	if (e != b) {
		--e;
		if (*e == '"' && e != b) {
			--e;
		}
	}
	return std::basic_string_view<CharT>{b, e+1};
}

namespace detail {

#if defined _WIN32
inline std::optional<std::string> wstrtoutf8(const std::wstring_view & s) {
	assert(s.size() < std::numeric_limits<int>::max());
	const auto size = (int) s.size();
	const auto converted_size = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), size, 0, 0, NULL, NULL);
	if (converted_size == 0)
		return std::nullopt;
	std::string res;
	res.resize(converted_size);
	const auto r = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), size, res.data(), converted_size, NULL, NULL);
	if (r == 0)
		return std::nullopt;
	return res;
}
#endif

} //ns yopt::detail

} //ns yopt


#ifdef YOPT_TEST

#ifndef DOCTEST_LIBRARY_INCLUDED
#include <doctest.h>
#endif

TEST_CASE("options wchar_t") {
	const wchar_t command_line[] = L"--first-option --second-option=value \"first quoted argument\"";
	yopt::options o{command_line};
	CHECK(o.arg_count() == 1);
	CHECK(o.arg(0) == L"first quoted argument");
	CHECK_THROWS_AS(o.arg(1), const std::out_of_range &);
	CHECK(o.has_opt("nonexistent") == false);
	CHECK(o.has_opt("first-option"));
	CHECK(o.has_opt("second-option"));
	CHECK(o.get_bool("first-option"));
	CHECK_THROWS_AS(o.get_required_string("nonexistent"), const std::out_of_range &);
	CHECK(o.get_string("first-option").has_value());
	CHECK(o.get_string("first-option").value() == L"");
	CHECK(o.get_string("second-option").value() == L"value");
}

TEST_CASE("options bool") {
	const char true_cmd[] = "--bool0 --bool1=TRUE --bool2=Y --bool3=1";
	yopt::options to{true_cmd};
	CHECK(to.get_bool("bool_nonexistent_default", false) == false);
	CHECK(to.get_bool("bool_nonexistent_default", true) == true);

	CHECK(to.get_bool("bool0", false) == true);
	CHECK(to.get_bool("bool1", false) == true);
	CHECK(to.get_bool("bool2", false) == true);
	CHECK(to.get_bool("bool3", false) == true);

	const char false_cmd[] = "--bool1=F --bool2=no --bool3=0";
	yopt::options fo{false_cmd};
	CHECK(fo.get_bool("bool1", true) == false);
	CHECK(fo.get_bool("bool2", true) == false);
	CHECK(fo.get_bool("bool3", true) == false);
}

TEST_CASE("options escaping") {
	yopt::options o{"--t=\"x x\" \"x x x\""};
	CHECK(o.arg(0) == "x x x");
	CHECK(o.get_required_string("t") == "x x");
}

TEST_CASE("options argv") {
	constexpr int argc = 5;
	const char * argv[argc] = {
		"binary.exe",
		"--t=42",
		"--u",
		"\"param param\"",
		"param param"
	};
	yopt::options o{argc, argv};
	CHECK(o.arg_count() == 2);
	CHECK(o.has_opt("t"));
	CHECK(o.has_opt("u"));
	CHECK(o.get_int("t").has_value());
	CHECK(o.get_int("t").value() == 42);
}

#endif //YOPT_TEST

#endif //YOPT_H