# Top-level $env drives the hiera %{::env} interpolation.
$env = $hostname ? {
  /^prod/ => 'prod',
  default => 'global',
}

node /^prod-/ {
  include mymod
}

node /^stg-/ {
  include mymod
}
