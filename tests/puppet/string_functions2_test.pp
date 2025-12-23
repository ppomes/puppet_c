# Test string functions: lstrip, rstrip, chomp, chop, capitalize

# Test lstrip() - remove leading whitespace
notice("lstrip('  hello'): '", lstrip('  hello'), "'")
notice("lstrip('hello'): '", lstrip('hello'), "'")
notice("lstrip('  hello  '): '", lstrip('  hello  '), "'")

# Test rstrip() - remove trailing whitespace
notice("rstrip('hello  '): '", rstrip('hello  '), "'")
notice("rstrip('hello'): '", rstrip('hello'), "'")
notice("rstrip('  hello  '): '", rstrip('  hello  '), "'")

# Test lstrip + rstrip = strip
$padded = '  hello  '
notice("lstrip + rstrip: '", rstrip(lstrip($padded)), "'")
notice("strip: '", strip($padded), "'")

# Test chomp() - remove trailing newlines only
$with_newline = "hello\n"
$with_crlf = "hello\r\n"
$no_newline = "hello"
$with_spaces = "hello  "

notice("chomp('hello\\n'): '", chomp($with_newline), "'")
notice("chomp('hello\\r\\n'): '", chomp($with_crlf), "'")
notice("chomp('hello'): '", chomp($no_newline), "'")
notice("chomp('hello  '): '", chomp($with_spaces), "'")

# Test chop() - remove last character
notice("chop('hello'): '", chop('hello'), "'")
notice("chop('hello\\n'): '", chop($with_newline), "'")
notice("chop('a'): '", chop('a'), "'")
notice("chop(''): '", chop(''), "'")

# Test capitalize() - first letter upper, rest lower
notice("capitalize('hello'): '", capitalize('hello'), "'")
notice("capitalize('HELLO'): '", capitalize('HELLO'), "'")
notice("capitalize('hELLO wORLD'): '", capitalize('hELLO wORLD'), "'")
notice("capitalize('a'): '", capitalize('a'), "'")
notice("capitalize(''): '", capitalize(''), "'")

# Combined operations
$input = "  HELLO WORLD\n"
notice("Original: '", $input, "'")
notice("strip + capitalize: '", capitalize(strip($input)), "'")
notice("chomp + lstrip + capitalize: '", capitalize(lstrip(chomp($input))), "'")

notice("String function tests (part 2) completed!")
