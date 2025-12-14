# Multi-node manifest demonstrating node-specific facts

node "web01.example.com" {
  # This should use web01's facts
  $my_hostname = $hostname
  $my_os = $operatingsystem  
  $my_role = $role
  
  if $role == "webserver" {
    $service_type = "apache"
  }
}

node "db01.example.com" {
  # This should use db01's facts  
  $my_hostname = $hostname
  $my_os = $operatingsystem
  $my_role = $role
  
  if $role == "database" {
    $service_type = "mysql"
  }
}

node "default" {
  # This should use default facts
  $my_hostname = $hostname
  $my_os = $operatingsystem
  $my_role = $role
  
  $service_type = "unknown"
}