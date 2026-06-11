node "web01.example.com" {
  $my_hostname = $facts['hostname']
  $my_role = $role
  $my_os = $facts['operatingsystem']
}

node "db01.example.com" {
  $my_hostname = $facts['hostname']
  $my_role = $role  
  $my_os = $facts['operatingsystem']
}

node default {
  $my_hostname = $facts['hostname']
  $my_role = $role
  $my_os = $facts['operatingsystem']
}