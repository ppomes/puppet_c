file { '/test':
  ensure  => file,
  content => template("test.erb"),
  owner   => 'root'
}