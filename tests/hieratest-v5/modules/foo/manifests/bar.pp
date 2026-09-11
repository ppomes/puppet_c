class foo::bar($param = 'DEFAUT') {
  $x = lookup('mykey')
  $y = lookup('mykey_default', 'nope')
  notify { "param=${param} x=${x} y=${y}": }
  # Resolved only by the %{facts.networking.domain} level of the hierarchy
  $z = lookup('domainkey', 'nodomain')
  $g = lookup('globalkey', 'noglobal')
  notify { "z=${z} g=${g}": }
}
