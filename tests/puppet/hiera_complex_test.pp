# Test complex Hiera data structures

# Test array lookup
$array = lookup('test_array')
notice("Array from Hiera: ${array}")

# Test hash lookup
$hash = lookup('test_hash')
notice("Hash from Hiera: ${hash}")

# Test nested hash lookup
$apache_config = lookup('apache')
notice("Apache config from Hiera: ${apache_config}")