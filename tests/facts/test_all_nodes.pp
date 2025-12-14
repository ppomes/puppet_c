node "web01.example.com" {
  $my_hostname = $hostname
  $my_role = $role
  $my_os = $operatingsystem
}

node "db01.example.com" {
  $my_hostname = $hostname
  $my_role = $role  
  $my_os = $operatingsystem
}

node default {
  $my_hostname = $hostname
  $my_role = $role
  $my_os = $operatingsystem
}