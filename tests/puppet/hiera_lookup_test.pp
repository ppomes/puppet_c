# Test Hiera lookup functionality

# Simple lookup
$message = lookup('test_message')
notice("Hiera message: ${message}")

# Lookup with default
$unknown = lookup('unknown_key', 'default_value')
notice("Unknown key with default: ${unknown}")

# Lookup number
$number = lookup('test_number')
notice("Hiera number: ${number}")

# Lookup boolean
$bool = lookup('test_boolean')
notice("Hiera boolean: ${bool}")

# Test that lookup() function exists and is callable
notice("Hiera lookup test completed")