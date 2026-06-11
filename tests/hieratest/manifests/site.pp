# Top-level $env drives the hiera %{::env} interpolation.
$env = $facts['hostname'] ? {
  /^prod/ => 'prod',
  default => 'global',
}

node /^prod-/ {
  include mymod
}

node /^stg-/ {
  include mymod
}
