# Multi-node isolation: each node should see ONLY its own facts
# bound to top-level vars and resource declarations. State from a
# previous node must not leak into the next compilation.

# Top-level: depends on $hostname
$jbossenv = $hostname ? {
  /^alpha/ => 'web',
  /^beta/  => 'db',
  /^gamma/ => 'cache',
  default  => 'unknown',
}

# Top-level: depends on $role (per-node fact)
$role_path = $role ? {
  'web'   => '/srv/www',
  'db'    => '/srv/db',
  'cache' => '/srv/cache',
  default => '/srv/unknown',
}

class profile {
  notify { "profile-info":
    message => "host=${hostname} jbossenv=${jbossenv} role_path=${role_path} nodepath=${nodepath}",
  }
}

node /^(alpha|beta|gamma)\.example\.com$/ {
  include profile
}
