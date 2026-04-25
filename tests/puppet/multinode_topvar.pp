# Reproduces the trest1/trest2 pattern: top-level $env depends on
# $hostname, then each node uses $env to drive resource declaration.
# Both parallel and sequential modes must produce a value matching
# the per-node hostname — not the puppetmaster's own hostname.

$env = $hostname ? {
  /^web/   => 'web-env',
  /^db/    => 'db-env',
  /^app/   => 'app-env',
  /^cache/ => 'cache-env',
  default  => 'unknown',
}

node /^(web|db|app|cache)[0-9]+\.example\.com$/ {
  notify { "topvar-${hostname}":
    message => "host=${hostname} env=${env}",
  }
}
