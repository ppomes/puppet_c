node web01 {
  $my_hostname = $facts['hostname']
  $my_role = $role
  $my_os = $facts['operatingsystem']
}

node db01 {
  $my_hostname = $facts['hostname']
  $my_role = $role  
  $my_os = $facts['operatingsystem']
}

node default {
  $my_hostname = $facts['hostname']
  $my_role = $role
  $my_os = $facts['operatingsystem']
}