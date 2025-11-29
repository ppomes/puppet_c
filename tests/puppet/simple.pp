# Simple Puppet manifest for testing

$myvar = "hello"
$num = 42

class myclass {
  file { '/tmp/test':
    ensure => present,
    content => $myvar,
  }
}

include myclass

if $num > 40 {
  notify { 'big number':
    message => "Number is ${num}",
  }
}