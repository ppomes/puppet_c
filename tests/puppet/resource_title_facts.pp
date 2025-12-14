# Test resource titles with facts
# Run with: ./src/puppetc --eval --facts tests/facts/sample_facts.json tests/puppet/resource_title_facts.pp

# Using fact directly as resource title
file { $hostname:
  ensure => present,
  content => 'Host specific file',
}

# Using interpolated fact in resource title
file { "/etc/hosts.${operatingsystem}":
  ensure => present,
  owner => 'root',
}

# Complex interpolation with multiple facts
file { "/var/log/${hostname}-${operatingsystemrelease}.log":
  ensure => present,
  mode => '0644',
}

# Mix of local variable and fact
$app = 'myapp'
file { "/opt/${app}/${hostname}.conf":
  ensure => present,
  content => "Configuration for ${hostname} running ${operatingsystem}",
}

# User resource with fact-based name
user { "admin-${hostname}":
  ensure => present,
  comment => "Admin for ${hostname}",
}