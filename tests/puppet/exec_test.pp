# Test exec resource

# Simple exec - command in title
exec { '/bin/echo "Hello from exec!"': }

# Exec with creates - only runs if file doesn't exist
exec { 'create_marker':
  command => '/bin/touch /tmp/exec-marker',
  creates => '/tmp/exec-marker',
}

# Exec with onlyif - only runs if check succeeds
exec { 'run_if_tmp_exists':
  command => '/bin/echo "/tmp exists!"',
  onlyif  => '/bin/test -d /tmp',
}
