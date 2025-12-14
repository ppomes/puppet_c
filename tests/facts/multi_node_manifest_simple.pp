# Multi-node manifest demonstrating node-specific facts
# Each node should get its own facts from the multi_node_facts.json file

node "web01.example.com" {
  # This should use web01's facts: Ubuntu, webserver role
  $node_info = "Node ${hostname}: ${operatingsystem} ${operatingsystemrelease}, Role: ${role}"
  
  if $role == "webserver" {
    $service_type = "apache"
    $port = 80
  }
}

node "db01.example.com" {
  # This should use db01's facts: CentOS, database role
  $node_info = "Node ${hostname}: ${operatingsystem} ${operatingsystemrelease}, Role: ${role}"
  
  if $role == "database" {
    $service_type = "mysql"
    $port = 3306
  }
}

node "default" {
  # This should use default facts: Ubuntu 22.04, unknown role
  $node_info = "Node ${hostname}: ${operatingsystem} ${operatingsystemrelease}, Role: ${role}"
  
  $service_type = "unknown"
  $port = 8080
}