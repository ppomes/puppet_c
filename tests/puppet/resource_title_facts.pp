# Test resource titles with facts
# Run with: ./src/puppetc --eval --facts tests/facts/sample_facts.json tests/puppet/resource_title_facts.pp

# Using fact directly as resource title
file { $facts['hostname']:
  ensure => present,
  content => 'Host specific file',
}

# Using interpolated fact in resource title
file { "/etc/hosts.${facts['operatingsystem']}":
  ensure => present,
  owner => 'root',
}

# Complex interpolation with multiple facts
file { "/var/log/${facts['hostname']}-${operatingsystemrelease}.log":
  ensure => present,
  mode => '0644',
}

# Mix of local variable and fact
$app = 'myapp'
file { "/opt/${app}/${facts['hostname']}.conf":
  ensure => present,
  content => "Configuration for ${facts['hostname']} running ${facts['operatingsystem']}",
}

# User resource with fact-based name
user { "admin-${facts['hostname']}":
  ensure => present,
  comment => "Admin for ${facts['hostname']}",
}