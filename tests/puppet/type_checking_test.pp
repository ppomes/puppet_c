# Test type checking functions: is_string, is_array, is_hash, is_numeric, is_bool

# Test is_string()
notice("is_string('hello'): ", is_string('hello'))
notice("is_string(123): ", is_string(123))
notice("is_string(['a']): ", is_string(['a']))

# Test is_array()
notice("is_array(['a', 'b']): ", is_array(['a', 'b']))
notice("is_array('hello'): ", is_array('hello'))
notice("is_array(123): ", is_array(123))

# Test is_hash()
$hash = { 'key' => 'value' }
notice("is_hash({key => value}): ", is_hash($hash))
notice("is_hash(['a']): ", is_hash(['a']))
notice("is_hash('hello'): ", is_hash('hello'))

# Test is_numeric()
notice("is_numeric(123): ", is_numeric(123))
notice("is_numeric(3.14): ", is_numeric(3.14))
notice("is_numeric('123'): ", is_numeric('123'))
notice("is_numeric(['a']): ", is_numeric(['a']))

# Test is_bool()
notice("is_bool(true): ", is_bool(true))
notice("is_bool(false): ", is_bool(false))
notice("is_bool('true'): ", is_bool('true'))
notice("is_bool(1): ", is_bool(1))

# Aliases
notice("is_integer(42): ", is_integer(42))
notice("is_boolean(true): ", is_boolean(true))

# Practical example
$value = 'hello'
notice("Value '", $value, "' is string: ", is_string($value))

notice("Type checking tests completed!")
