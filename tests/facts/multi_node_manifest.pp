# Multi-node manifest demonstrating node-specific facts
# Each node should get its own facts from the multi_node_facts.json file

node "web01.example.com" {
  # This should use web01's facts: Ubuntu, webserver role, 4GB RAM
  $ram_amount = $memory.system.total
  $cpu_count = $processors.count
  $node_info = "Node ${hostname}: ${operatingsystem} ${operatingsystemrelease}, Role: ${role}, RAM: ${ram_amount}, CPUs: ${cpu_count}"
  
  if $role == "webserver" {
    $service_type = "apache"
    $port = 80
  }
}

node "db01.example.com" {
  # This should use db01's facts: CentOS, database role, 8GB RAM  
  $ram_amount = $memory.system.total
  $cpu_count = $processors.count
  $node_info = "Node ${hostname}: ${operatingsystem} ${operatingsystemrelease}, Role: ${role}, RAM: ${ram_amount}, CPUs: ${cpu_count}"
  
  if $role == "database" {
    $service_type = "mysql"
    $port = 3306
  }
}

node "default" {
  # This should use default facts: Ubuntu 22.04, unknown role, 2GB RAM
  $ram_amount = $memory.system.total
  $cpu_count = $processors.count
  $node_info = "Node ${hostname}: ${operatingsystem} ${operatingsystemrelease}, Role: ${role}, RAM: ${ram_amount}, CPUs: ${cpu_count}"
  
  $service_type = "unknown"
  $port = 8080
}