# Sensitive Type Constructor Test
# Tests the Sensitive() type constructor (basic pass-through implementation)

# Basic Sensitive wrapping
$password = Sensitive('secret123')
notice("Password wrapped: ${password}")

# Sensitive with variable
$secret_value = 'my_api_key'
$wrapped = Sensitive($secret_value)
notice("Wrapped value: ${wrapped}")

# Sensitive in resource (pass-through for now)
file { '/tmp/test_sensitive':
  ensure  => file,
  content => Sensitive('sensitive content'),
}

notice('Sensitive tests completed')
