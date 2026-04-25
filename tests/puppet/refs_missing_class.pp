# Notify a class that's defined but never included — must be flagged.
class never_used {
  notify { "stub": message => "stub" }
}

node default {
  notify { 'check':
    message => 'main',
    notify  => Class['never_used'],
  }
}
