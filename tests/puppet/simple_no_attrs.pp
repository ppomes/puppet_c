$myvar = "hello"
$num = 42

class myclass {
  file { '/tmp/test': }
}

if $num > 40 {
  notify { 'big number': }
}