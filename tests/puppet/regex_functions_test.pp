# Test regex functions: regsubst, match

# Test regsubst() - basic substitution
notice("regsubst('hello world', 'world', 'puppet'): ", regsubst('hello world', 'world', 'puppet'))
notice("regsubst('foo bar baz', 'bar', 'qux'): ", regsubst('foo bar baz', 'bar', 'qux'))

# Test regsubst() with global flag
notice("regsubst('foo bar foo', 'foo', 'baz', 'g'): ", regsubst('foo bar foo', 'foo', 'baz', 'g'))
notice("regsubst('aaa', 'a', 'b', 'g'): ", regsubst('aaa', 'a', 'b', 'g'))

# Test regsubst() with case-insensitive flag
notice("regsubst('Hello HELLO hello', 'hello', 'hi', 'i'): ", regsubst('Hello HELLO hello', 'hello', 'hi', 'i'))

# Test regsubst() with both flags
notice("regsubst('Hello HELLO hello', 'hello', 'hi', 'gi'): ", regsubst('Hello HELLO hello', 'hello', 'hi', 'gi'))

# Test regsubst() with regex patterns
notice("regsubst('foo123bar', '[0-9]+', 'NUM'): ", regsubst('foo123bar', '[0-9]+', 'NUM'))
notice("regsubst('the quick brown fox', 'quick|slow', 'fast'): ", regsubst('the quick brown fox', 'quick|slow', 'fast'))

# Test match() - basic matching
notice("match('hello world', 'world'): ", match('hello world', 'world'))
notice("match('hello world', 'xyz'): ", match('hello world', 'xyz'))

# Test match() with regex patterns
notice("match('foo123bar', '[0-9]+'): ", match('foo123bar', '[0-9]+'))
notice("match('the quick brown fox', 'q.*k'): ", match('the quick brown fox', 'q.*k'))

# Test match() with capture groups
notice("match('hello world', '(h..)lo (w..ld)'): ", match('hello world', '(h..)lo (w..ld)'))
notice("match('foo 123 bar', '([a-z]+) ([0-9]+)'): ", match('foo 123 bar', '([a-z]+) ([0-9]+)'))

# Practical examples
$version_string = 'package-1.2.3-release'
notice("Extracting version from '", $version_string, "'")
notice("match with pattern: ", match($version_string, '([0-9]+)\\.([0-9]+)\\.([0-9]+)'))

$log_line = '2024-01-15 Error: Connection failed'
notice("Sanitized log: ", regsubst($log_line, '[0-9]{4}-[0-9]{2}-[0-9]{2}', 'DATE'))

notice("Regex function tests completed!")
