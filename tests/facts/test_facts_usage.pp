# Test Puppet manifest demonstrating facts usage
# This manifest uses facts to make decisions about configuration

# Access basic facts directly
$server_hostname = $facts['hostname']
$os_name = $facts['operatingsystem']
$os_version = $facts['operatingsystemrelease']
$server_arch = $architecture

# Use facts in conditional logic for OS-specific configuration
class apache($port = 80) {
  # Use hostname in configuration
  $server_name = $facts['hostname']
  $config_file = "/etc/apache2/sites-enabled/${facts['hostname']}.conf"
  
  # OS-specific package names
  if $facts['operatingsystem'] == "Ubuntu" {
    $package_name = "apache2"
    $service_name = "apache2"
  } elsif $facts['operatingsystem'] == "CentOS" {
    $package_name = "httpd"
    $service_name = "httpd"
  } else {
    $package_name = "apache"
    $service_name = "apache"
  }
  
  # Memory-based worker configuration
  if $memory.system.total == "4.00 GiB" {
    $max_workers = 150
  } else {
    $max_workers = 75
  }
  
  # Architecture-specific optimizations
  if $architecture == "x86_64" {
    $enable_64bit_optimizations = true
  } else {
    $enable_64bit_optimizations = false
  }
}

# Environment-based class instantiation
if $environment == "production" {
  class { "apache":
    port => 443,
  }

  # Production-specific settings
  $ssl_enabled = true
  $debug_mode = false
  $log_level = "warn"
} else {
  class { "apache":
    port => 8080,
  }

  # Development settings
  $ssl_enabled = false
  $debug_mode = true
  $log_level = "debug"
}

# Role-based configuration
if $role == "webserver" {
  $install_php = true
  $install_mysql_client = true
} elsif $role == "database" {
  $install_mysql_server = true
  $install_php = false
} else {
  $install_php = false
  $install_mysql_client = false
}

# Network configuration based on interface facts
$primary_ip = $networking.interfaces.eth0.ip
$primary_netmask = $networking.interfaces.eth0.netmask

# Datacenter-specific settings
if $datacenter == "us-east-1" {
  $ntp_servers = "us.pool.ntp.org"
  $dns_servers = "8.8.8.8"
} elsif $datacenter == "eu-west-1" {
  $ntp_servers = "europe.pool.ntp.org" 
  $dns_servers = "1.1.1.1"
} else {
  $ntp_servers = "pool.ntp.org"
  $dns_servers = "8.8.8.8"
}

# System resource-based decisions
$processor_count = $processors.count
if $processor_count > 2 {
  $enable_parallel_processing = true
  $worker_processes = $processor_count
} else {
  $enable_parallel_processing = false
  $worker_processes = 2
}

# Summary variables showing fact integration
$deployment_summary = "Deploying to ${facts['hostname']} (${facts['operatingsystem']} ${facts['operatingsystemrelease']}) in ${environment} environment"
$network_summary = "Primary interface: ${primary_ip}/${primary_netmask} on ${networking.hostname}"
$resource_summary = "Resources: ${processor_count} CPUs, ${memory.system.total} RAM"

# Final status
$facts_integration_test = "Facts successfully integrated into Puppet variables"