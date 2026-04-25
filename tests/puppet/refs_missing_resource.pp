# Subscribe to a File that nobody declares — must be flagged.
node default {
  exec { 'broken':
    command => '/bin/true',
    subscribe => [File['/etc/never-declared']],
  }
}
