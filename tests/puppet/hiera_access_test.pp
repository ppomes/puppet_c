# Test accessing elements from complex Hiera data

# Access array element
$array = lookup('test_array')
notice("First array element would be accessed here")

# Access hash values
$apache = lookup('apache')
# In full Puppet, we'd access like $apache['port']
notice("Apache data loaded successfully")

# Show that we can still get simple values
$message = lookup('test_message')
notice("Simple value still works: ${message}")