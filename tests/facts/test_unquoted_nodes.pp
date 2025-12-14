node web01 {
  $my_hostname = $hostname
  $my_role = $role
  $my_os = $operatingsystem
}

node db01 {
  $my_hostname = $hostname
  $my_role = $role  
  $my_os = $operatingsystem
}

node default {
  $my_hostname = $hostname
  $my_role = $role
  $my_os = $operatingsystem
}