# Test core Puppet functions (simplified for current parser)

# Test logging functions
notice("Starting core function tests")
info("This is an info message")
warning("This is a warning message")
debug("Debug message: testing = ", true)

# Test with variables
$test_var = "hello world"
notice("Variable test: ", $test_var)

# Test concatenation
$host = "web01"
$port = 8080
notice("Server ", $host, " listening on port ", $port)

# Test defined() function with classes
$apache_defined = defined("apache")
if $apache_defined {
    notice("Apache class is defined")
} else {
    notice("Apache class is not defined")
}

# Test conditional with error
$config_valid = false
if !$config_valid {
    $error_result = err("Configuration is invalid but continuing")
}

notice("Core function tests completed")