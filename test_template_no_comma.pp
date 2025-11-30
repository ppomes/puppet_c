file { '/test':
  ensure => file,
  content => template("test.erb")
}