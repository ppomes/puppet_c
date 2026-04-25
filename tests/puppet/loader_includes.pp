# Exercise the loader paths:
#   - bare 'include loadertest' triggers init.pp autoload
#   - namespaced 'include loadertest::sub' triggers sub.pp autoload
#   - Class['loadertest'] reference must resolve in the catalog
#   - Ruby custom type loadertest_type must be accepted
node default {
  include loadertest
  include loadertest::sub

  loadertest_type { 'rb-instance':
    value => 'configured',
  }

  notify { "ref-check":
    message => "main reached",
    require => Class['loadertest'],
  }
}
