class test {
  file { '/test':
    ensure  => file,
    content => template("test.erb"),
    owner   => 'root',
    group   => 'root',
    mode    => '0644'
  }
}