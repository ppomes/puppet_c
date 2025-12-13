# Test enhanced class instantiation with proper parameter matching

# Global variables for testing
$environment = "production"
$default_port = 8080

# Define a class with mixed parameter patterns
class webserver($port = 80, $ssl = false, $workers = 2, $domain) {
  $service_name = "nginx"
  $config_dir = "/etc/nginx"
  
  # Test parameter usage within class body
  $listen_port = $port
  $ssl_enabled = $ssl
  $worker_count = $workers
  $server_domain = $domain
  
  # Test global variable access
  $env_setting = $environment
  $fallback_port = $default_port
}

# Test instantiation with partial parameters (should use defaults for missing)
class { 'webserver':
  port => 443,
  ssl => true,
  domain => "example.com",
}

# Test different syntax patterns
class monitoring($enabled = true) {
  $service_type = "monitoring"
  $is_active = $enabled
}

# Test instantiation with no parameters
class { 'monitoring': }

$test_complete = "enhanced instantiation working"